/**
 * @file src/platform/windows/vr_compose.cpp
 * @brief Definitions for the VR "composed windows" capture (Windows).
 */
// standard includes
#include <algorithm>
#include <map>
#include <mutex>

// platform includes
#include <Windows.h>

// local includes
#include "src/config.h"
#include "src/platform/windows/vr_compose.h"
#include "src/platform/windows/window_host.h"
#include "src/platform/windows/vr_windows.h"

namespace platf::vr_compose {
  namespace {
    std::mutex state_mutex;  ///< Guards everything below.
    std::vector<std::uint64_t> requested;  ///< Windows asked for by the client.
    std::map<std::uint64_t, std::pair<int, int>> actual_sizes;  ///< Sizes the capture really delivers.
    layout_t layout;  ///< Latest layout.
    std::atomic<std::uint64_t> focused_hwnd {0};  ///< Window that receives input.
    bool auto_select_all = true;  ///< Start in window-only mode with all eligible top-level windows selected.

    constexpr double scales[] = {1.0, 1.5, 2.0, 3.0};  ///< Atlas sizes tried, in screens per side.
    constexpr int max_atlas_side = 16384;  ///< Largest D3D11 texture side.

    HWND to_hwnd(std::uint64_t id) {
      return reinterpret_cast<HWND>(static_cast<std::uintptr_t>(id));
    }

    int even(double v) {
      return std::max(2, static_cast<int>(v) & ~1);
    }

    /**
     * @brief Shelf-pack the slots into an atlas of the given size. Windows that do not fit are dropped.
     */
    bool pack(std::vector<slot_t> &slots, int atlas_w, int atlas_h) {
      int x = 0;
      int y = 0;
      int row_h = 0;
      for (auto &s : slots) {
        if (s.aw > atlas_w || s.ah > atlas_h) {
          return false;
        }
        if (x + s.aw > atlas_w) {
          x = 0;
          y += row_h;
          row_h = 0;
        }
        if (y + s.ah > atlas_h) {
          return false;
        }
        s.ax = x;
        s.ay = y;
        x += s.aw;
        row_h = std::max(row_h, s.ah);
      }
      return true;
    }

    bool same_layout(const layout_t &a, const layout_t &b) {
      if (a.atlas_w != b.atlas_w || a.atlas_h != b.atlas_h || a.slots.size() != b.slots.size()) {
        return false;
      }
      for (size_t i = 0; i < a.slots.size(); ++i) {
        const auto &p = a.slots[i];
        const auto &q = b.slots[i];
        if (p.hwnd != q.hwnd || p.ax != q.ax || p.ay != q.ay || p.aw != q.aw || p.ah != q.ah || p.dx != q.dx || p.dy != q.dy || p.members.size() != q.members.size()) {
          return false;
        }
        for (size_t k = 0; k < p.members.size(); ++k) {
          const auto &m = p.members[k];
          const auto &n = q.members[k];
          if (m.hwnd != n.hwnd || m.ox != n.ox || m.oy != n.oy || m.w != n.w || m.h != n.h) {
            return false;
          }
        }
      }
      return true;
    }
  }  // namespace

  bool enabled() {
    return config::vr_compose_enabled;
  }

  void set_windows(const std::vector<std::uint64_t> &ids) {
    std::lock_guard lock {state_mutex};
    requested.clear();
    auto_select_all = ids.empty();
    for (auto id : ids) {
      if (requested.size() >= 16) {
        break;
      }
      if (std::find(requested.begin(), requested.end(), id) == requested.end()) {
        requested.push_back(id);
      }
    }
  }

  void set_actual_size(std::uint64_t hwnd, int w, int h) {
    std::lock_guard lock {state_mutex};
    if (w <= 0 || h <= 0) {
      actual_sizes.erase(hwnd);
    } else {
      actual_sizes[hwnd] = {w, h};
    }
  }

  void refresh(int screen_w, int screen_h) {
    // Physical pixels on every monitor, like the window list
    const auto previous_dpi = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    std::lock_guard lock {state_mutex};

    // Size and place of every requested window
    struct info_t {
      std::uint64_t id;
      std::uint64_t owner;
      int left;
      int top;
      int w;
      int h;
    };
    if (auto_select_all && requested.empty()) {
      const auto snap = platf::vr::list_windows();
      for (const auto &w : snap.windows) {
        if (requested.size() >= 16) {
          break;
        }
        const auto id = static_cast<std::uint64_t>(w.id);
        if (std::find(requested.begin(), requested.end(), id) == requested.end()) {
          requested.push_back(id);
        }
      }
    }

    std::vector<info_t> infos;
    for (auto id : requested) {
      auto hwnd = to_hwnd(id);
      if (!IsWindow(hwnd) || IsIconic(hwnd)) {
        continue;  // a minimized window cannot be captured
      }
      RECT r {};
      if (!GetWindowRect(hwnd, &r)) {
        continue;
      }
      int w = static_cast<int>(r.right - r.left);
      int h = static_cast<int>(r.bottom - r.top);
      if (auto it = actual_sizes.find(id); it != actual_sizes.end()) {
        w = it->second.first;
        h = it->second.second;
      }
      if (w < 8 || h < 8) {
        continue;
      }
      infos.push_back({id, static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(GetWindow(hwnd, GW_OWNER))), static_cast<int>(r.left), static_cast<int>(r.top), w, h});
    }

    // A window owned by another requested window (dialog, popup) goes into that window's slot
    auto find_info = [&infos](std::uint64_t id) -> const info_t * {
      for (const auto &i : infos) {
        if (i.id == id) {
          return &i;
        }
      }
      return nullptr;
    };
    auto root_of = [&](const info_t &i) {
      std::uint64_t id = i.id;
      for (int depth = 0; depth < 8; ++depth) {
        const auto *cur = find_info(id);
        if (!cur || cur->owner == 0 || !find_info(cur->owner)) {
          return id;
        }
        id = cur->owner;
      }
      return id;
    };

    std::vector<slot_t> slots;
    for (const auto &i : infos) {
      if (root_of(i) != i.id) {
        continue;  // handled with its owner
      }
      slot_t slot {i.id, 0, 0, 0, 0, 0, 0, {}};
      int left = i.left;
      int top = i.top;
      int right = i.left + i.w;
      int bottom = i.top + i.h;
      std::vector<const info_t *> group;
      for (const auto &m : infos) {
        if (root_of(m) == i.id) {
          group.push_back(&m);
          left = std::min(left, m.left);
          top = std::min(top, m.top);
          right = std::max(right, m.left + m.w);
          bottom = std::max(bottom, m.top + m.h);
        }
      }
      slot.dx = left;
      slot.dy = top;
      slot.aw = right - left;
      slot.ah = bottom - top;
      for (const auto *m : group) {
        slot.members.push_back({m->id, m->left - left, m->top - top, m->w, m->h});
      }
      slots.push_back(std::move(slot));
    }

    layout_t next;
    next.screen_w = screen_w;
    next.screen_h = screen_h;
    next.atlas_w = screen_w;
    next.atlas_h = screen_h;

    bool fitted = false;
    for (auto scale : scales) {
      const int aw = even(screen_w * scale);
      const int ah = even(screen_h * scale);
      if (aw > max_atlas_side || ah > max_atlas_side) {
        break;
      }
      next.atlas_w = aw;
      next.atlas_h = ah;
      if (pack(slots, aw, ah)) {
        fitted = true;
        break;
      }
    }
    while (!fitted && !slots.empty()) {
      slots.pop_back();  // the newest windows are dropped until the rest fit the largest atlas
      fitted = pack(slots, next.atlas_w, next.atlas_h);
    }
    next.slots = std::move(slots);

    next.version = layout.version;
    if (!same_layout(layout, next) || layout.screen_w != screen_w || layout.screen_h != screen_h) {
      next.version = layout.version + 1;
      layout = std::move(next);
    }

    if (previous_dpi) {
      SetThreadDpiAwarenessContext(previous_dpi);
    }
  }

  layout_t current() {
    std::lock_guard lock {state_mutex};
    return layout;
  }

  void set_focus_target(std::uint64_t hwnd) {
    focused_hwnd.store(hwnd, std::memory_order_relaxed);
  }

  std::uint64_t focus_target() {
    return focused_hwnd.load(std::memory_order_relaxed);
  }

  void apply_focus() {
    if (!enabled()) {
      return;
    }
    if (const auto target = focused_hwnd.load(std::memory_order_relaxed)) {
      window_host::focus_window(target);
    }
  }
}  // namespace platf::vr_compose
