/**
 * @file src/platform/windows/local_input_guard.h
 * @brief Declarations for suppressing the physical mouse while a remote client is in control.
 */
#pragma once

namespace platf::local_input_guard {
  /**
   * @brief Install the low-level mouse hook on a dedicated thread if it is not already running.
   *
   * @details Events injected by software (including Sunshine's own SendInput calls and touch-synthesized
   * mouse events) are never touched; only events from a physical device are dropped, and only while
   * input::arbiter::local_mouse_blocked() is true.
   */
  void ensure_started();

  /**
   * @brief Remove the hook and stop its thread. Safe to call when the hook was never installed.
   */
  void stop();
}  // namespace platf::local_input_guard
