/**
 * @file src/window_policy.h
 * @brief Platform independent helpers used to decide which windows belong to a streamed app.
 */
#pragma once

// standard includes
#include <cstdint>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace window_policy {
  /**
   * @brief One edge of the process tree.
   */
  using process_edge_t = std::pair<std::uint32_t, std::uint32_t>;  ///< `first` is the child pid, `second` is its parent pid.

  /**
   * @brief Check whether a window title matches a user supplied pattern.
   *
   * @details The match is a case-insensitive substring test (ASCII case folding). An empty pattern never
   * matches, so callers can use an empty pattern to mean "match by process tree only".
   *
   * @param pattern Text that must appear in the title.
   * @param title Window title.
   * @return `true` when @p pattern is non-empty and appears in @p title.
   */
  bool title_matches(std::string_view pattern, std::string_view title);

  /**
   * @brief Collect a process and every process that descends from it.
   *
   * @param root Pid of the process Sunshine launched.
   * @param edges Snapshot of the process tree as (child, parent) pairs.
   * @return Set containing @p root and all of its descendants.
   */
  std::unordered_set<std::uint32_t> collect_descendants(std::uint32_t root, const std::vector<process_edge_t> &edges);
}  // namespace window_policy
