/**
 * @file src/window_policy.cpp
 * @brief Definitions for platform independent window ownership helpers.
 */
// standard includes
#include <algorithm>
#include <cctype>
#include <string>

// local includes
#include "src/window_policy.h"

namespace window_policy {
  namespace {
    /**
     * @brief Fold a string to lower case using ASCII rules.
     *
     * @param text Text to fold.
     * @return Lower-cased copy.
     */
    std::string fold(std::string_view text) {
      std::string out {text};
      std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
      });
      return out;
    }
  }  // namespace

  bool title_matches(std::string_view pattern, std::string_view title) {
    if (pattern.empty()) {
      return false;
    }
    return fold(title).find(fold(pattern)) != std::string::npos;
  }

  std::unordered_set<std::uint32_t> collect_descendants(std::uint32_t root, const std::vector<process_edge_t> &edges) {
    std::unordered_set<std::uint32_t> family {root};

    // Parents can be listed after their children, so keep sweeping until nothing new is added.
    bool grew = true;
    while (grew) {
      grew = false;
      for (const auto &[child, parent] : edges) {
        if (family.count(parent) != 0 && family.insert(child).second) {
          grew = true;
        }
      }
    }
    return family;
  }
}  // namespace window_policy
