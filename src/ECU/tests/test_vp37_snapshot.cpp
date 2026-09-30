// The published snapshot crosses cores without a lock: the control core never
// waits for a reader, and a copy that returns HAL_OK is never torn.

#include "testable/vp37_testable.h"
#include "unity.h"
#include "vp37_internal.h"

#include <atomic>
#include <chrono>
#include <string.h>
#include <thread>

static VP37Pump s_pump;

#ifdef VP37_TEST_DELAY_HOOK
/* What the control core does while a reader sleeps in its poll delay: one
 * whole publication in the n-th delay, or one counter step per delay, which
 * shows the reader a publication that starts and later ends. */
static int s_publishInDelay = -1;
static int s_counterStepsInDelays = 0;

extern "C" void __real_hal_delay_us(uint32_t us);
extern "C" void __wrap_hal_delay_us(uint32_t us) {
  __real_hal_delay_us(us);
  if ((s_publishInDelay > 0) && (--s_publishInDelay == 0)) {
    VP37_publish(&s_pump);
  }
  if (s_counterStepsInDelays > 0) {
    s_counterStepsInDelays--;
    s_pump.publishedSequence++;
  }
}
#endif

static void noQuantity(int32_t command) { (void)command; }
static void noTiming(int32_t command) { (void)command; }
static void noEnable(bool enabled) { (void)enabled; }
static bool notEnabled(void) { return false; }
static hal_status_t noAdjustometer(uint8_t reg, uint8_t *data, size_t len) {
  (void)reg;
  (void)data;
  (void)len;
  return HAL_EBUS;
}

/** Write one value into fields at the start, middle and end of the snapshot. */
static void markPump(uint32_t value) {
  s_pump.feedback.readCount = value;
  s_pump.controlSequence = value;
  s_pump.scan.blocks = value;
  s_pump.currentControl.history[VP37_CURRENT_CONTROL_HISTORY - 1U].writtenUs =
      value;
  s_pump.output.finalPWM = (int32_t)value;
}

static bool snapshotMarked(const VP37Snapshot *snapshot, uint32_t *value) {
  const VP37Telemetry *view = &snapshot->telemetry;
  *value = snapshot->status.readCount;
  return (view->controlSequence == *value) && (view->scan.blocks == *value) &&
         (view->currentControl.history[VP37_CURRENT_CONTROL_HISTORY - 1U]
              .writtenUs == *value) &&
         (view->output.finalPWM == (int32_t)*value);
}

void setUp(void) {
#ifdef VP37_TEST_DELAY_HOOK
  s_publishInDelay = -1;
  s_counterStepsInDelays = 0;
#endif
  memset(&s_pump, 0, sizeof(s_pump));
  VP37Callbacks table = {};
  table.writeQuantityPwm = noQuantity;
  table.writeTimingPwm = noTiming;
  table.setDriveEnabled = noEnable;
  table.driveEnabled = notEnabled;
  table.adjustometerTransfer = noAdjustometer;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_setCallbacks(&s_pump, &table));
}

void tearDown(void) {}

void test_publication_does_not_wait_for_a_copy_in_progress(void) {
  markPump(1U);
  VP37_publish(&s_pump);
  // Core 0 has started a copy when core 1 ends three control steps.
  const uint32_t sequence = VP37_snapshotBegin(&s_pump);
  std::atomic<bool> published{false};
  std::thread controlCore([&published] {
    for (uint32_t step = 2U; step <= 4U; step++) {
      markPump(step);
      VP37_publish(&s_pump);
    }
    published = true;
  });
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (!published && (std::chrono::steady_clock::now() < deadline)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  const bool waited = !published;
  // Ending the copy releases a control core that did wait, so it can be joined.
  const bool accepted = VP37_snapshotEnd(&s_pump, sequence);
  controlCore.join();
  TEST_ASSERT_FALSE_MESSAGE(waited, "the control core waited for a reader");

  // The copy that spanned those steps is refused, the next one is whole.
  TEST_ASSERT_FALSE(accepted);
  static VP37Snapshot snapshot;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_readSnapshot(&s_pump, &snapshot));
  uint32_t value = 0U;
  TEST_ASSERT_TRUE(snapshotMarked(&snapshot, &value));
  TEST_ASSERT_EQUAL_UINT32(4U, value);
  VP37Status status;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_readStatus(&s_pump, &status));
  TEST_ASSERT_EQUAL_UINT32(4U, status.readCount);
}

#ifndef VP37_SNAPSHOT_TEST_STEPS
#define VP37_SNAPSHOT_TEST_STEPS 200000U
#endif

void test_copies_interleaved_with_publications_are_never_torn(void) {
  const uint32_t steps = VP37_SNAPSHOT_TEST_STEPS;
  // Published once before the race, so every copy has something to take.
  markPump(0U);
  VP37_publish(&s_pump);
  std::atomic<bool> running{true};
  std::thread controlCore([&running, steps] {
    for (uint32_t step = 1U; step <= steps; step++) {
      markPump(step);
      VP37_publish(&s_pump);
    }
    running = false;
  });
  static VP37Snapshot snapshot;
  uint32_t copies = 0U;
  uint32_t torn = 0U;
  uint32_t last = 0U;
  bool ordered = true;
  // Yielding lets a serializing scheduler (valgrind) interleave the threads;
  // the loop ends with at least one copy either way.
  while (running || (copies == 0U)) {
    if (VP37_readSnapshot(&s_pump, &snapshot) == HAL_OK) {
      uint32_t value = 0U;
      if (snapshotMarked(&snapshot, &value)) {
        ordered = ordered && (value >= last);
        last = value;
        copies++;
      } else {
        torn++;
      }
    }
    std::this_thread::yield();
  }
  controlCore.join();
  TEST_ASSERT_EQUAL_UINT32(0U, torn);
  TEST_ASSERT_TRUE(ordered);
  TEST_ASSERT_GREATER_THAN_UINT32(0U, copies);
}

void test_copy_gives_up_while_a_publication_stays_open(void) {
  VP37Status status;
  static VP37Snapshot snapshot;
  // Nothing to copy before the first publication.
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, VP37_readStatus(&s_pump, &status));
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, VP37_readSnapshot(&s_pump, &snapshot));
  markPump(7U);
  VP37_publish(&s_pump);
  // A control core halted inside VP37_publish() leaves the sequence odd.
  s_pump.publishedSequence++;
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, VP37_readStatus(&s_pump, &status));
  s_pump.publishedSequence++;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_readStatus(&s_pump, &status));
  TEST_ASSERT_EQUAL_UINT32(7U, status.readCount);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, VP37_readStatus(NULL, &status));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, VP37_readStatus(&s_pump, NULL));
}

void test_status_reads_back_exactly_what_the_step_published(void) {
  s_pump.vp37Initialized = true;
  s_pump.feedback.readCount = 123456U;
  s_pump.feedback.fuelTempC = -12.625f;
  s_pump.feedback.supplyVolts = 13.7f;
  VP37_publish(&s_pump);
  VP37Status status;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_readStatus(&s_pump, &status));
  TEST_ASSERT_TRUE(status.initialized);
  TEST_ASSERT_EQUAL_UINT32(123456U, status.readCount);
  TEST_ASSERT_TRUE(status.fuelTempC == -12.625f);
  TEST_ASSERT_TRUE(status.supplyVolts == 13.7f);
  static VP37Snapshot snapshot;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_readSnapshot(&s_pump, &snapshot));
  TEST_ASSERT_TRUE(snapshot.status.supplyVolts == 13.7f);
  TEST_ASSERT_TRUE(snapshot.telemetry.feedback.fuelTempC == -12.625f);

  s_pump.vp37Initialized = false;
  VP37_publish(&s_pump);
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_readStatus(&s_pump, &status));
  TEST_ASSERT_FALSE(status.initialized);
}

void test_sequence_wrap_never_reads_as_unpublished(void) {
  s_pump.publishedSequence = UINT32_MAX - 1U;
  markPump(9U);
  VP37_publish(&s_pump);
  TEST_ASSERT_NOT_EQUAL_UINT32(0U, s_pump.publishedSequence);
  TEST_ASSERT_EQUAL_UINT32(0U, s_pump.publishedSequence & 1U);
  VP37Status status;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_readStatus(&s_pump, &status));
  TEST_ASSERT_EQUAL_UINT32(9U, status.readCount);
}

#ifdef VP37_TEST_DELAY_HOOK
void test_wait_returns_when_the_next_publication_ends(void) {
  markPump(1U);
  VP37_publish(&s_pump);
  s_publishInDelay = 3;
  const uint32_t startedUs = hal_micros();
  TEST_ASSERT_TRUE(VP37_waitForPublication(&s_pump, 10000U));
  TEST_ASSERT_EQUAL_UINT32(3U * VP37_PUBLICATION_POLL_US,
                           hal_micros() - startedUs);
}

void test_wait_ends_with_a_publication_not_at_its_start(void) {
  markPump(1U);
  VP37_publish(&s_pump);
  // A publication starts in the first delay and ends in the second.
  s_counterStepsInDelays = 2;
  uint32_t startedUs = hal_micros();
  TEST_ASSERT_TRUE(VP37_waitForPublication(&s_pump, 10000U));
  TEST_ASSERT_EQUAL_UINT32(2U * VP37_PUBLICATION_POLL_US,
                           hal_micros() - startedUs);
  // Waiting from inside a publication, its end is the one that counts.
  s_pump.publishedSequence++;
  s_counterStepsInDelays = 1;
  startedUs = hal_micros();
  TEST_ASSERT_TRUE(VP37_waitForPublication(&s_pump, 10000U));
  TEST_ASSERT_EQUAL_UINT32(VP37_PUBLICATION_POLL_US, hal_micros() - startedUs);
}

void test_wait_gives_up_on_a_pump_that_does_not_publish(void) {
  markPump(1U);
  VP37_publish(&s_pump);
  const uint32_t startedUs = hal_micros();
  TEST_ASSERT_FALSE(VP37_waitForPublication(&s_pump, 1000U));
  const uint32_t waitedUs = hal_micros() - startedUs;
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1000U, waitedUs);
  TEST_ASSERT_LESS_THAN_UINT32(1000U + VP37_PUBLICATION_POLL_US + 1U, waitedUs);
  TEST_ASSERT_FALSE(VP37_waitForPublication(NULL, 1000U));
}
#endif

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_publication_does_not_wait_for_a_copy_in_progress);
  RUN_TEST(test_copies_interleaved_with_publications_are_never_torn);
  RUN_TEST(test_copy_gives_up_while_a_publication_stays_open);
  RUN_TEST(test_status_reads_back_exactly_what_the_step_published);
  RUN_TEST(test_sequence_wrap_never_reads_as_unpublished);
#ifdef VP37_TEST_DELAY_HOOK
  RUN_TEST(test_wait_returns_when_the_next_publication_ends);
  RUN_TEST(test_wait_ends_with_a_publication_not_at_its_start);
  RUN_TEST(test_wait_gives_up_on_a_pump_that_does_not_publish);
#endif
  return UNITY_END();
}
