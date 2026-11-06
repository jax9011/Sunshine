/**
 * @file src/platform/windows/window_host.cpp
 * @brief Definitions for tracking the window of an app that is streamed on its own (Windows).
 *
 * @details The window is captured directly by Windows.Graphics.Capture, so nothing here changes how it looks.
 * The watcher only finds the app's main window, keeps it un-minimized and in place (input coordinates depend on
 * its position), and reports when it has been closed so the session can end.
 */
// standard includes
#include <algorithm>
#include <string_view>
#include <utility>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

// platform includes
#include <Windows.h>
#include <dwmapi.h>
#include <tlhelp32.h>

// local includes
#include "src/logging.h"
#include "src/platform/windows/utf_utils.h"
#include "src/platform/windows/window_host.h"
#include "src/window_policy.h"

namespace platf::window_host {
  using namespace std::literals;

  namespace {
    constexpr auto POLL_INTERVAL = 250ms;  ///< Time between watcher sweeps.
    constexpr auto REPLACEMENT_GRACE = 1500ms;  ///< How long to look for a successor window before reporting a close.

    /**
     * @brief Snapshot the process tree.
     *
     * @return (child, parent) pairs for every running process.
     */
    std::vector<window_policy::process_edge_t> snapshot_processes() {
      std::vector<window_policy::process_edge_t> edges;
      HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
      if (snap == INVALID_HANDLE_VALUE) {
        return edges;
      }
      PROCESSENTRY32W entry {};
      entry.dwSize = sizeof(entry);
      for (BOOL ok = Process32FirstW(snap, &entry); ok; ok = Process32NextW(snap, &entry)) {
        edges.emplace_back(entry.th32ProcessID, entry.th32ParentProcessID);
      }
      CloseHandle(snap);
      return edges;
    }

    /**
     * @brief Read a window's title.
     *
     * @param hwnd Window handle.
     * @return UTF-8 title, possibly empty.
     */
    std::string window_title(HWND hwnd) {
      wchar_t buffer[256] {};
      GetWindowTextW(hwnd, buffer, 256);
      return utf_utils::to_utf8(buffer);
    }

    /**
     * @brief Check whether a window is an ordinary, user-visible top-level window.
     *
     * @param hwnd Window handle.
     * @return `true` for real application windows.
     */
    bool is_app_window(HWND hwnd) {
      if (GetWindow(hwnd, GW_OWNER) != nullptr) {
        return false;
      }
      const auto ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
      if ((ex & WS_EX_TOOLWINDOW) || (ex & WS_EX_NOACTIVATE)) {
        return false;
      }
      // Minimized windows are still app windows; they are restored once adopted.
      if (!IsWindowVisible(hwnd)) {
        return false;
      }
      int cloaked = 0;
      DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
      if (cloaked) {
        return false;
      }
      wchar_t cls[256] {};
      GetClassNameW(hwnd, cls, 256);
      const std::wstring_view name {cls};
      return !(name == L"Progman" || name == L"WorkerW" || name == L"Shell_TrayWnd" || name == L"Shell_SecondaryTrayWnd" || name == L"Windows.UI.Core.CoreWindow");
    }

    /**
     * @brief Context handed to the EnumWindows callback.
     */
    struct enum_ctx_t {
      const options_t *options;  ///< Tracking settings.
      const std::unordered_set<std::uint32_t> *family;  ///< App process tree.
      DWORD own_pid;  ///< Sunshine's pid; its windows are never captured.
      std::vector<HWND> app_windows;  ///< Windows that belong to the app.
    };

    /**
     * @brief EnumWindows callback that collects the windows belonging to the app.
     *
     * @param hwnd Window handle.
     * @param lparam Pointer to enum_ctx_t.
     * @return TRUE to continue.
     */
    BOOL CALLBACK enum_proc(HWND hwnd, LPARAM lparam) {
      auto &ctx = *reinterpret_cast<enum_ctx_t *>(lparam);
      if (!is_app_window(hwnd)) {
        return TRUE;
      }
      DWORD pid = 0;
      GetWindowThreadProcessId(hwnd, &pid);
      if (pid == ctx.own_pid) {
        return TRUE;
      }
      if (ctx.family->count(pid) != 0 || window_policy::title_matches(ctx.options->title_match, window_title(hwnd))) {
        ctx.app_windows.push_back(hwnd);
      }
      return TRUE;
    }

    /**
     * @brief Pick the largest window from a list.
     *
     * @param windows Candidate windows.
     * @return The largest one, or nullptr for an empty list.
     */
    HWND largest(const std::vector<HWND> &windows) {
      HWND best = nullptr;
      long long best_area = -1;
      for (auto hwnd : windows) {
        RECT r {};
        GetWindowRect(hwnd, &r);
        const long long area = static_cast<long long>(r.right - r.left) * (r.bottom - r.top);
        if (area > best_area) {
          best_area = area;
          best = hwnd;
        }
      }
      return best;
    }

    /**
     * @brief Width and height of a rectangle.
     *
     * @param r Rectangle.
     * @return Size as a pair.
     */
    std::pair<LONG, LONG> size_of(const RECT &r) {
      return {r.right - r.left, r.bottom - r.top};
    }
  }  // namespace

  void focus_window(std::uint64_t window) {
    auto hwnd = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(window));
    if (!hwnd || !IsWindow(hwnd) || GetForegroundWindow() == hwnd) {
      return;
    }
    if (IsIconic(hwnd)) {
      ShowWindow(hwnd, SW_RESTORE);
    }
    // Windows only lets the foreground thread change the foreground window, so briefly share its input queue.
    const auto foreground_thread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    const auto this_thread = GetCurrentThreadId();
    const bool attached = foreground_thread != 0 && foreground_thread != this_thread && AttachThreadInput(this_thread, foreground_thread, TRUE);
    SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(hwnd);
    if (attached) {
      AttachThreadInput(this_thread, foreground_thread, FALSE);
    }
  }

  watcher_t::~watcher_t() {
    stop();
  }

  void watcher_t::start(const options_t &options) {
    stop();
    {
      std::lock_guard lock {wake_mutex};
      stop_requested = false;
    }
    hwnd = 0;
    closed = false;

    thread = std::thread([this, options]() {
      // Use physical pixels on every monitor. Looked up at run time because not every toolchain declares it.
      if (auto user32 = GetModuleHandleW(L"user32.dll")) {
        using set_thread_dpi_t = HANDLE(WINAPI *)(HANDLE);
        if (auto set_dpi = reinterpret_cast<set_thread_dpi_t>(reinterpret_cast<void *>(GetProcAddress(user32, "SetThreadDpiAwarenessContext")))) {
          set_dpi(reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(-4)));  // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
        }
      }

      const auto started = std::chrono::steady_clock::now();
      std::chrono::steady_clock::time_point missing_since {};
      HWND current = nullptr;
      RECT pinned {};
      bool ever_adopted = false;
      bool gave_up_logged = false;

      auto sleep_or_stop = [this]() {
        std::unique_lock lock {wake_mutex};
        return wake.wait_for(lock, POLL_INTERVAL, [this]() {
          return stop_requested;
        });
      };

      while (!sleep_or_stop()) {
        if (current && !IsWindow(current)) {
          // The window is gone; start the grace period in which a successor (splash -> main window) may appear.
          current = nullptr;
          hwnd = 0;
          missing_since = std::chrono::steady_clock::now();
        }

        if (!current) {
          const auto family = window_policy::collect_descendants(options.root_pid, snapshot_processes());
          enum_ctx_t ctx {&options, &family, GetCurrentProcessId(), {}};
          EnumWindows(enum_proc, reinterpret_cast<LPARAM>(&ctx));

          if (auto candidate = largest(ctx.app_windows)) {
            current = candidate;
            ever_adopted = true;
            if (IsIconic(current)) {
              ShowWindow(current, SW_RESTORE);
            }
            GetWindowRect(current, &pinned);
            hwnd = reinterpret_cast<std::uintptr_t>(current);
            BOOST_LOG(info) << "Streaming window ["sv << window_title(current) << ']';
          } else if (ever_adopted) {
            if (std::chrono::steady_clock::now() - missing_since >= REPLACEMENT_GRACE) {
              BOOST_LOG(info) << "The streamed window was closed"sv;
              closed = true;
              return;
            }
          } else if (!gave_up_logged && std::chrono::steady_clock::now() - started > options.adopt_timeout) {
            BOOST_LOG(warning) << "No window found for the app yet"sv;
            gave_up_logged = true;
          }
          continue;
        }

        // Windows can't be captured while minimized.
        if (IsIconic(current)) {
          ShowWindow(current, SW_RESTORE);
        }

        // Keep the window where input coordinates expect it; follow it when its size (not position) changed.
        RECT now {};
        GetWindowRect(current, &now);
        if (size_of(now) != size_of(pinned)) {
          pinned = now;
        } else if (now.left != pinned.left || now.top != pinned.top) {
          SetWindowPos(current, nullptr, pinned.left, pinned.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
      }
    });
  }

  void watcher_t::stop() {
    if (!thread.joinable()) {
      return;
    }
    {
      std::lock_guard lock {wake_mutex};
      stop_requested = true;
    }
    wake.notify_all();
    thread.join();
  }

  std::uint64_t watcher_t::window() const {
    return hwnd.load();
  }

  bool watcher_t::window_closed() const {
    return closed.load();
  }

  std::uint64_t watcher_t::wait_for_window(std::chrono::seconds timeout) const {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      if (auto w = hwnd.load()) {
        return w;
      }
      if (closed.load()) {
        break;
      }
      std::this_thread::sleep_for(100ms);
    }
    return hwnd.load();
  }
}  // namespace platf::window_host
