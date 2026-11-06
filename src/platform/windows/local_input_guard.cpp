/**
 * @file src/platform/windows/local_input_guard.cpp
 * @brief Definitions for suppressing the physical mouse while a remote client is in control.
 */
// standard includes
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

// platform includes
#include <Windows.h>

// local includes
#include "src/input_arbiter.h"
#include "src/logging.h"
#include "src/platform/windows/local_input_guard.h"

#ifndef LLMHF_LOWER_IL_INJECTED
  #define LLMHF_LOWER_IL_INJECTED 0x00000002  // missing from some toolchain headers
#endif

namespace platf::local_input_guard {
  using namespace std::literals;

  namespace {
    constexpr ULONG_PTR TOUCH_SIGNATURE_MASK = 0xFFFFFF00;  ///< Mask of the signature Windows puts in touch-synthesized mouse events.
    constexpr ULONG_PTR TOUCH_SIGNATURE = 0xFF515700;  ///< MI_WP_SIGNATURE, set for mouse events created from touch or pen.

    std::mutex state_mutex;  ///< Guards thread lifetime.
    std::thread hook_thread;  ///< Thread that owns the hook and pumps its messages.
    std::atomic<DWORD> hook_thread_id {0};  ///< Native id of hook_thread, used to post WM_QUIT.

    /**
     * @brief Low-level mouse hook that drops physical mouse events during remote control.
     *
     * @param code Hook code.
     * @param wparam Message identifier.
     * @param lparam Pointer to MSLLHOOKSTRUCT.
     * @return 1 to swallow the event, otherwise the result of the next hook.
     */
    LRESULT CALLBACK mouse_hook(int code, WPARAM wparam, LPARAM lparam) {
      if (code == HC_ACTION) {
        const auto *info = reinterpret_cast<const MSLLHOOKSTRUCT *>(lparam);
        const bool injected = (info->flags & (LLMHF_INJECTED | LLMHF_LOWER_IL_INJECTED)) != 0;
        const bool from_touch = (info->dwExtraInfo & TOUCH_SIGNATURE_MASK) == TOUCH_SIGNATURE;
        if (!injected && !from_touch && input::arbiter::local_mouse_blocked()) {
          return 1;
        }
      }
      return CallNextHookEx(nullptr, code, wparam, lparam);
    }

    /**
     * @brief Thread body: install the hook and pump messages until WM_QUIT.
     */
    void run_hook_thread() {
      HHOOK hook = SetWindowsHookExW(WH_MOUSE_LL, mouse_hook, GetModuleHandleW(nullptr), 0);
      if (!hook) {
        BOOST_LOG(warning) << "Unable to install the local mouse guard, error ["sv << GetLastError() << ']';
        hook_thread_id = 0;
        return;
      }
      BOOST_LOG(info) << "Local mouse guard installed; the physical mouse is ignored while a client is active"sv;

      MSG msg;
      while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
      UnhookWindowsHookEx(hook);
    }
  }  // namespace

  void ensure_started() {
    std::lock_guard lock {state_mutex};
    if (hook_thread.joinable()) {
      return;
    }
    hook_thread = std::thread([]() {
      hook_thread_id = GetCurrentThreadId();
      run_hook_thread();
    });
  }

  void stop() {
    std::lock_guard lock {state_mutex};
    if (!hook_thread.joinable()) {
      return;
    }
    // The thread publishes its id right after it starts; give it a moment if stop() raced with startup.
    for (int i = 0; i < 50 && hook_thread_id.load() == 0 && hook_thread.joinable(); ++i) {
      std::this_thread::sleep_for(2ms);
    }
    if (auto id = hook_thread_id.load(); id != 0) {
      PostThreadMessageW(id, WM_QUIT, 0, 0);
    }
    hook_thread.join();
    hook_thread_id = 0;
  }
}  // namespace platf::local_input_guard
