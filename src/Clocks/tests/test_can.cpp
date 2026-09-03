#include "can.h"
#include "hal/impl/.mock/hal_mock.h"
#include "unity.h"

#include <cstring>

hal_can_t clocksTestGetCanHandle(void);

// Clocks can.cpp calls into logic.cpp (triggerDrawHighImportanceValue,
// updateCluster) for UI updates. Provide no-op stubs here so the test
// target doesn't need to pull in the display + cluster graph.
void triggerDrawHighImportanceValue(bool) {}
static unsigned int clusterUpdateCount = 0u;
void updateCluster(void) { clusterUpdateCount++; }

// watchdog_feed is declared in JaszczurHAL's multicoreWatchdog.h but its
// definition lives in a module we don't compile here; stub it out.
extern "C" void watchdog_feed(void) {}

static void ensure_can_ready(void) {
  if (clocksTestGetCanHandle() == NULL) {
    canInit();
  }
  TEST_ASSERT_NOT_NULL(clocksTestGetCanHandle());
  hal_mock_can_reset(clocksTestGetCanHandle());
}

void setUp(void) {
  ensure_can_ready();
  clusterUpdateCount = 0u;
  for (int i = 0; i < F_LAST; i++) {
    valueFields[i] = 0.0f;
  }
}

void tearDown(void) {}

// ── RX validation regression guards ──────────────────────────────────────────
// Mirror the ECU's post-d6b8ffc CAN hardening: NULL/oversized frames and
// per-case truncated frames must be rejected before the switch body
// dereferences any payload byte.

void test_truncated_ecu_update_01_frame_is_ignored(void) {
  valueFields[F_CALCULATED_ENGINE_LOAD] = 77.0f;
  valueFields[F_VOLTS] = 13.8f;

  // ECU_UPDATE_01 reads up to CAN_FRAME_ECU_UPDATE_OIL (index 5);
  // a 3-byte frame must be rejected without touching state.
  uint8_t shortFrame[3] = {0x00, 0x11, 0x22};
  hal_mock_can_inject(clocksTestGetCanHandle(), CAN_ID_ECU_UPDATE_01, 3,
                      shortFrame);
  canMainLoop();

  TEST_ASSERT_EQUAL_FLOAT(77.0f, valueFields[F_CALCULATED_ENGINE_LOAD]);
  TEST_ASSERT_EQUAL_FLOAT(13.8f, valueFields[F_VOLTS]);
}

void test_truncated_egt_frame_is_ignored(void) {
  valueFields[F_EGT] = 321.0f;
  valueFields[F_DPF_TEMP] = 654.0f;

  uint8_t shortFrame[3] = {0x00, 0x12, 0x34};
  hal_mock_can_inject(clocksTestGetCanHandle(), CAN_ID_EGT_UPDATE, 3,
                      shortFrame);
  canMainLoop();

  TEST_ASSERT_EQUAL_FLOAT(321.0f, valueFields[F_EGT]);
  TEST_ASSERT_EQUAL_FLOAT(654.0f, valueFields[F_DPF_TEMP]);
}

void test_full_ecu_update_02_frame_updates_state(void) {
  uint8_t frame[CAN_FRAME_MAX_LENGTH] = {0};
  frame[CAN_FRAME_ECU_UPDATE_INTAKE] = 45;
  frame[CAN_FRAME_ECU_UPDATE_FUEL_HI] = 0x10; // 4096
  frame[CAN_FRAME_ECU_UPDATE_FUEL_LO] = 0x00;
  frame[CAN_FRAME_ECU_UPDATE_GPS_AVAILABLE] = 1;
  frame[CAN_FRAME_ECU_UPDATE_VEHICLE_SPEED] = 60;

  hal_mock_can_inject(clocksTestGetCanHandle(), CAN_ID_ECU_UPDATE_02,
                      CAN_FRAME_MAX_LENGTH, frame);
  canMainLoop();

  TEST_ASSERT_EQUAL_INT(45, (int)valueFields[F_INTAKE_TEMP]);
  TEST_ASSERT_EQUAL_INT(4096, (int)valueFields[F_FUEL]);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, valueFields[F_GPS_IS_AVAILABLE]);
  TEST_ASSERT_EQUAL_FLOAT(60.0f, valueFields[F_GPS_CAR_SPEED]);
}

void test_rpm_frame_updates_cluster_in_same_can_drain(void) {
  uint8_t frame[CAN_FRAME_MAX_LENGTH] = {0};
  frame[CAN_FRAME_RPM_UPDATE_HI] = 0x09;
  frame[CAN_FRAME_RPM_UPDATE_LO] = 0xC4; // 2500 RPM

  hal_mock_can_inject(clocksTestGetCanHandle(), CAN_ID_RPM,
                      CAN_FRAME_MAX_LENGTH, frame);

  TEST_ASSERT_EQUAL_UINT(0u, clusterUpdateCount);
  canMainLoop();

  TEST_ASSERT_EQUAL_FLOAT(2500.0f, valueFields[F_RPM]);
  TEST_ASSERT_EQUAL_UINT(1u, clusterUpdateCount);
}

void test_non_fiesta_frame_is_rejected_by_acceptance_filters(void) {
  uint8_t frame[CAN_FRAME_MAX_LENGTH] = {0};

  hal_mock_can_inject(clocksTestGetCanHandle(), 0x555u, CAN_FRAME_MAX_LENGTH,
                      frame);

  TEST_ASSERT_FALSE(hal_can_available(clocksTestGetCanHandle()));
}

void test_unknown_can_id_does_not_update_state(void) {
  valueFields[F_INTAKE_TEMP] = 77.0f;
  valueFields[F_FUEL] = 1234.0f;
  uint8_t frame[CAN_FRAME_MAX_LENGTH] = {0xAA, 0xBB, 0xCC, 0xDD,
                                         0xEE, 0xFF, 0x11, 0x22};
  hal_mock_can_inject(clocksTestGetCanHandle(), 0x131u, CAN_FRAME_MAX_LENGTH,
                      frame);
  canMainLoop();
  TEST_ASSERT_EQUAL_FLOAT(77.0f, valueFields[F_INTAKE_TEMP]);
  TEST_ASSERT_EQUAL_FLOAT(1234.0f, valueFields[F_FUEL]);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_truncated_ecu_update_01_frame_is_ignored);
  RUN_TEST(test_truncated_egt_frame_is_ignored);
  RUN_TEST(test_full_ecu_update_02_frame_updates_state);
  RUN_TEST(test_rpm_frame_updates_cluster_in_same_can_drain);
  RUN_TEST(test_non_fiesta_frame_is_rejected_by_acceptance_filters);
  RUN_TEST(test_unknown_can_id_does_not_update_state);
  return UNITY_END();
}
