/**
 * @file src/platform/windows/vr_compose.h
 * @brief Declarations for the VR "composed windows" capture (Windows).
 *
 * @details Instead of streaming the desktop, the VR client can ask for a list of windows. VR composition is strictly window-only; desktop capture is never used as a fallback. Each one is
 * captured on its own (Windows.Graphics.Capture, like OBS "Window Capture") and placed side by side in
 * one atlas image, which is what gets encoded. Windows that overlap on the real desktop therefore never
 * show up inside each other's picture.
 */
#pragma once

// standard includes
#include <atomic>
#include <cstdint>
#include <vector>

namespace platf::vr_compose {
  /**
   * @brief One window drawn into a slot.
   */
  struct member_t {
    std::uint64_t hwnd;  ///< Native window handle.
    int ox;  ///< Left edge inside the slot, in pixels.
    int oy;  ///< Top edge inside the slot, in pixels.
    int w;  ///< Width of the window picture, in pixels.
    int h;  ///< Height of the window picture, in pixels.
  };

  /**
   * @brief One composed app window. A dialog or popup owned by the window is drawn inside the same slot,
   * where it sits on the real desktop, so it stays inside the frame of its app.
   */
  struct slot_t {
    std::uint64_t hwnd;  ///< Native window handle of the main (owner) window.
    int ax;  ///< Left edge of the slot inside the atlas, in pixels.
    int ay;  ///< Top edge of the slot inside the atlas, in pixels.
    int aw;  ///< Width of the slot (the window and its dialogs), in pixels.
    int ah;  ///< Height of the slot, in pixels.
    int dx;  ///< Left edge of the slot on the real desktop, in pixels (primary monitor origin).
    int dy;  ///< Top edge of the slot on the real desktop, in pixels.
    std::vector<member_t> members;  ///< Windows of the slot, front-most first.
  };

  /**
   * @brief Where every composed window sits in the atlas.
   */
  struct layout_t {
    int atlas_w = 0;  ///< Atlas width in pixels. Same aspect as the screen.
    int atlas_h = 0;  ///< Atlas height in pixels.
    int screen_w = 0;  ///< Primary monitor width in pixels.
    int screen_h = 0;  ///< Primary monitor height in pixels.
    unsigned version = 0;  ///< Increases whenever the layout changes.
    std::vector<slot_t> slots;  ///< Composed windows.
  };

  /**
   * @brief Whether composed capture is switched on (`vr_compose` option).
   */
  bool enabled();

  /**
   * @brief Replace the list of windows to compose. Called by the VR client through `/vrcompose`.
   *
   * @param ids Native window handles, in the order they should be packed.
   */
  void set_windows(const std::vector<std::uint64_t> &ids);

  /**
   * @brief Recompute the layout from the current window sizes.
   *
   * @param screen_w Primary monitor width in pixels.
   * @param screen_h Primary monitor height in pixels.
   */
  void refresh(int screen_w, int screen_h);

  /**
   * @brief Thread-safe copy of the current layout.
   */
  layout_t current();

  /**
   * @brief Record the size the capture really delivers for a window (it can differ from the window rectangle).
   *
   * @param hwnd Native window handle.
   * @param w Width in pixels, or 0 to forget the value.
   * @param h Height in pixels.
   */
  void set_actual_size(std::uint64_t hwnd, int w, int h);

  /**
   * @brief Window that keyboard and mouse input should go to. Set by the client through `/vrfocus`.
   *
   * @param hwnd Native window handle, or 0.
   */
  void set_focus_target(std::uint64_t hwnd);

  /** @brief Return the current focused VR window. */
  std::uint64_t focus_target();

  /**
   * @brief Bring the focus target to the front when composed capture is on. Called for every client input packet.
   */
  void apply_focus();
}  // namespace platf::vr_compose
