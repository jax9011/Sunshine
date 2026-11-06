/**
 * @file src/platform/windows/window_host.h
 * @brief Declarations for tracking the window of an app that is streamed on its own (Windows).
 */
#pragma once

// standard includes
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace platf::window_host {
  /**
   * @brief Settings for one tracked app.
   */
  struct options_t {
    std::uint32_t root_pid;  ///< Pid of the process Sunshine launched.
    std::string title_match;  ///< Optional case-insensitive title fragment for apps that hand off to another process.
    std::chrono::seconds adopt_timeout;  ///< How long to wait for the app window before logging that none was found.
  };

  /**
   * @brief Bring a window to the front so keyboard and mouse input reaches it.
   *
   * @details Window capture is unaffected by other windows, but injected input goes to whatever is on top at the
   * cursor position, so the streamed window is raised before input is delivered to it.
   *
   * @param window Native window handle returned by watcher_t::window().
   */
  void focus_window(std::uint64_t window);

  /**
   * @brief Watches one app and reports its main window. One watcher exists per streamed app.
   */
  class watcher_t {
  public:
    watcher_t() = default;
    watcher_t(const watcher_t &) = delete;
    watcher_t &operator=(const watcher_t &) = delete;
    ~watcher_t();

    /**
     * @brief Start looking for the app's window. Replaces a previous run of this watcher.
     *
     * @param options Tracking settings.
     */
    void start(const options_t &options);

    /**
     * @brief Stop the watcher thread.
     */
    void stop();

    /**
     * @brief Get the app's current main window.
     *
     * @return Native window handle, or 0 while none has been found.
     */
    std::uint64_t window() const;

    /**
     * @brief Report whether a window was found and has since been closed.
     *
     * @return `true` once the window is gone and no successor appeared within the grace period.
     */
    bool window_closed() const;

    /**
     * @brief Wait until a window has been found.
     *
     * @param timeout Longest time to wait.
     * @return The window handle, or 0 if none appeared in time.
     */
    std::uint64_t wait_for_window(std::chrono::seconds timeout) const;

  private:
    std::thread thread;  ///< Watcher thread.
    std::mutex wake_mutex;  ///< Guards stop_requested.
    std::condition_variable wake;  ///< Wakes the thread early when stopping.
    bool stop_requested = false;  ///< Set by stop().
    std::atomic<std::uint64_t> hwnd {0};  ///< Adopted window.
    std::atomic<bool> closed {false};  ///< Set once the adopted window is gone for good.
  };
}  // namespace platf::window_host
