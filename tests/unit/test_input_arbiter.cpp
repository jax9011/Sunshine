/**
 * @file tests/unit/test_input_arbiter.cpp
 * @brief Test src/input_arbiter.* functions.
 */
// test includes
#include "../tests_common.h"

// local includes
#include <src/config.h>
#include <src/input_arbiter.h>

TEST(InputArbiterShouldBlockLocal, AllowPolicyNeverBlocks) {
  EXPECT_FALSE(input::arbiter::should_block_local(input::arbiter::POLICY_ALLOW, 1000, 990, 1500));
}

TEST(InputArbiterShouldBlockLocal, NoRemoteInputYetDoesNotBlock) {
  EXPECT_FALSE(input::arbiter::should_block_local(input::arbiter::POLICY_REMOTE_PRIORITY, 1000, -1, 1500));
}

TEST(InputArbiterShouldBlockLocal, BlocksInsideGraceAndReleasesAfter) {
  EXPECT_TRUE(input::arbiter::should_block_local(input::arbiter::POLICY_REMOTE_PRIORITY, 2000, 1000, 1500));
  EXPECT_TRUE(input::arbiter::should_block_local(input::arbiter::POLICY_REMOTE_PRIORITY, 2500, 1000, 1500));
  EXPECT_FALSE(input::arbiter::should_block_local(input::arbiter::POLICY_REMOTE_PRIORITY, 2501, 1000, 1500));
}

TEST(InputArbiterNoteRemoteActivity, DefaultPolicyLeavesMouseUnblocked) {
  config::input.local_mouse_policy = "allow";
  input::arbiter::note_remote_activity();
  EXPECT_FALSE(input::arbiter::local_mouse_blocked());
}
