/**
 * @file src/platform/windows/vr_windows.h
 * @brief Declarations for reporting desktop windows and Start Menu apps to the VR Moonlight client.
 */
#pragma once

// standard includes
#include <filesystem>
#include <string>
#include <vector>

namespace platf::vr {
  /**
   * @brief One visible top-level window, normalized to the primary monitor.
   */
  struct window_info_t {
    long long id;  ///< Window handle (HWND), stable while the window lives.
    unsigned long pid;  ///< Owning process id: windows of one app share it.
    long long owner;  ///< Handle of the owner window (dialogs, popups), 0 for none.
    double x;  ///< Left edge, 0..1 of the primary monitor width.
    double y;  ///< Top edge, 0..1 of the primary monitor height.
    double w;  ///< Width, 0..1 of the primary monitor width.
    double h;  ///< Height, 0..1 of the primary monitor height.
    int z;  ///< Stacking order, 0 is front-most.
    std::string title;  ///< Window title in UTF-8 (debug aid).
  };

  /**
   * @brief Snapshot of all visible windows and the primary monitor size.
   */
  struct window_snapshot_t {
    int screen_w;  ///< Primary monitor width in physical pixels.
    int screen_h;  ///< Primary monitor height in physical pixels.
    std::vector<window_info_t> windows;  ///< Visible windows, front-most first.
  };

  /**
   * @brief One Start Menu shortcut.
   */
  struct app_entry_t {
    int id;  ///< Index in the most recent scan.
    std::string name;  ///< Display name in UTF-8.
    std::filesystem::path path;  ///< Full path of the .lnk file.
  };

  /**
   * @brief Enumerate visible, non-minimized, non-cloaked top-level windows.
   *
   * @return Snapshot with windows normalized to the primary monitor.
   */
  window_snapshot_t list_windows();

  /**
   * @brief Scan the Start Menu for shortcuts and remember the result for launch_app().
   *
   * @return Sorted list of apps. Ids are valid until the next scan.
   */
  std::vector<app_entry_t> scan_apps();

  /**
   * @brief Launch an app from the most recent scan_apps() result.
   *
   * @param id App id returned by scan_apps().
   * @param name_out Receives the app display name on success.
   * @return true when the shortcut was started.
   */
  bool launch_app(int id, std::string &name_out);
}  // namespace platf::vr
