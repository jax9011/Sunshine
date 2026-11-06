/**
 * @file src/input_arbiter.h
 * @brief Decides whether a physical mouse on the host may act while a remote client is controlling it.
 */
#pragma once

// standard includes
#include <cstdint>
#include <string_view>

namespace input::arbiter {
  inline constexpr std::string_view POLICY_ALLOW = "allow";  ///< Local and remote input work together (upstream behavior).
  inline constexpr std::string_view POLICY_REMOTE_PRIORITY = "remote_priority";  ///< Local mouse is ignored while the client is active.

  /**
   * @brief Decide whether a physical mouse event should be dropped.
   *
   * @param policy Value of the `local_mouse_policy` option.
   * @param now_ms Current time in milliseconds.
   * @param last_remote_ms Time of the last remote input in milliseconds, or a negative value if there was none.
   * @param grace_ms How long after the last remote input the physical mouse stays suppressed.
   * @return `true` if the physical mouse event must be dropped.
   */
  bool should_block_local(std::string_view policy, std::int64_t now_ms, std::int64_t last_remote_ms, int grace_ms);

  /**
   * @brief Record that a remote client just sent mouse, keyboard, touch or pen input.
   *
   * @details Cheap enough to call for every input packet. The first call with an active policy installs the
   * platform hook that enforces it.
   */
  void note_remote_activity();

  /**
   * @brief Check the current state against the configured policy.
   *
   * @return `true` if a physical mouse event arriving right now must be dropped.
   */
  bool local_mouse_blocked();
}  // namespace input::arbiter
