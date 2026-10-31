#include "../common/canDefinitions/canDefinitions.h"
#include "hal/impl/.mock/hal_mock.h"
#include "timingDiag.h"
#include "unity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The ECU frame loss counter of the T0 report, fed with frame numbers. The
// tests run in order and go on with the numbers of the one before.

static uint32_t s_nowMs = 1000u;

static void ecuFrame(uint8_t number) {
  uint8_t buf[CAN_FRAME_MAX_LENGTH] = {number};
  timingDiagCanFrame(CAN_ID_RPM, buf, CAN_FRAME_MAX_LENGTH);
}

static long field(const char *line, const char *name) {
  const char *at = strstr(line, name);
  TEST_ASSERT_NOT_NULL(at);
  return strtol(at + strlen(name), NULL, 10);
}

static long s_late = 0;    // ecu_late of the last report
static long s_win = 0;     // its index
static char s_lateWin[64]; // its ecu_late_win

// Close the window of the CAN core; returns its ecu_lost.
static long reportLost(void) {
  s_nowMs += TIMING_DIAG_REPORT_MS;
  hal_mock_set_millis(s_nowMs);
  char previous[1024];
  snprintf(previous, sizeof(previous), "%s", hal_mock_serial_last_line());
  timingDiagLoopEnd(0u, timingDiagLoopStart());
  // The receive core only publishes. Formatting and serial I/O belong to
  // the display core, including when both windows end on the same tick.
  TEST_ASSERT_EQUAL_STRING(previous, hal_mock_serial_last_line());
  timingDiagLoopEnd(1u, timingDiagLoopStart());
  const char *line = hal_mock_serial_last_line();
  s_late = field(line, "ecu_late:");
  s_win = field(line, " win:");
  const char *lateWin = strstr(line, "ecu_late_win:");
  TEST_ASSERT_NOT_NULL(lateWin);
  lateWin += strlen("ecu_late_win:");
  const size_t length = strcspn(lateWin, " ");
  TEST_ASSERT_TRUE(length < sizeof(s_lateWin));
  memcpy(s_lateWin, lateWin, length);
  s_lateWin[length] = '\0';
  return field(line, "ecu_lost:");
}

void setUp(void) {}

void tearDown(void) {}

void test_frames_lost_between_two_windows_are_counted(void) {
  (void)reportLost(); // start from a closed window
  ecuFrame(10u);
  ecuFrame(11u);
  TEST_ASSERT_EQUAL_INT32(0, reportLost());
  ecuFrame(13u);
  ecuFrame(14u);
  TEST_ASSERT_EQUAL_INT32(1, reportLost());
}

void test_frames_lost_inside_a_window_are_counted(void) {
  ecuFrame(15u);
  ecuFrame(18u);
  TEST_ASSERT_EQUAL_INT32(2, reportLost());
}

void test_reordered_frames_are_no_loss(void) {
  ecuFrame(20u);
  ecuFrame(19u);
  ecuFrame(21u);
  TEST_ASSERT_EQUAL_INT32(0, reportLost());
}

void test_a_late_frame_takes_back_its_loss_in_the_next_window(void) {
  ecuFrame(22u);
  ecuFrame(24u);
  long lost = reportLost();
  long late = s_late;
  TEST_ASSERT_EQUAL_INT32(1, lost);
  ecuFrame(23u);
  ecuFrame(25u);
  lost += reportLost();
  late += s_late;
  // Every frame came: the run has lost none.
  TEST_ASSERT_EQUAL_INT32(0, lost - late);
}

void test_a_window_of_late_frames_only_loses_nothing(void) {
  ecuFrame(26u);
  ecuFrame(28u);
  TEST_ASSERT_EQUAL_INT32(1, reportLost());
  ecuFrame(27u);
  TEST_ASSERT_EQUAL_INT32(0, reportLost());
  TEST_ASSERT_EQUAL_INT32(1, s_late);
  ecuFrame(29u);
  TEST_ASSERT_EQUAL_INT32(0, reportLost());
  TEST_ASSERT_EQUAL_INT32(0, s_late);
}

// A frame late by two windows is given back to the window that counted it.
void test_a_late_frame_names_the_report_that_counted_it(void) {
  ecuFrame(30u);
  ecuFrame(32u); // 31 missing
  TEST_ASSERT_EQUAL_INT32(1, reportLost());
  const long first = s_win;
  ecuFrame(33u);
  ecuFrame(35u); // 34 missing
  TEST_ASSERT_EQUAL_INT32(1, reportLost());
  ecuFrame(31u); // late, from the first window
  ecuFrame(36u);
  TEST_ASSERT_EQUAL_INT32(0, reportLost());
  TEST_ASSERT_EQUAL_INT32(1, s_late);
  char expected[24];
  snprintf(expected, sizeof(expected), "%ldx1", first);
  TEST_ASSERT_EQUAL_STRING(expected, s_lateWin);
  TEST_ASSERT_EQUAL_STRING("-", (reportLost(), s_lateWin));
}

void test_numbers_wrap_after_255(void) {
  for (unsigned number = 37u; number <= 255u; number++) {
    ecuFrame((uint8_t)number);
  }
  ecuFrame(1u); // 0 lost on the wrap
  TEST_ASSERT_EQUAL_INT32(1, reportLost());
}

void test_a_window_without_ecu_frames_loses_nothing(void) {
  TEST_ASSERT_EQUAL_INT32(0, reportLost());
  ecuFrame(2u);
  TEST_ASSERT_EQUAL_INT32(0, reportLost());
}

void test_pending_report_preserves_the_next_collecting_window(void) {
  ecuFrame(3u);
  s_nowMs += TIMING_DIAG_REPORT_MS;
  hal_mock_set_millis(s_nowMs);
  timingDiagLoopEnd(0u, timingDiagLoopStart());
  ecuFrame(4u);
  s_nowMs += TIMING_DIAG_REPORT_MS;
  hal_mock_set_millis(s_nowMs);
  timingDiagLoopEnd(0u, timingDiagLoopStart());
  timingDiagLoopEnd(1u, timingDiagLoopStart());
  TEST_ASSERT_EQUAL_INT32(1, field(hal_mock_serial_last_line(), "ecu_n:"));
  ecuFrame(5u);
  TEST_ASSERT_EQUAL_INT32(0, reportLost());
  TEST_ASSERT_EQUAL_INT32(2, field(hal_mock_serial_last_line(), "ecu_n:"));
  TEST_ASSERT_EQUAL_INT32(10000,
                          field(hal_mock_serial_last_line(), "window_ms:"));
}

void test_report_counts_each_ecu_id_without_counting_unusable_frames(void) {
  (void)reportLost();
  const uint32_t ids[] = {CAN_ID_ECU_UPDATE_01, CAN_ID_ECU_UPDATE_02,
                          CAN_ID_ECU_UPDATE_03, CAN_ID_RPM,
                          CAN_ID_THROTTLE,      CAN_ID_TURBO_PRESSURE};
  uint8_t payload[CAN_FRAME_MAX_LENGTH] = {0};
  for (size_t i = 0u; i < COUNTOF(ids); ++i) {
    payload[0] = (uint8_t)(10u + i);
    timingDiagCanFrame(ids[i], payload, CAN_FRAME_MAX_LENGTH);
  }
  payload[0] = 16u;
  timingDiagCanFrame(CAN_ID_ECU_UPDATE_01, payload, CAN_FRAME_MAX_LENGTH);
  timingDiagCanFrame(CAN_ID_RPM, NULL, CAN_FRAME_MAX_LENGTH);
  timingDiagCanFrame(CAN_ID_RPM, payload, 0u);
  timingDiagCanFrame(0x7FFu, payload, CAN_FRAME_MAX_LENGTH);
  (void)reportLost();
  TEST_ASSERT_NOT_NULL(
      strstr(hal_mock_serial_last_line(), "ecu_ids:2,1,1,1,1,1 "));
  TEST_ASSERT_EQUAL_INT32(7, field(hal_mock_serial_last_line(), "ecu_n:"));
}

int main(void) {
  hal_mock_set_millis(s_nowMs);
  UNITY_BEGIN();
  RUN_TEST(test_frames_lost_between_two_windows_are_counted);
  RUN_TEST(test_frames_lost_inside_a_window_are_counted);
  RUN_TEST(test_reordered_frames_are_no_loss);
  RUN_TEST(test_a_late_frame_takes_back_its_loss_in_the_next_window);
  RUN_TEST(test_a_window_of_late_frames_only_loses_nothing);
  RUN_TEST(test_a_late_frame_names_the_report_that_counted_it);
  RUN_TEST(test_numbers_wrap_after_255);
  RUN_TEST(test_a_window_without_ecu_frames_loses_nothing);
  RUN_TEST(test_pending_report_preserves_the_next_collecting_window);
  RUN_TEST(test_report_counts_each_ecu_id_without_counting_unusable_frames);
  return UNITY_END();
}
