#include <unity.h>
#include "state/WindowModel.h"

using namespace WindowModel;

void test_estimate_opening_half(void) {
  // Opening from 0 for half of openMs (20000) -> 50%
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 50.0f, estimatePct(0, 10000, Motion::Opening, 20000, 27000));
}
void test_estimate_closing_quarter(void) {
  // Closing from 100 for a quarter of closeMs (20000) -> 75%
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 75.0f, estimatePct(100, 5000, Motion::Closing, 23500, 20000));
}
void test_estimate_clamps_high(void) {
  TEST_ASSERT_EQUAL_FLOAT(100.0f, estimatePct(50, 999999, Motion::Opening, 20000, 27000));
}
void test_estimate_clamps_low(void) {
  TEST_ASSERT_EQUAL_FLOAT(0.0f, estimatePct(50, 999999, Motion::Closing, 23500, 27000));
}
void test_estimate_idle_unchanged(void) {
  TEST_ASSERT_EQUAL_FLOAT(42.0f, estimatePct(42, 5000, Motion::Idle, 20000, 27000));
}

// lead/deadband chosen for clarity; settleOk=true unless a test needs the guard.
void test_idle_starts_open_toward_higher_target(void) {
  Step s = decideStep(0, 100, Motion::Idle, 3.0f, 1.5f, true);
  TEST_ASSERT_EQUAL(Command::SendOpen, s.command);
  TEST_ASSERT_EQUAL(Motion::Opening, s.motion);
}
void test_idle_starts_close_toward_lower_target(void) {
  Step s = decideStep(80, 30, Motion::Idle, 3.0f, 1.5f, true);
  TEST_ASSERT_EQUAL(Command::SendClose, s.command);
  TEST_ASSERT_EQUAL(Motion::Closing, s.motion);
}
void test_idle_at_target_is_done(void) {
  Step s = decideStep(50, 50, Motion::Idle, 3.0f, 1.5f, true);
  TEST_ASSERT_EQUAL(Command::None, s.command);
  TEST_ASSERT_TRUE(s.reachedTarget);
}
void test_idle_waits_when_not_settled(void) {
  Step s = decideStep(80, 30, Motion::Idle, 3.0f, 1.5f, false);
  TEST_ASSERT_EQUAL(Command::None, s.command);
  TEST_ASSERT_EQUAL(Motion::Idle, s.motion);
  TEST_ASSERT_FALSE(s.reachedTarget);
}
void test_opening_full_keeps_going_until_limit(void) {
  Step s = decideStep(60, 100, Motion::Opening, 3.0f, 1.5f, true);
  TEST_ASSERT_EQUAL(Command::None, s.command);
  TEST_ASSERT_EQUAL(Motion::Opening, s.motion);
}
void test_opening_full_done_at_limit(void) {
  Step s = decideStep(100, 100, Motion::Opening, 3.0f, 1.5f, true);
  TEST_ASSERT_EQUAL(Command::None, s.command);
  TEST_ASSERT_EQUAL(Motion::Idle, s.motion);
  TEST_ASSERT_TRUE(s.reachedTarget);
}
void test_opening_stops_at_intermediate_with_lead(void) {
  // target 50, lead 3 -> stop once pos >= 47
  Step below = decideStep(46, 50, Motion::Opening, 3.0f, 1.5f, true);
  TEST_ASSERT_EQUAL(Command::None, below.command);
  Step at = decideStep(47, 50, Motion::Opening, 3.0f, 1.5f, true);
  TEST_ASSERT_EQUAL(Command::SendClose, at.command);   // close-cmd stops an opening window
  TEST_ASSERT_EQUAL(Motion::Idle, at.motion);
  TEST_ASSERT_TRUE(at.reachedTarget);
}
void test_closing_stops_at_intermediate_with_lead(void) {
  // target 50, lead 3 -> stop once pos <= 53
  Step above = decideStep(54, 50, Motion::Closing, 3.0f, 1.5f, true);
  TEST_ASSERT_EQUAL(Command::None, above.command);
  Step at = decideStep(53, 50, Motion::Closing, 3.0f, 1.5f, true);
  TEST_ASSERT_EQUAL(Command::SendOpen, at.command);    // open-cmd stops a closing window
  TEST_ASSERT_EQUAL(Motion::Idle, at.motion);
  TEST_ASSERT_TRUE(at.reachedTarget);
}
void test_opening_retarget_below_triggers_stop(void) {
  // moving open at 70 but target dropped to 30 -> must stop now
  Step s = decideStep(70, 30, Motion::Opening, 3.0f, 1.5f, true);
  TEST_ASSERT_EQUAL(Command::SendClose, s.command);
  TEST_ASSERT_EQUAL(Motion::Idle, s.motion);
  TEST_ASSERT_FALSE(s.reachedTarget);
}

void test_closing_retarget_above_triggers_stop(void) {
  // closing at 20 but target jumped to 70 (above) -> stop now, not reached
  Step s = decideStep(20, 70, Motion::Closing, 3.0f, 1.5f, true);
  TEST_ASSERT_EQUAL(Command::SendOpen, s.command);
  TEST_ASSERT_EQUAL(Motion::Idle, s.motion);
  TEST_ASSERT_FALSE(s.reachedTarget);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_estimate_opening_half);
  RUN_TEST(test_estimate_closing_quarter);
  RUN_TEST(test_estimate_clamps_high);
  RUN_TEST(test_estimate_clamps_low);
  RUN_TEST(test_estimate_idle_unchanged);
  RUN_TEST(test_idle_starts_open_toward_higher_target);
  RUN_TEST(test_idle_starts_close_toward_lower_target);
  RUN_TEST(test_idle_at_target_is_done);
  RUN_TEST(test_idle_waits_when_not_settled);
  RUN_TEST(test_opening_full_keeps_going_until_limit);
  RUN_TEST(test_opening_full_done_at_limit);
  RUN_TEST(test_opening_stops_at_intermediate_with_lead);
  RUN_TEST(test_closing_stops_at_intermediate_with_lead);
  RUN_TEST(test_opening_retarget_below_triggers_stop);
  RUN_TEST(test_closing_retarget_above_triggers_stop);
  return UNITY_END();
}
