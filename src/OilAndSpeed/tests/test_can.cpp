#include "unity.h"
#include "can.h"
#include "peripherials.h"
#include "hal/impl/.mock/hal_mock.h"

#include <cstring>

hal_can_t oilspeedTestGetCanHandle(void);

static float s_globalValues[F_LAST];

void setGlobalValue(int idx, float val) {
  if ((idx >= 0) && (idx < F_LAST)) {
    s_globalValues[idx] = val;
  }
}

float getGlobalValue(int idx) {
  if ((idx >= 0) && (idx < F_LAST)) {
    return s_globalValues[idx];
  }
  return 0.0f;
}

void setLEDColor(int) {}
extern "C" void watchdog_feed(void) {}
// tools.cpp pulls in getAverageValueFrom which references hal_adc_read;
// provide a no-op stub so the symbol resolves without pulling hal_adc mock.
extern "C" int hal_adc_read(uint8_t) { return 0; }

static void ensure_can_ready(void) {
  if (oilspeedTestGetCanHandle() == NULL) {
    canInit();
  }
  TEST_ASSERT_NOT_NULL(oilspeedTestGetCanHandle());
  hal_mock_can_reset(oilspeedTestGetCanHandle());
}

void setUp(void) {
  std::memset(s_globalValues, 0, sizeof(s_globalValues));
  ensure_can_ready();
}

void tearDown(void) {}

// ── RX validation regression guards ──────────────────────────────────────────
// These mirror the ECU's post-d6b8ffc CAN hardening: NULL/oversized frames
// and per-case truncated frames must be rejected before the switch-body
// dereferences any payload byte.

void test_truncated_ecu_update_02_frame_is_ignored(void) {
  setGlobalValue(F_INTAKE_TEMP, 55.0f);
  setGlobalValue(F_FUEL, 4321.0f);
  setGlobalValue(F_GPS_IS_AVAILABLE, 1.0f);
  setGlobalValue(F_GPS_CAR_SPEED, 88.0f);

  // ECU_UPDATE_02 reads up to CAN_FRAME_ECU_UPDATE_VEHICLE_SPEED (index 5);
  // a 3-byte frame must be rejected without touching state.
  uint8_t shortFrame[3] = {0x00, 0x11, 0x22};
  hal_mock_can_inject(oilspeedTestGetCanHandle(),
                      CAN_ID_ECU_UPDATE_02, 3, shortFrame);
  canMainLoop();

  TEST_ASSERT_EQUAL_FLOAT(55.0f,   getGlobalValue(F_INTAKE_TEMP));
  TEST_ASSERT_EQUAL_FLOAT(4321.0f, getGlobalValue(F_FUEL));
  TEST_ASSERT_EQUAL_FLOAT(1.0f,    getGlobalValue(F_GPS_IS_AVAILABLE));
  TEST_ASSERT_EQUAL_FLOAT(88.0f,   getGlobalValue(F_GPS_CAR_SPEED));
}

void test_full_ecu_update_02_frame_updates_state(void) {
  // Positive counterpart: a well-formed frame must update all four fields.
  uint8_t frame[CAN_FRAME_MAX_LENGTH] = {0};
  frame[CAN_FRAME_ECU_UPDATE_INTAKE]         = 45;
  frame[CAN_FRAME_ECU_UPDATE_FUEL_HI]        = 0x10;  // 4096
  frame[CAN_FRAME_ECU_UPDATE_FUEL_LO]        = 0x00;
  frame[CAN_FRAME_ECU_UPDATE_GPS_AVAILABLE]  = 1;
  frame[CAN_FRAME_ECU_UPDATE_VEHICLE_SPEED]  = 60;

  hal_mock_can_inject(oilspeedTestGetCanHandle(),
                      CAN_ID_ECU_UPDATE_02,
                      CAN_FRAME_MAX_LENGTH, frame);
  canMainLoop();

  TEST_ASSERT_EQUAL_FLOAT(45.0f,   getGlobalValue(F_INTAKE_TEMP));
  TEST_ASSERT_EQUAL_FLOAT(4096.0f, getGlobalValue(F_FUEL));
  TEST_ASSERT_EQUAL_FLOAT(1.0f,    getGlobalValue(F_GPS_IS_AVAILABLE));
  TEST_ASSERT_EQUAL_FLOAT(60.0f,   getGlobalValue(F_GPS_CAR_SPEED));
}

void test_clock_brightness_frame_marks_cluster_connected(void) {
  // CAN_ID_CLOCK_BRIGHTNESS has no payload deref in OilAndSpeed - it is
  // used purely as a heartbeat for cluster-presence detection. Any length
  // ≤ CAN_FRAME_MAX_LENGTH must flip isClusterConnected() true.
  TEST_ASSERT_FALSE(isClusterConnected());

  uint8_t dummy[CAN_FRAME_MAX_LENGTH] = {0};
  hal_mock_can_inject(oilspeedTestGetCanHandle(),
                      CAN_ID_CLOCK_BRIGHTNESS,
                      CAN_FRAME_MAX_LENGTH, dummy);
  canMainLoop();

  TEST_ASSERT_TRUE(isClusterConnected());
}

void test_unknown_can_id_does_not_update_state(void) {
  // Unknown CAN IDs hit the `default` branch and must leave payload-derived
  // state intact. This catches a regression where onCanFrame might
  // accidentally drop into a typo'd case label.
  setGlobalValue(F_INTAKE_TEMP, 77.0f);
  setGlobalValue(F_FUEL, 1234.0f);
  uint8_t frame[CAN_FRAME_MAX_LENGTH] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22};
  hal_mock_can_inject(oilspeedTestGetCanHandle(),
                      0x7FFu, CAN_FRAME_MAX_LENGTH, frame);
  canMainLoop();
  TEST_ASSERT_EQUAL_FLOAT(77.0f,   getGlobalValue(F_INTAKE_TEMP));
  TEST_ASSERT_EQUAL_FLOAT(1234.0f, getGlobalValue(F_FUEL));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_truncated_ecu_update_02_frame_is_ignored);
  RUN_TEST(test_full_ecu_update_02_frame_updates_state);
  RUN_TEST(test_clock_brightness_frame_marks_cluster_connected);
  RUN_TEST(test_unknown_can_id_does_not_update_state);
  return UNITY_END();
}
