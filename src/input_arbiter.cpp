/**
 * @file src/input_arbiter.cpp
 * @brief Definitions for remote versus local mouse arbitration.
 */
// standard includes
#include <atomic>
#include <chrono>

// local includes
#include "src/config.h"
#include "src/input_arbiter.h"
#ifdef _WIN32
  #include "src/platform/windows/local_input_guard.h"
#endif

namespace input::arbiter {
  namespace {
    std::atomic<std::int64_t> last_remote_activity {-1};  ///< Steady-clock milliseconds of the last remote input.

    /**
     * @brief Read the monotonic clock in milliseconds.
     *
     * @return Milliseconds since an arbitrary epoch.
     */
    std::int64_t now_ms() {
      return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
  }  // namespace

  bool should_block_local(std::string_view policy, std::int64_t now_ms, std::int64_t last_remote_ms, int grace_ms) {
    if (policy != POLICY_REMOTE_PRIORITY || last_remote_ms < 0) {
      return false;
    }
    return now_ms - last_remote_ms <= grace_ms;
  }

  void note_remote_activity() {
    if (config::input.local_mouse_policy != POLICY_REMOTE_PRIORITY) {
      return;
    }
    last_remote_activity.store(now_ms(), std::memory_order_relaxed);
#ifdef _WIN32
    platf::local_input_guard::ensure_started();
#endif
  }

  bool local_mouse_blocked() {
    return should_block_local(
      config::input.local_mouse_policy,
      now_ms(),
      last_remote_activity.load(std::memory_order_relaxed),
      config::input.local_mouse_grace_ms
    );
  }
}  // namespace input::arbiter
