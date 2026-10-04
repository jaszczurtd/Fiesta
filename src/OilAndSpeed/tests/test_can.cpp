#include "can.h"
#include "hal/impl/.mock/hal_mock.h"
#include "peripherials.h"
#include "unity.h"

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

static void ensure_can_ready(void) {
  if (oilspeedTestGetCanHandle() == NULL) {
    TEST_ASSERT_EQUAL_INT(HAL_OK, canInit());
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
  hal_mock_can_inject(oilspeedTestGetCanHandle(), CAN_ID_ECU_UPDATE_02, 3,
                      shortFrame);
  canMainLoop();

  TEST_ASSERT_EQUAL_FLOAT(55.0f, getGlobalValue(F_INTAKE_TEMP));
  TEST_ASSERT_EQUAL_FLOAT(4321.0f, getGlobalValue(F_FUEL));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, getGlobalValue(F_GPS_IS_AVAILABLE));
  TEST_ASSERT_EQUAL_FLOAT(88.0f, getGlobalValue(F_GPS_CAR_SPEED));
}

void test_full_ecu_update_02_frame_updates_state(void) {
  // Positive counterpart: a well-formed frame must update all four fields.
  uint8_t frame[CAN_FRAME_MAX_LENGTH] = {0};
  frame[CAN_FRAME_ECU_UPDATE_INTAKE] = 45;
  frame[CAN_FRAME_ECU_UPDATE_FUEL_HI] = 0x10; // 4096
  frame[CAN_FRAME_ECU_UPDATE_FUEL_LO] = 0x00;
  frame[CAN_FRAME_ECU_UPDATE_GPS_AVAILABLE] = 1;
  frame[CAN_FRAME_ECU_UPDATE_VEHICLE_SPEED] = 60;

  hal_mock_can_inject(oilspeedTestGetCanHandle(), CAN_ID_ECU_UPDATE_02,
                      CAN_FRAME_MAX_LENGTH, frame);
  canMainLoop();

  TEST_ASSERT_EQUAL_FLOAT(45.0f, getGlobalValue(F_INTAKE_TEMP));
  TEST_ASSERT_EQUAL_FLOAT(4096.0f, getGlobalValue(F_FUEL));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, getGlobalValue(F_GPS_IS_AVAILABLE));
  TEST_ASSERT_EQUAL_FLOAT(60.0f, getGlobalValue(F_GPS_CAR_SPEED));
}

void test_clock_brightness_frame_marks_cluster_connected(void) {
  // CAN_ID_CLOCK_BRIGHTNESS has no payload deref in OilAndSpeed - it is
  // used purely as a heartbeat for cluster-presence detection. Any length
  // ≤ CAN_FRAME_MAX_LENGTH must flip isClusterConnected() true.
  TEST_ASSERT_FALSE(isClusterConnected());

  uint8_t dummy[CAN_FRAME_MAX_LENGTH] = {0};
  hal_mock_can_inject(oilspeedTestGetCanHandle(), CAN_ID_CLOCK_BRIGHTNESS,
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
  uint8_t frame[CAN_FRAME_MAX_LENGTH] = {0xAA, 0xBB, 0xCC, 0xDD,
                                         0xEE, 0xFF, 0x11, 0x22};
  hal_mock_can_inject(oilspeedTestGetCanHandle(), 0x7FFu, CAN_FRAME_MAX_LENGTH,
                      frame);
  canMainLoop();
  TEST_ASSERT_EQUAL_FLOAT(77.0f, getGlobalValue(F_INTAKE_TEMP));
  TEST_ASSERT_EQUAL_FLOAT(1234.0f, getGlobalValue(F_FUEL));
}

// ── TX schedule ──────────────────────────────────────────────────────────────
// The periodic frames run on timers. The loop hook sends nothing: sending
// both frames every loop pass (~2000 frames/s) took about half of the
// 500 kbit/s bus and overran the ECU's two receive buffers.

typedef struct {
  uint32_t oilSpeed;
  uint32_t egt;
  uint32_t other;
} sent_counts_t;

static void drainSent(sent_counts_t *counts) {
  uint32_t id = 0u;
  uint8_t len = 0u;
  uint8_t data[CAN_FRAME_MAX_LENGTH] = {0};
  while (hal_mock_can_get_sent(oilspeedTestGetCanHandle(), &id, &len, data) ==
         HAL_OK) {
    if (id == CAN_ID_OIL_AND_SPEED_MODULE_UPDATE) {
      counts->oilSpeed++;
    } else if (id == CAN_ID_EGT_UPDATE) {
      counts->egt++;
    } else {
      counts->other++;
    }
  }
}

void test_send_loop_sends_no_periodic_frames(void) {
  sent_counts_t counts = {};
  for (uint32_t pass = 0u; pass < 1000u; pass++) {
    canSendLoop();
    drainSent(&counts);
  }
  TEST_ASSERT_EQUAL_UINT32(0u, counts.oilSpeed);
  TEST_ASSERT_EQUAL_UINT32(0u, counts.egt);
  TEST_ASSERT_EQUAL_UINT32(0u, counts.other);
}

void test_broadcast_timers_pace_oil_speed_and_egt_frames(void) {
  hal_mock_set_millis(1000u);
  TEST_ASSERT_TRUE(canSetupBroadcastTimers());
  sent_counts_t counts = {};
  drainSent(&counts);
  counts = (sent_counts_t){};
  const uint32_t start = hal_millis();
  const uint32_t durationMs = 10000u;

  // Ten seconds of the core-0 loop, one pass per millisecond.
  for (uint32_t ms = 1u; ms <= durationMs; ms++) {
    hal_mock_set_millis(start + ms);
    canTickBroadcastTimers();
    canSendLoop();
    drainSent(&counts);
  }

  TEST_ASSERT_UINT32_WITHIN(1u, durationMs / CAN_UPDATE_RECIPIENTS,
                            counts.oilSpeed);
  TEST_ASSERT_UINT32_WITHIN(1u, durationMs / CAN_EGT_UPDATE_INTERVAL,
                            counts.egt);
  TEST_ASSERT_EQUAL_UINT32(0u, counts.other);
  // Both frames stay the module heartbeat: several arrive inside each
  // connection-check window of the receivers.
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(2u * CAN_EGT_UPDATE_INTERVAL,
                                      (uint32_t)CAN_CHECK_CONNECTION);
}

void test_egt_frame_carries_both_temperatures(void) {
  setGlobalValue(F_EGT, 612.0f);
  setGlobalValue(F_DPF_TEMP, 455.0f);
  updateEGTrecipients();

  uint32_t id = 0u;
  uint8_t len = 0u;
  uint8_t data[CAN_FRAME_MAX_LENGTH] = {0};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK,
      hal_mock_can_get_sent(oilspeedTestGetCanHandle(), &id, &len, data));
  TEST_ASSERT_EQUAL_UINT32(CAN_ID_EGT_UPDATE, id);
  TEST_ASSERT_EQUAL_UINT8(CAN_FRAME_MAX_LENGTH, len);
  TEST_ASSERT_EQUAL_UINT16(
      612u, jh_u16_from_bytes(data[CAN_FRAME_EGT_UPDATE_EGT_HI],
                              data[CAN_FRAME_EGT_UPDATE_EGT_LO]));
  TEST_ASSERT_EQUAL_UINT16(
      455u, jh_u16_from_bytes(data[CAN_FRAME_EGT_UPDATE_DPF_TEMP_HI],
                              data[CAN_FRAME_EGT_UPDATE_DPF_TEMP_LO]));
}

/* A controller that does not come up: canInit() returns why, and the
 * broadcasts and the receive loop do nothing without a channel. */
void test_can_init_reports_why_the_bus_stayed_off(void) {
  hal_can_destroy(oilspeedTestGetCanHandle());
  hal_mock_can_fail_creates(MAX_RETRIES + 1);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, canInit());
  TEST_ASSERT_NULL(oilspeedTestGetCanHandle());
  updateCANrecipients();
  updateEGTrecipients();
  canMainLoop();
  hal_mock_can_fail_creates(0u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, canInit());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_truncated_ecu_update_02_frame_is_ignored);
  RUN_TEST(test_full_ecu_update_02_frame_updates_state);
  RUN_TEST(test_clock_brightness_frame_marks_cluster_connected);
  RUN_TEST(test_unknown_can_id_does_not_update_state);
  RUN_TEST(test_send_loop_sends_no_periodic_frames);
  RUN_TEST(test_broadcast_timers_pace_oil_speed_and_egt_frames);
  RUN_TEST(test_egt_frame_carries_both_temperatures);
  RUN_TEST(test_can_init_reports_why_the_bus_stayed_off);
  return UNITY_END();
}
