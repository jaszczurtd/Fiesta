#include "can.h"
#include "hal/impl/.mock/hal_mock.h"
#include "unity.h"

#include <cstring>

hal_can_t clocksTestGetCanHandle(void);
float *clocksTestCanValues(void);

// Clocks can.cpp calls into logic.cpp (updateCluster) and peripherials.cpp
// (backlight level). Provide stubs here so the test target doesn't need to
// pull in the display + cluster graph.
static unsigned int clusterUpdateCount = 0u;
void updateCluster(void) { clusterUpdateCount++; }
int getLcdBrightness(void) { return 0; }

// Readings of core 1 taken now.
static clocks_local_snapshot_t freshLocal(void) {
  clocks_local_snapshot_t local = {};
  local.oilUpdatedMs = hal_millis();
  for (int channel = 0; channel < EGT_CHANNELS; channel++) {
    local.egtUpdatedMs[channel] = hal_millis();
  }
  return local;
}

// ECU state of core 0, as decoded by can.cpp.
#define canValues (clocksTestCanValues())

// watchdog_feed is declared in JaszczurHAL's multicoreWatchdog.h but its
// definition lives in a module we don't compile here; stub it out.
extern "C" void watchdog_feed(void) {}

static void ensure_can_ready(void) {
  if (clocksTestGetCanHandle() == NULL) {
    TEST_ASSERT_EQUAL_INT(HAL_OK, canInit());
  }
  TEST_ASSERT_NOT_NULL(clocksTestGetCanHandle());
  hal_mock_can_reset(clocksTestGetCanHandle());
}

void setUp(void) {
  ensure_can_ready();
  clusterUpdateCount = 0u;
  clocksStateInit();
  for (int i = 0; i < F_LAST; i++) {
    canValues[i] = 0.0f;
  }
  const clocks_local_snapshot_t noMeasurements = {};
  clocksStatePublishLocal(&noMeasurements);
  canCheckConnection();
  (void)clocksStateRefreshView();
}

void tearDown(void) {}

// ── RX validation regression guards ──────────────────────────────────────────
// Mirror the ECU's post-d6b8ffc CAN hardening: NULL/oversized frames and
// per-case truncated frames must be rejected before the switch body
// dereferences any payload byte.

void test_truncated_ecu_update_01_frame_is_ignored(void) {
  canValues[F_CALCULATED_ENGINE_LOAD] = 77.0f;
  canValues[F_VOLTS] = 13.8f;

  // ECU_UPDATE_01 reads up to CAN_FRAME_ECU_UPDATE_OIL (index 5);
  // a 3-byte frame must be rejected without touching state.
  uint8_t shortFrame[3] = {0x00, 0x11, 0x22};
  hal_mock_can_inject(clocksTestGetCanHandle(), CAN_ID_ECU_UPDATE_01, 3,
                      shortFrame);
  canMainLoop();

  TEST_ASSERT_EQUAL_FLOAT(77.0f, canValues[F_CALCULATED_ENGINE_LOAD]);
  TEST_ASSERT_EQUAL_FLOAT(13.8f, canValues[F_VOLTS]);
}

// Clocks is the only sender of 0x12A and 0x12F; the same IDs from another
// node, e.g. a forgotten OilAndSpeed, must not overwrite its measurements.
void test_own_frames_from_another_node_change_nothing(void) {
  canSetValue(F_ABS_CAR_SPEED, 42.0f);
  uint8_t frame[CAN_FRAME_MAX_LENGTH] = {0x00, 0x05, 0x00, 0x99,
                                         0x01, 0x00, 0x00, 0x00};
  hal_mock_can_inject(clocksTestGetCanHandle(),
                      CAN_ID_OIL_AND_SPEED_MODULE_UPDATE, CAN_FRAME_MAX_LENGTH,
                      frame);
  hal_mock_can_inject(clocksTestGetCanHandle(), CAN_ID_EGT_UPDATE,
                      CAN_FRAME_MAX_LENGTH, frame);
  canMainLoop();

  TEST_ASSERT_EQUAL_FLOAT(42.0f, canValues[F_ABS_CAR_SPEED]);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, canValues[F_OIL_PRESSURE]);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, canValues[F_EGT]);
  TEST_ASSERT_EQUAL_UINT(0u, clusterUpdateCount);
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

  TEST_ASSERT_EQUAL_INT(45, (int)canValues[F_INTAKE_TEMP]);
  TEST_ASSERT_EQUAL_INT(4096, (int)canValues[F_FUEL]);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, canValues[F_GPS_IS_AVAILABLE]);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, canValues[F_GPS_CAR_SPEED]);
}

void test_rpm_frame_updates_cluster_in_same_can_drain(void) {
  uint8_t frame[CAN_FRAME_MAX_LENGTH] = {0};
  frame[CAN_FRAME_RPM_UPDATE_HI] = 0x09;
  frame[CAN_FRAME_RPM_UPDATE_LO] = 0xC4; // 2500 RPM

  hal_mock_can_inject(clocksTestGetCanHandle(), CAN_ID_RPM,
                      CAN_FRAME_MAX_LENGTH, frame);

  TEST_ASSERT_EQUAL_UINT(0u, clusterUpdateCount);
  canMainLoop();

  TEST_ASSERT_EQUAL_FLOAT(2500.0f, canValues[F_RPM]);
  TEST_ASSERT_EQUAL_UINT(1u, clusterUpdateCount);
}

void test_non_fiesta_frame_is_rejected_by_acceptance_filters(void) {
  uint8_t frame[CAN_FRAME_MAX_LENGTH] = {0};

  hal_mock_can_inject(clocksTestGetCanHandle(), 0x555u, CAN_FRAME_MAX_LENGTH,
                      frame);

  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN,
                        hal_can_available(clocksTestGetCanHandle()));
}

void test_unknown_can_id_does_not_update_state(void) {
  canValues[F_INTAKE_TEMP] = 77.0f;
  canValues[F_FUEL] = 1234.0f;
  uint8_t frame[CAN_FRAME_MAX_LENGTH] = {0xAA, 0xBB, 0xCC, 0xDD,
                                         0xEE, 0xFF, 0x11, 0x22};
  hal_mock_can_inject(clocksTestGetCanHandle(), 0x131u, CAN_FRAME_MAX_LENGTH,
                      frame);
  canMainLoop();
  TEST_ASSERT_EQUAL_FLOAT(77.0f, canValues[F_INTAKE_TEMP]);
  TEST_ASSERT_EQUAL_FLOAT(1234.0f, canValues[F_FUEL]);
}

static void injectFrame(uint32_t id, const uint8_t *frame) {
  hal_mock_can_inject(clocksTestGetCanHandle(), id, CAN_FRAME_MAX_LENGTH,
                      frame);
  canMainLoop();
}

// ── Snapshot from core 0 to the display view of core 1 ──────────────────────

void test_display_view_changes_only_on_refresh(void) {
  uint8_t frame[CAN_FRAME_MAX_LENGTH] = {0};
  frame[CAN_FRAME_ECU_UPDATE_INTAKE] = 33;
  injectFrame(CAN_ID_ECU_UPDATE_02, frame);

  TEST_ASSERT_EQUAL_FLOAT(33.0f, canValues[F_INTAKE_TEMP]);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, valueFields[F_INTAKE_TEMP]);
  TEST_ASSERT_FALSE(isEcuConnected());

  (void)clocksStateRefreshView();
  TEST_ASSERT_EQUAL_FLOAT(33.0f, valueFields[F_INTAKE_TEMP]);
  TEST_ASSERT_TRUE(isEcuConnected());
}

void test_throttle_frame_requests_one_redraw(void) {
  (void)clocksStateRefreshView();
  uint8_t frame[CAN_FRAME_MAX_LENGTH] = {0};
  frame[CAN_FRAME_THROTTLE_UPDATE_HI] = 0x03;
  frame[CAN_FRAME_THROTTLE_UPDATE_LO] = 0xE8; // 1000
  injectFrame(CAN_ID_THROTTLE, frame);

  TEST_ASSERT_TRUE(clocksStateRefreshView());
  TEST_ASSERT_EQUAL_FLOAT(1000.0f, valueFields[F_THROTTLE_POS]);
  TEST_ASSERT_FALSE(clocksStateRefreshView());
}

void test_frame_without_redraw_value_refreshes_view_quietly(void) {
  (void)clocksStateRefreshView();
  uint8_t frame[CAN_FRAME_MAX_LENGTH] = {0};
  frame[CAN_FRAME_RPM_UPDATE_HI] = 0x03;
  frame[CAN_FRAME_RPM_UPDATE_LO] = 0x84; // 900 RPM
  injectFrame(CAN_ID_RPM, frame);

  TEST_ASSERT_FALSE(clocksStateRefreshView());
  TEST_ASSERT_EQUAL_INT(900, getEngineRPM());
}

void test_ecu_loss_clears_ecu_values_but_keeps_local_measurements(void) {
  uint8_t frame[CAN_FRAME_MAX_LENGTH] = {0};
  frame[CAN_FRAME_ECU_UPDATE_INTAKE] = 40;
  injectFrame(CAN_ID_ECU_UPDATE_02, frame);
  canSetValue(F_ABS_CAR_SPEED, 70.0f);
  clocks_local_snapshot_t local = freshLocal();
  local.oilValid = true;
  local.oilBar = 3.0f;
  local.egtValid[EGT_PRE_DPF] = true;
  local.egtC[EGT_PRE_DPF] = 480.0f;
  clocksStatePublishLocal(&local);
  canCheckConnection(); // frames since the previous check: ECU on
  (void)clocksStateRefreshView();
  TEST_ASSERT_TRUE(isEcuConnected());

  canCheckConnection(); // no ECU frame since the previous check
  TEST_ASSERT_TRUE(clocksStateRefreshView());
  TEST_ASSERT_FALSE(isEcuConnected());
  TEST_ASSERT_EQUAL_FLOAT(0.0f, valueFields[F_INTAKE_TEMP]);
  TEST_ASSERT_EQUAL_INT(70, getCurrentCarSpeed());
  TEST_ASSERT_EQUAL_FLOAT(3.0f, getOilPressure());
  TEST_ASSERT_TRUE(isEgtValid(EGT_PRE_DPF));
  TEST_ASSERT_EQUAL_FLOAT(480.0f, valueFields[F_EGT]);
}

// ── Freshness of the decoded values ─────────────────────────────────────────

void test_decoded_values_are_fresh_below_the_age_limit(void) {
  const uint32_t at = 50000u;
  hal_mock_set_millis(at);
  uint8_t rpm[CAN_FRAME_MAX_LENGTH] = {0};
  rpm[CAN_FRAME_RPM_UPDATE_LO] = 0x84;
  injectFrame(CAN_ID_RPM, rpm);
  hal_mock_set_millis(at + 40u);
  uint8_t ecu01[CAN_FRAME_MAX_LENGTH] = {0};
  ecu01[CAN_FRAME_ECU_UPDATE_VOLTS_HI] = 14;
  injectFrame(CAN_ID_ECU_UPDATE_01, ecu01);

  TEST_ASSERT_TRUE(canValueFresh(F_RPM, at + 99u, 100u));
  TEST_ASSERT_FALSE(canValueFresh(F_RPM, at + 100u, 100u));
  TEST_ASSERT_TRUE(canValueFresh(F_VOLTS, at + 139u, 100u));
  TEST_ASSERT_FALSE(canValueFresh(F_VOLTS, at + 140u, 100u));
  TEST_ASSERT_FALSE(canValueFresh(-1, at, 100u));
  TEST_ASSERT_FALSE(canValueFresh(F_LAST, at, 100u));
}

void test_truncated_frame_and_ecu_loss_do_not_refresh_values(void) {
  const uint32_t at = 60000u;
  hal_mock_set_millis(at);
  uint8_t ecu01[CAN_FRAME_MAX_LENGTH] = {0};
  injectFrame(CAN_ID_ECU_UPDATE_01, ecu01);
  canCheckConnection(); // ECU frames since the previous check: connected

  hal_mock_set_millis(at + 500u);
  hal_mock_can_inject(clocksTestGetCanHandle(), CAN_ID_ECU_UPDATE_01, 3, ecu01);
  canMainLoop();
  canCheckConnection(); // no valid ECU frame since: lost, values cleared
  (void)clocksStateRefreshView();
  TEST_ASSERT_FALSE(isEcuConnected());
  TEST_ASSERT_TRUE(canValueFresh(F_VOLTS, at + 499u, 500u));
  TEST_ASSERT_FALSE(canValueFresh(F_VOLTS, at + 500u, 500u));
}

void test_measured_speed_is_fresh_from_its_set_time(void) {
  hal_mock_set_millis(70000u);
  canSetValue(F_ABS_CAR_SPEED, 30.0f);
  TEST_ASSERT_TRUE(canValueFresh(F_ABS_CAR_SPEED, 70010u, 11u));
  TEST_ASSERT_FALSE(canValueFresh(F_ABS_CAR_SPEED, 70011u, 11u));
}

// ── Frames sent by Clocks ────────────────────────────────────────────────────

// The last sent frame with @p wanted ID; false when none went out.
static bool lastSent(uint32_t wanted, uint8_t payload[CAN_FRAME_MAX_LENGTH]) {
  bool found = false;
  uint32_t id = 0u;
  uint8_t len = 0u;
  uint8_t data[CAN_FRAME_MAX_LENGTH] = {0};
  while (hal_mock_can_get_sent(clocksTestGetCanHandle(), &id, &len, data) ==
         HAL_OK) {
    if (id == wanted && len == CAN_FRAME_MAX_LENGTH) {
      memcpy(payload, data, CAN_FRAME_MAX_LENGTH);
      found = true;
    }
  }
  return found;
}

void test_oil_and_speed_frame_carries_the_local_measurements(void) {
  canSetValue(F_ABS_CAR_SPEED, 72.0f);
  clocks_local_snapshot_t local = freshLocal();
  local.oilValid = true;
  local.oilBar = 3.4f;
  clocksStatePublishLocal(&local);

  updateCANrecipients();

  uint8_t payload[CAN_FRAME_MAX_LENGTH] = {0};
  TEST_ASSERT_TRUE(lastSent(CAN_ID_OIL_AND_SPEED_MODULE_UPDATE, payload));
  TEST_ASSERT_EQUAL_FLOAT(3.4f,
                          fiesta_can_join_decimal_tenths(
                              payload[CAN_FRAME_ECU_UPDATE_OIL_PRESSURE_HI],
                              payload[CAN_FRAME_ECU_UPDATE_OIL_PRESSURE_LO]));
  TEST_ASSERT_EQUAL_UINT8(72u, payload[CAN_FRAME_ECU_UPDATE_ABS_CAR_SPEED]);
  TEST_ASSERT_EQUAL_HEX8(CAN_OIL_AND_SPEED_STATUS_OIL_VALID,
                         payload[CAN_FRAME_OIL_AND_SPEED_STATUS]);
}

void test_faulty_oil_sender_is_sent_as_no_measurement(void) {
  clocks_local_snapshot_t local = freshLocal();
  local.oilValid = false;
  local.oilBar = 9.9f;
  clocksStatePublishLocal(&local);

  updateCANrecipients();

  uint8_t payload[CAN_FRAME_MAX_LENGTH] = {0};
  TEST_ASSERT_TRUE(lastSent(CAN_ID_OIL_AND_SPEED_MODULE_UPDATE, payload));
  TEST_ASSERT_EQUAL_HEX8(0u, payload[CAN_FRAME_ECU_UPDATE_OIL_PRESSURE_HI]);
  TEST_ASSERT_EQUAL_HEX8(0u, payload[CAN_FRAME_ECU_UPDATE_OIL_PRESSURE_LO]);
  TEST_ASSERT_EQUAL_HEX8(0u, payload[CAN_FRAME_OIL_AND_SPEED_STATUS]);
}

// Core 0 stops trusting a reading core 1 no longer renews, e.g. a hung core.
void test_stale_local_readings_lose_their_flags(void) {
  const uint32_t at = 80000u;
  hal_mock_set_millis(at);
  clocks_local_snapshot_t local = freshLocal();
  local.oilValid = true;
  local.oilBar = 3.5f;
  local.egtValid[EGT_PRE_DPF] = true;
  local.egtC[EGT_PRE_DPF] = 400.0f;
  clocksStatePublishLocal(&local);

  clocks_local_snapshot_t copy;
  hal_mock_set_millis(at + OIL_PRESSURE_MAX_AGE_MS - 1u);
  clocksStateCopyLocal(&copy);
  TEST_ASSERT_TRUE(copy.oilValid);
  hal_mock_set_millis(at + OIL_PRESSURE_MAX_AGE_MS);
  clocksStateCopyLocal(&copy);
  TEST_ASSERT_FALSE(copy.oilValid);

  hal_mock_set_millis(at + THERMOCOUPLE_MAX_AGE_MS - 1u);
  clocksStateCopyLocal(&copy);
  TEST_ASSERT_TRUE(copy.egtValid[EGT_PRE_DPF]);
  hal_mock_set_millis(at + THERMOCOUPLE_MAX_AGE_MS);
  clocksStateCopyLocal(&copy);
  TEST_ASSERT_FALSE(copy.egtValid[EGT_PRE_DPF]);
  TEST_ASSERT_FALSE(copy.egtValid[EGT_IN_DPF]);
}

// Core 1 stamps a reading in the same millisecond tick as core 0 checks
// it, or one later: that reading is new, not expired.
void test_reading_stamped_after_the_check_is_fresh(void) {
  const uint32_t at = 85000u;
  hal_mock_set_millis(at);
  clocks_local_snapshot_t local = {};
  local.oilValid = true;
  local.oilUpdatedMs = at + 1u;
  local.egtValid[EGT_IN_DPF] = true;
  local.egtUpdatedMs[EGT_IN_DPF] = at + 1u;
  clocksStatePublishLocal(&local);

  clocks_local_snapshot_t copy;
  clocksStateCopyLocal(&copy);
  TEST_ASSERT_TRUE(copy.oilValid);
  TEST_ASSERT_TRUE(copy.egtValid[EGT_IN_DPF]);
}

void test_stale_oil_reading_goes_out_without_its_flag(void) {
  const uint32_t at = 90000u;
  hal_mock_set_millis(at);
  clocks_local_snapshot_t local = freshLocal();
  local.oilValid = true;
  local.oilBar = 3.5f;
  clocksStatePublishLocal(&local);

  hal_mock_set_millis(at + OIL_PRESSURE_MAX_AGE_MS);
  updateCANrecipients();
  uint8_t sent[CAN_FRAME_MAX_LENGTH] = {0};
  TEST_ASSERT_TRUE(lastSent(CAN_ID_OIL_AND_SPEED_MODULE_UPDATE, sent));
  TEST_ASSERT_EQUAL_HEX8(0u, sent[CAN_FRAME_OIL_AND_SPEED_STATUS]);
  TEST_ASSERT_EQUAL_UINT8(0u, sent[CAN_FRAME_ECU_UPDATE_OIL_PRESSURE_HI]);
}

void test_egt_frame_carries_signed_temperatures_and_validity(void) {
  clocks_local_snapshot_t local = freshLocal();
  local.egtValid[EGT_PRE_DPF] = true;
  local.egtC[EGT_PRE_DPF] = 612.9f;
  local.egtValid[EGT_IN_DPF] = true;
  local.egtC[EGT_IN_DPF] = -18.5f;
  clocksStatePublishLocal(&local);

  updateEGTrecipients();

  uint8_t payload[CAN_FRAME_MAX_LENGTH] = {0};
  TEST_ASSERT_TRUE(lastSent(CAN_ID_EGT_UPDATE, payload));
  TEST_ASSERT_EQUAL_INT16(
      612, fiesta_can_get_temp_i16(&payload[CAN_FRAME_EGT_UPDATE_EGT_HI]));
  TEST_ASSERT_EQUAL_INT16(
      -18, fiesta_can_get_temp_i16(&payload[CAN_FRAME_EGT_UPDATE_DPF_TEMP_HI]));
  TEST_ASSERT_EQUAL_HEX8(CAN_EGT_STATUS_EGT_VALID |
                             CAN_EGT_STATUS_DPF_TEMP_VALID,
                         payload[CAN_FRAME_EGT_UPDATE_STATUS]);
}

void test_egt_channel_without_measurement_is_zero_without_its_flag(void) {
  clocks_local_snapshot_t local = freshLocal();
  local.egtValid[EGT_PRE_DPF] = false;
  local.egtC[EGT_PRE_DPF] = 700.0f;
  local.egtValid[EGT_IN_DPF] = true;
  local.egtC[EGT_IN_DPF] = 350.0f;
  clocksStatePublishLocal(&local);

  updateEGTrecipients();

  uint8_t payload[CAN_FRAME_MAX_LENGTH] = {0};
  TEST_ASSERT_TRUE(lastSent(CAN_ID_EGT_UPDATE, payload));
  TEST_ASSERT_EQUAL_INT16(
      0, fiesta_can_get_temp_i16(&payload[CAN_FRAME_EGT_UPDATE_EGT_HI]));
  TEST_ASSERT_EQUAL_INT16(
      350, fiesta_can_get_temp_i16(&payload[CAN_FRAME_EGT_UPDATE_DPF_TEMP_HI]));
  TEST_ASSERT_EQUAL_HEX8(CAN_EGT_STATUS_DPF_TEMP_VALID,
                         payload[CAN_FRAME_EGT_UPDATE_STATUS]);
}

// The periodic frames run on timers, never on every loop pass: one pass per
// millisecond would take about half of the 500 kbit/s bus.
void test_broadcast_timers_pace_the_periodic_frames(void) {
  hal_mock_set_millis(1000u);
  TEST_ASSERT_TRUE(canSetupBroadcastTimers());
  uint8_t payload[CAN_FRAME_MAX_LENGTH] = {0};
  (void)lastSent(0u, payload);
  uint32_t sent[4] = {0u, 0u, 0u, 0u}; // brightness, oil and speed, EGT, UTC
  const uint32_t ids[4] = {CAN_ID_CLOCK_BRIGHTNESS,
                           CAN_ID_OIL_AND_SPEED_MODULE_UPDATE,
                           CAN_ID_EGT_UPDATE, CAN_ID_GPS_UTC_UPDATE};
  const uint32_t durationMs = 10000u;

  for (uint32_t ms = 1u; ms <= durationMs; ms++) {
    hal_mock_set_millis(1000u + ms);
    canTickBroadcastTimers();
    uint32_t id = 0u;
    uint8_t len = 0u;
    while (hal_mock_can_get_sent(clocksTestGetCanHandle(), &id, &len,
                                 payload) == HAL_OK) {
      for (size_t kind = 0u; kind < COUNTOF(ids); kind++) {
        sent[kind] += (id == ids[kind]) ? 1u : 0u;
      }
    }
  }

  TEST_ASSERT_UINT32_WITHIN(1u, durationMs / CAN_UPDATE_RECIPIENTS, sent[0]);
  TEST_ASSERT_UINT32_WITHIN(1u, durationMs / CAN_UPDATE_RECIPIENTS, sent[1]);
  TEST_ASSERT_UINT32_WITHIN(1u, durationMs / CAN_EGT_UPDATE_INTERVAL, sent[2]);
  TEST_ASSERT_UINT32_WITHIN(1u, durationMs / CAN_GPS_UPDATE_INTERVAL, sent[3]);
}

void test_local_gps_survives_ecu_values_and_ecu_connection_loss(void) {
  hal_mock_set_millis(12000u);
  clocks_local_snapshot_t local = {};
  local.gps.available = true;
  local.gps.locationUpdatedMs = hal_millis();
  local.gps.speedKmh = 42.0;
  clocksStatePublishLocal(&local);
  const uint8_t ecu[CAN_FRAME_MAX_LENGTH] = {0u, 45u, 0u, 12u, 0u, 99u, 0u, 0u};
  hal_mock_can_inject(clocksTestGetCanHandle(), CAN_ID_ECU_UPDATE_02, 8u, ecu);
  canMainLoop();
  (void)clocksStateRefreshView();
  TEST_ASSERT_TRUE(isGPSAvailable());
  TEST_ASSERT_EQUAL_INT(42, getGPSSpeed());
  canCheckConnection();
  canCheckConnection();
  (void)clocksStateRefreshView();
  TEST_ASSERT_FALSE(isEcuConnected());
  TEST_ASSERT_TRUE(isGPSAvailable());
  TEST_ASSERT_EQUAL_INT(42, getGPSSpeed());
}

void test_gps_broadcast_sends_utc_and_preserves_legacy_id_ownership(void) {
  hal_mock_set_millis(13000u);
  clocks_local_snapshot_t local = {};
  local.gps.available = local.gps.utcValid = true;
  local.gps.locationUpdatedMs = local.gps.utcUpdatedMs = hal_millis();
  local.gps.latitude = 52.2296756;
  local.gps.epochUtc = 1793018096u;
  clocksStatePublishLocal(&local);
  updateGPSrecipients();
  uint32_t id;
  uint8_t len, payload[8];
  unsigned utcFrames = 0u, legacyFrames = 0u;
  while (hal_mock_can_get_sent(clocksTestGetCanHandle(), &id, &len, payload) ==
         HAL_OK) {
    if (id == CAN_ID_GPS_UTC_UPDATE) {
      ++utcFrames;
      TEST_ASSERT_EQUAL_UINT8(8u, len);
      TEST_ASSERT_EQUAL_UINT32(1793018096u, jh_load_be32(payload + 1));
      TEST_ASSERT_EQUAL_UINT8(1u, payload[CAN_FRAME_GPS_UTC_STATUS]);
    } else if (id == CAN_ID_GPS_EXT_LAT || id == CAN_ID_GPS_EXT_LON_TIME) {
      ++legacyFrames;
    }
  }
  TEST_ASSERT_EQUAL_UINT(1u, utcFrames);
  TEST_ASSERT_EQUAL_UINT(CLOCKS_PUBLISH_LEGACY_GPS ? 2u : 0u, legacyFrames);
  hal_mock_set_millis(13000u + GPS_UTC_MAX_AGE_MS + 1u);
  updateGPSrecipients();
  TEST_ASSERT_TRUE(lastSent(CAN_ID_GPS_UTC_UPDATE, payload));
  TEST_ASSERT_EQUAL_UINT32(0u, jh_load_be32(payload + 1));
  TEST_ASSERT_EQUAL_UINT8(0u, payload[CAN_FRAME_GPS_UTC_STATUS]);
}

/* A controller that does not come up: canInit() returns why, and the loop
 * functions do nothing without a channel. */
void test_can_init_reports_why_the_bus_stayed_off(void) {
  hal_can_destroy(clocksTestGetCanHandle());
  hal_mock_can_fail_creates(MAX_RETRIES + 1);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, canInit());
  TEST_ASSERT_NULL(clocksTestGetCanHandle());
  updateCANrecipients();
  canMainLoop();
  hal_mock_can_fail_creates(0u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, canInit());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_truncated_ecu_update_01_frame_is_ignored);
  RUN_TEST(test_own_frames_from_another_node_change_nothing);
  RUN_TEST(test_full_ecu_update_02_frame_updates_state);
  RUN_TEST(test_rpm_frame_updates_cluster_in_same_can_drain);
  RUN_TEST(test_non_fiesta_frame_is_rejected_by_acceptance_filters);
  RUN_TEST(test_unknown_can_id_does_not_update_state);
  RUN_TEST(test_display_view_changes_only_on_refresh);
  RUN_TEST(test_throttle_frame_requests_one_redraw);
  RUN_TEST(test_frame_without_redraw_value_refreshes_view_quietly);
  RUN_TEST(test_ecu_loss_clears_ecu_values_but_keeps_local_measurements);
  RUN_TEST(test_decoded_values_are_fresh_below_the_age_limit);
  RUN_TEST(test_truncated_frame_and_ecu_loss_do_not_refresh_values);
  RUN_TEST(test_measured_speed_is_fresh_from_its_set_time);
  RUN_TEST(test_oil_and_speed_frame_carries_the_local_measurements);
  RUN_TEST(test_faulty_oil_sender_is_sent_as_no_measurement);
  RUN_TEST(test_stale_local_readings_lose_their_flags);
  RUN_TEST(test_reading_stamped_after_the_check_is_fresh);
  RUN_TEST(test_stale_oil_reading_goes_out_without_its_flag);
  RUN_TEST(test_egt_frame_carries_signed_temperatures_and_validity);
  RUN_TEST(test_egt_channel_without_measurement_is_zero_without_its_flag);
  RUN_TEST(test_broadcast_timers_pace_the_periodic_frames);
  RUN_TEST(test_local_gps_survives_ecu_values_and_ecu_connection_loss);
  RUN_TEST(test_gps_broadcast_sends_utc_and_preserves_legacy_id_ownership);
  RUN_TEST(test_can_init_reports_why_the_bus_stayed_off);
  return UNITY_END();
}
