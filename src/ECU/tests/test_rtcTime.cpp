#include "config.h"
#include "hal/impl/.mock/hal_mock.h"
#include "rtcTime.h"
#include "unity.h"

static const uint32_t kUtcA = 1767268800u; // 2026-01-01 12:00:00 UTC
static const uint32_t kUtcB = 1767272400u; // an hour later

static unsigned s_hookCalls;

void setUp(void) {
  hal_mock_set_millis(1000u);
  rtcTimeResetForTest();
  s_hookCalls = 0u;
}

void tearDown(void) { rtcTimeReadHookForTest = NULL; }

void test_no_time_before_the_first_frame_or_without_integrity(void) {
  TEST_ASSERT_EQUAL_UINT32(0u, rtcTimeGetUtc());
  rtcTimeStore(kUtcA);
  TEST_ASSERT_EQUAL_UINT32(kUtcA, rtcTimeGetUtc());
  rtcTimeStore(0u); // a frame without trusted UTC
  TEST_ASSERT_EQUAL_UINT32(0u, rtcTimeGetUtc());
}

void test_time_advances_by_whole_seconds_and_expires(void) {
  rtcTimeStore(kUtcA);
  hal_mock_advance_millis(999u);
  TEST_ASSERT_EQUAL_UINT32(kUtcA, rtcTimeGetUtc());
  hal_mock_advance_millis(1u);
  TEST_ASSERT_EQUAL_UINT32(kUtcA + 1u, rtcTimeGetUtc());
  hal_mock_set_millis(1000u + RTC_TIME_MAX_AGE_MS - 1u);
  TEST_ASSERT_EQUAL_UINT32(kUtcA + ((RTC_TIME_MAX_AGE_MS - 1u) / 1000u),
                           rtcTimeGetUtc());
  hal_mock_set_millis(1000u + RTC_TIME_MAX_AGE_MS);
  TEST_ASSERT_EQUAL_UINT32(0u, rtcTimeGetUtc());
  rtcTimeStore(kUtcB); // the next frame brings it back
  TEST_ASSERT_EQUAL_UINT32(kUtcB, rtcTimeGetUtc());
}

void test_age_survives_the_millisecond_counter_wrap(void) {
  hal_mock_set_millis(0xFFFFFF00u);
  rtcTimeStore(kUtcA);
  hal_mock_advance_millis(0x200u + 1000u);
  TEST_ASSERT_EQUAL_UINT32(kUtcA + 1u, rtcTimeGetUtc());
}

void test_expire_forgets_a_stale_time_for_good(void) {
  rtcTimeStore(kUtcA);
  hal_mock_advance_millis(RTC_TIME_MAX_AGE_MS - 1u);
  rtcTimeExpire();
  TEST_ASSERT_NOT_EQUAL_UINT32(0u, rtcTimeGetUtc());
  hal_mock_advance_millis(1u);
  rtcTimeExpire();
  // The counter coming back to the reception time must not revive it.
  hal_mock_set_millis(1000u);
  TEST_ASSERT_EQUAL_UINT32(0u, rtcTimeGetUtc());
}

static void storeOnceBetweenTheWords(void) {
  if (s_hookCalls++ == 0u) {
    hal_mock_advance_millis(2000u);
    rtcTimeStore(kUtcB);
  }
}

/* A store between the two words of a read: the torn pair (A with the
 * reception time of B) is dropped and the next attempt returns B. */
void test_read_retries_after_a_concurrent_store(void) {
  rtcTimeStore(kUtcA);
  rtcTimeReadHookForTest = storeOnceBetweenTheWords;
  TEST_ASSERT_EQUAL_UINT32(kUtcB, rtcTimeGetUtc());
  TEST_ASSERT_EQUAL_UINT(2u, s_hookCalls);
}

static void storeEveryTime(void) {
  ++s_hookCalls;
  rtcTimeStore(kUtcB);
}

/* A writer active during every attempt: the reader gives up instead of
 * waiting for it. */
void test_read_gives_up_while_the_writer_keeps_storing(void) {
  rtcTimeStore(kUtcA);
  rtcTimeReadHookForTest = storeEveryTime;
  TEST_ASSERT_EQUAL_UINT32(0u, rtcTimeGetUtc());
  TEST_ASSERT_GREATER_THAN_UINT(1u, s_hookCalls);
  rtcTimeReadHookForTest = NULL;
  TEST_ASSERT_EQUAL_UINT32(kUtcB, rtcTimeGetUtc());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_no_time_before_the_first_frame_or_without_integrity);
  RUN_TEST(test_time_advances_by_whole_seconds_and_expires);
  RUN_TEST(test_age_survives_the_millisecond_counter_wrap);
  RUN_TEST(test_expire_forgets_a_stale_time_for_good);
  RUN_TEST(test_read_retries_after_a_concurrent_store);
  RUN_TEST(test_read_gives_up_while_the_writer_keeps_storing);
  return UNITY_END();
}
