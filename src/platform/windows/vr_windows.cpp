/**
 * @file src/platform/windows/vr_windows.cpp
 * @brief Definitions for reporting desktop windows and Start Menu apps to the VR Moonlight client.
 */
// standard includes
#include <algorithm>
#include <cctype>
#include <cstring>
#include <mutex>
#include <system_error>

// platform includes
#include <Windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <ShlObj.h>

// local includes
#include "src/platform/windows/utf_utils.h"
#include "src/platform/windows/vr_windows.h"

namespace platf::vr {
  namespace {
    std::mutex apps_mutex;  ///< Guards last_apps.
    std::vector<app_entry_t> last_apps;  ///< Result of the most recent scan.

    /**
     * @brief Switches the calling thread to per-monitor DPI awareness so coordinates are real pixels.
     */
    class dpi_scope_t {
    public:
      dpi_scope_t():
          previous(SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
      }

      ~dpi_scope_t() {
        if (previous) {
          SetThreadDpiAwarenessContext(previous);
        }
      }

      dpi_scope_t(const dpi_scope_t &) = delete;
      dpi_scope_t &operator=(const dpi_scope_t &) = delete;

    private:
      DPI_AWARENESS_CONTEXT previous;  ///< Context to restore.
    };

    /**
     * @brief State passed to the EnumWindows callback.
     */
    struct enum_state_t {
      window_snapshot_t *snapshot;  ///< Output snapshot.
    };

    /**
     * @brief Read a window attribute string through a Win32 getter.
     *
     * @param hwnd Window handle.
     * @param getter Function such as GetWindowTextW or GetClassNameW.
     * @return The text, or an empty string.
     */
    template<typename Getter>
    std::wstring read_text(HWND hwnd, Getter getter) {
      wchar_t buffer[256] {};
      getter(hwnd, buffer, 256);
      return buffer;
    }

    /**
     * @brief EnumWindows callback that appends one window to the snapshot when it qualifies.
     *
     * @param hwnd Window handle.
     * @param lparam Pointer to enum_state_t.
     * @return TRUE to continue enumerating.
     */
    BOOL CALLBACK enum_proc(HWND hwnd, LPARAM lparam) {
      auto &state = *reinterpret_cast<enum_state_t *>(lparam);
      auto &snap = *state.snapshot;

      if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) {
        return TRUE;
      }

      int cloaked = 0;
      DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
      if (cloaked) {
        return TRUE;
      }

      auto cls = read_text(hwnd, GetClassNameW);
      if (cls == L"Progman" || cls == L"WorkerW" || cls == L"Shell_TrayWnd" ||
          cls == L"Shell_SecondaryTrayWnd" || cls == L"Windows.UI.Core.CoreWindow") {
        return TRUE;
      }

      // Click-through overlays (layered + transparent) must not become clickable areas.
      auto ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
      if ((ex & WS_EX_LAYERED) && (ex & WS_EX_TRANSPARENT)) {
        return TRUE;
      }

      RECT r {};
      if (DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)) != S_OK) {
        GetWindowRect(hwnd, &r);
      }

      auto left = std::max<LONG>(r.left, 0);
      auto top = std::max<LONG>(r.top, 0);
      auto right = std::min<LONG>(r.right, snap.screen_w);
      auto bottom = std::min<LONG>(r.bottom, snap.screen_h);
      if (right - left < 4 || bottom - top < 4) {
        return TRUE;
      }

      window_info_t info {};
      DWORD pid = 0;
      GetWindowThreadProcessId(hwnd, &pid);
      info.id = static_cast<long long>(reinterpret_cast<intptr_t>(hwnd));
      info.pid = pid;
      info.owner = static_cast<long long>(reinterpret_cast<intptr_t>(GetWindow(hwnd, GW_OWNER)));
      info.x = static_cast<double>(left) / snap.screen_w;
      info.y = static_cast<double>(top) / snap.screen_h;
      info.w = static_cast<double>(right - left) / snap.screen_w;
      info.h = static_cast<double>(bottom - top) / snap.screen_h;
      info.z = static_cast<int>(snap.windows.size());
      info.title = utf_utils::to_utf8(read_text(hwnd, GetWindowTextW));
      snap.windows.push_back(std::move(info));
      return TRUE;
    }

    /**
     * @brief Append every .lnk found under a Start Menu folder.
     *
     * @param csidl CSIDL of the Start Menu Programs folder (per-user or all-users).
     * @param found Output list.
     */
    void collect_shortcuts(int csidl, std::vector<app_entry_t> &found) {
      wchar_t raw[MAX_PATH] {};
      if (SHGetFolderPathW(nullptr, csidl, nullptr, SHGFP_TYPE_CURRENT, raw) != S_OK) {
        return;
      }
      std::filesystem::path root {raw};

      std::error_code ec;
      std::filesystem::recursive_directory_iterator it {root, std::filesystem::directory_options::skip_permission_denied, ec};
      for (; !ec && it != std::filesystem::recursive_directory_iterator {}; it.increment(ec)) {
        const auto &p = it->path();
        if (p.extension() != L".lnk") {
          continue;
        }
        auto name = utf_utils::to_utf8(p.stem().wstring());
        auto lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
          return static_cast<char>(std::tolower(c));
        });
        if (lower.find("uninstall") != std::string::npos) {
          continue;
        }
        found.push_back({0, std::move(name), p});
      }
    }

    /**
     * @brief Remove helper windows that sit completely inside another top-level window of the same app.
     *
     * @details Some apps (OBS, Qt apps) own extra top-level windows that cover part of their main window.
     * Shown as separate VR windows they look like a second copy of the app. Dialogs have an owner window
     * and are kept; only unowned windows of the same process that lie inside a bigger one are dropped.
     *
     * @param snap Snapshot to filter in place; stacking order is renumbered.
     */
    void drop_duplicate_windows(window_snapshot_t &snap) {
      constexpr double tol = 0.002;  // about 4 px on a 1920 px screen
      auto &all = snap.windows;
      std::vector<window_info_t> kept;
      for (size_t i = 0; i < all.size(); ++i) {
        const auto &a = all[i];
        bool inside = false;
        if (a.owner == 0) {
          for (size_t j = 0; j < all.size() && !inside; ++j) {
            if (j == i || all[j].pid != a.pid) {
              continue;
            }
            const auto &b = all[j];
            const bool contained = a.x >= b.x - tol && a.y >= b.y - tol && a.x + a.w <= b.x + b.w + tol && a.y + a.h <= b.y + b.h + tol;
            const bool bigger = b.w * b.h > a.w * a.h * 1.02;
            const bool same_size_in_front = !bigger && b.w * b.h >= a.w * a.h * 0.98 && j < i;
            inside = contained && (bigger || same_size_in_front);
          }
        }
        if (!inside) {
          kept.push_back(a);
        }
      }
      for (size_t i = 0; i < kept.size(); ++i) {
        kept[i].z = static_cast<int>(i);
      }
      all = std::move(kept);
    }
  }  // namespace

  window_snapshot_t list_windows() {
    dpi_scope_t dpi;
    window_snapshot_t snap {};
    snap.screen_w = std::max(GetSystemMetrics(SM_CXSCREEN), 1);
    snap.screen_h = std::max(GetSystemMetrics(SM_CYSCREEN), 1);
    enum_state_t state {&snap};
    EnumWindows(enum_proc, reinterpret_cast<LPARAM>(&state));  // top to bottom
    drop_duplicate_windows(snap);
    return snap;
  }

  std::vector<app_entry_t> scan_apps() {
    std::vector<app_entry_t> found;
    collect_shortcuts(CSIDL_PROGRAMS, found);
    collect_shortcuts(CSIDL_COMMON_PROGRAMS, found);

    std::sort(found.begin(), found.end(), [](const app_entry_t &a, const app_entry_t &b) {
      auto la = a.name;
      auto lb = b.name;
      std::transform(la.begin(), la.end(), la.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      std::transform(lb.begin(), lb.end(), lb.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      return la < lb;
    });

    // keep the first shortcut of each name
    std::vector<app_entry_t> unique;
    for (auto &entry : found) {
      auto same = std::any_of(unique.begin(), unique.end(), [&entry](const app_entry_t &u) {
        return _stricmp(u.name.c_str(), entry.name.c_str()) == 0;
      });
      if (!same) {
        entry.id = static_cast<int>(unique.size());
        unique.push_back(std::move(entry));
      }
    }

    std::lock_guard lock {apps_mutex};
    last_apps = unique;
    return unique;
  }

  bool launch_app(int id, std::string &name_out) {
    std::filesystem::path path;
    {
      std::lock_guard lock {apps_mutex};
      if (id < 0 || id >= static_cast<int>(last_apps.size())) {
        return false;
      }
      path = last_apps[id].path;
      name_out = last_apps[id].name;
    }
    auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    return result > 32;
  }
}  // namespace platf::vr
