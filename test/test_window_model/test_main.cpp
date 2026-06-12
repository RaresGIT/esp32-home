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

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_estimate_opening_half);
  RUN_TEST(test_estimate_closing_quarter);
  RUN_TEST(test_estimate_clamps_high);
  RUN_TEST(test_estimate_clamps_low);
  RUN_TEST(test_estimate_idle_unchanged);
  return UNITY_END();
}
