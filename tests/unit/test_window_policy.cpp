/**
 * @file tests/unit/test_window_policy.cpp
 * @brief Test src/window_policy.* functions.
 */
// test includes
#include "../tests_common.h"

// local includes
#include <src/window_policy.h>

TEST(WindowPolicyTitleMatches, EmptyPatternNeverMatches) {
  EXPECT_FALSE(window_policy::title_matches("", "Anything"));
}

TEST(WindowPolicyTitleMatches, IsCaseInsensitiveSubstring) {
  EXPECT_TRUE(window_policy::title_matches("notepad", "Untitled - Notepad"));
  EXPECT_FALSE(window_policy::title_matches("calc", "Untitled - Notepad"));
}

TEST(WindowPolicyCollectDescendants, ContainsRootWhenNoEdges) {
  const auto family = window_policy::collect_descendants(10, {});
  EXPECT_EQ(family.size(), 1U);
  EXPECT_TRUE(family.count(10) != 0);
}

TEST(WindowPolicyCollectDescendants, FollowsChildrenListedBeforeParents) {
  // 30 -> 20 -> 10, with the deepest edge listed first; 99 is unrelated.
  const auto family = window_policy::collect_descendants(10, {{30, 20}, {20, 10}, {99, 1}});
  EXPECT_TRUE(family.count(10) != 0);
  EXPECT_TRUE(family.count(20) != 0);
  EXPECT_TRUE(family.count(30) != 0);
  EXPECT_FALSE(family.count(99) != 0);
}
