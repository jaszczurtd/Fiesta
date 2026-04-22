/**
 * @file test_sensors_internal.cpp
 * @brief Direct unit tests for internal sensors.c helpers and state.
 */

#include "utils/unity.h"
#include "sensors.h"
#include "adjustometer_unit_testing.h"
#include "hal/impl/.mock/hal_mock.h"

static const uint32_t kPulseWindow = 128U;

static void runCompletedWindow(uint32_t nowUs, uint32_t periodUs) {
    adj_sensors_test_state_t state = {};
    const uint32_t windowUs = kPulseWindow * periodUs;
    uint32_t windowStartUs = nowUs - windowUs;

    if (windowStartUs == 0U) {
        windowStartUs = 1U;
        nowUs = windowStartUs + windowUs;
    }

    adj_test_sensors_get_state(&state);
    state.windowStartUs = windowStartUs;
    state.windowCount = kPulseWindow - 1U;
    adj_test_sensors_set_state(&state);
    hal_mock_set_micros(nowUs);
    adj_test_sensors_count_edge();
}

void setUp(void) {
    hal_mock_set_micros(0);
    hal_mock_set_millis(0);
    adj_test_sensors_reset_state();
}

void tearDown(void) {}

void test_adjustometer_ema_uses_minimum_positive_step(void) {
    TEST_ASSERT_EQUAL_UINT32(1001U, adj_test_sensors_apply_adjustometer_ema(1001U, 1000U));
}

void test_adjustometer_ema_uses_minimum_negative_step(void) {
    TEST_ASSERT_EQUAL_UINT32(999U, adj_test_sensors_apply_adjustometer_ema(999U, 1000U));
}

void test_adc_ema_initializes_from_negative_previous(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.5f, adj_test_sensors_apply_adc_ema(12.5f, -1.0f));
}

void test_internal_state_round_trip(void) {
    adj_sensors_test_state_t expected = {};
    expected.pulse = -123;
    expected.lastEdgeUs = 4567U;
    expected.signalHz = 9876U;
    expected.windowStartUs = 1111U;
    expected.windowCount = 64U;
    expected.filteredHz = 9950U;
    expected.baselineStartUs = 2222U;
    expected.baselineEstimate = 10010U;
    expected.baselineStableWindows = 4U;
    expected.baseline = 10000U;
    expected.baselineReady = true;
    expected.verifying = true;
    expected.verifyStartUs = 3333U;
    expected.zeroHold = false;
    expected.zeroCandidateSign = -1;
    expected.zeroCandidateWindows = 2U;
    expected.filteredFuelTemp = 41.25f;
    expected.filteredVoltage = 13.75f;

    adj_test_sensors_set_state(&expected);

    adj_sensors_test_state_t actual = {};
    adj_test_sensors_get_state(&actual);

    TEST_ASSERT_EQUAL_INT32(expected.pulse, actual.pulse);
    TEST_ASSERT_EQUAL_UINT32(expected.lastEdgeUs, actual.lastEdgeUs);
    TEST_ASSERT_EQUAL_UINT32(expected.signalHz, actual.signalHz);
    TEST_ASSERT_EQUAL_UINT32(expected.windowStartUs, actual.windowStartUs);
    TEST_ASSERT_EQUAL_UINT32(expected.windowCount, actual.windowCount);
    TEST_ASSERT_EQUAL_UINT32(expected.filteredHz, actual.filteredHz);
    TEST_ASSERT_EQUAL_UINT32(expected.baselineStartUs, actual.baselineStartUs);
    TEST_ASSERT_EQUAL_UINT32(expected.baselineEstimate, actual.baselineEstimate);
    TEST_ASSERT_EQUAL_UINT32(expected.baselineStableWindows, actual.baselineStableWindows);
    TEST_ASSERT_EQUAL_UINT32(expected.baseline, actual.baseline);
    TEST_ASSERT_EQUAL(expected.baselineReady, actual.baselineReady);
    TEST_ASSERT_EQUAL(expected.verifying, actual.verifying);
    TEST_ASSERT_EQUAL_UINT32(expected.verifyStartUs, actual.verifyStartUs);
    TEST_ASSERT_EQUAL(expected.zeroHold, actual.zeroHold);
    TEST_ASSERT_EQUAL_INT8(expected.zeroCandidateSign, actual.zeroCandidateSign);
    TEST_ASSERT_EQUAL_UINT8(expected.zeroCandidateWindows, actual.zeroCandidateWindows);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, expected.filteredFuelTemp, actual.filteredFuelTemp);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, expected.filteredVoltage, actual.filteredVoltage);
}

void test_reset_state_clears_internal_runtime(void) {
    adj_sensors_test_state_t dirty = {};
    dirty.pulse = 77;
    dirty.lastEdgeUs = 100U;
    dirty.signalHz = 12000U;
    dirty.windowStartUs = 10U;
    dirty.windowCount = 12U;
    dirty.filteredHz = 11800U;
    dirty.baselineStartUs = 20U;
    dirty.baselineEstimate = 11950U;
    dirty.baselineStableWindows = 5U;
    dirty.baseline = 12000U;
    dirty.baselineReady = true;
    dirty.verifying = true;
    dirty.verifyStartUs = 30U;
    dirty.zeroHold = false;
    dirty.zeroCandidateSign = 1;
    dirty.zeroCandidateWindows = 3U;
    dirty.filteredFuelTemp = 20.0f;
    dirty.filteredVoltage = 12.0f;
    adj_test_sensors_set_state(&dirty);

    adj_test_sensors_reset_state();

    adj_sensors_test_state_t actual = {};
    adj_test_sensors_get_state(&actual);

    TEST_ASSERT_EQUAL_INT32(0, actual.pulse);
    TEST_ASSERT_EQUAL_UINT32(0U, actual.lastEdgeUs);
    TEST_ASSERT_EQUAL_UINT32(0U, actual.signalHz);
    TEST_ASSERT_EQUAL_UINT32(0U, actual.windowStartUs);
    TEST_ASSERT_EQUAL_UINT32(0U, actual.windowCount);
    TEST_ASSERT_EQUAL_UINT32(0U, actual.filteredHz);
    TEST_ASSERT_EQUAL_UINT32(0U, actual.baselineStartUs);
    TEST_ASSERT_EQUAL_UINT32(0U, actual.baselineEstimate);
    TEST_ASSERT_EQUAL_UINT32(0U, actual.baselineStableWindows);
    TEST_ASSERT_EQUAL_UINT32(0U, actual.baseline);
    TEST_ASSERT_FALSE(actual.baselineReady);
    TEST_ASSERT_FALSE(actual.verifying);
    TEST_ASSERT_EQUAL_UINT32(0U, actual.verifyStartUs);
    TEST_ASSERT_TRUE(actual.zeroHold);
    TEST_ASSERT_EQUAL_INT8(0, actual.zeroCandidateSign);
    TEST_ASSERT_EQUAL_UINT8(0U, actual.zeroCandidateWindows);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -1.0f, actual.filteredFuelTemp);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -1.0f, actual.filteredVoltage);
}

void test_signal_loss_uses_minimum_timeout_clamp(void) {
    adj_sensors_test_state_t state = {};
    state.lastEdgeUs = 1000U;
    state.signalHz = 50000U;
    adj_test_sensors_set_state(&state);

    hal_mock_set_micros(11000U);
    TEST_ASSERT_FALSE(adj_test_sensors_is_signal_lost());

    hal_mock_set_micros(11001U);
    TEST_ASSERT_TRUE(adj_test_sensors_is_signal_lost());
}

void test_signal_loss_uses_maximum_timeout_clamp(void) {
    adj_sensors_test_state_t state = {};
    state.lastEdgeUs = 1000U;
    state.signalHz = 1U;
    adj_test_sensors_set_state(&state);

    hal_mock_set_micros(201000U);
    TEST_ASSERT_FALSE(adj_test_sensors_is_signal_lost());

    hal_mock_set_micros(201001U);
    TEST_ASSERT_TRUE(adj_test_sensors_is_signal_lost());
}

void test_baseline_convergence_enters_verification_after_last_stable_window(void) {
    const uint32_t periodUs = 100U;
    const uint32_t nowUs = (ADJUSTOMETER_BASELINE_MIN_TIME_MS * 1000U) + (kPulseWindow * periodUs) + 1000U;

    adj_sensors_test_state_t state = {};
    state.filteredHz = 10000U;
    state.baselineStartUs = nowUs - (ADJUSTOMETER_BASELINE_MIN_TIME_MS * 1000U);
    state.baselineEstimate = 10000U;
    state.baselineStableWindows = ADJUSTOMETER_BASELINE_LOCK_WINDOWS - 1U;
    adj_test_sensors_set_state(&state);

    runCompletedWindow(nowUs, periodUs);

    adj_sensors_test_state_t actual = {};
    adj_test_sensors_get_state(&actual);

    TEST_ASSERT_TRUE(actual.verifying);
    TEST_ASSERT_FALSE(actual.baselineReady);
    TEST_ASSERT_EQUAL_UINT32(10000U, actual.baseline);
    TEST_ASSERT_EQUAL_UINT32(10000U, actual.filteredHz);
    TEST_ASSERT_EQUAL_UINT32(10000U, actual.signalHz);
    TEST_ASSERT_EQUAL_UINT32(nowUs, actual.verifyStartUs);
    TEST_ASSERT_EQUAL_INT32(0, actual.pulse);
}

void test_baseline_verification_drift_restarts_convergence(void) {
    const uint32_t periodUs = 50U;
    const uint32_t nowUs = 200000U;

    adj_sensors_test_state_t state = {};
    state.filteredHz = 10000U;
    state.baselineStartUs = 12345U;
    state.baselineEstimate = 10000U;
    state.baselineStableWindows = 5U;
    state.baseline = 10000U;
    state.verifying = true;
    state.verifyStartUs = nowUs - 100U;
    adj_test_sensors_set_state(&state);

    runCompletedWindow(nowUs, periodUs);

    adj_sensors_test_state_t actual = {};
    adj_test_sensors_get_state(&actual);

    TEST_ASSERT_FALSE(actual.verifying);
    TEST_ASSERT_FALSE(actual.baselineReady);
    TEST_ASSERT_EQUAL_UINT32(11250U, actual.filteredHz);
    TEST_ASSERT_EQUAL_UINT32(11250U, actual.signalHz);
    TEST_ASSERT_EQUAL_UINT32(11250U, actual.baselineEstimate);
    TEST_ASSERT_EQUAL_UINT32(nowUs, actual.baselineStartUs);
    TEST_ASSERT_EQUAL_UINT32(0U, actual.baselineStableWindows);
    TEST_ASSERT_EQUAL_UINT32(10000U, actual.baseline);
    TEST_ASSERT_EQUAL_INT32(0, actual.pulse);
}

void test_baseline_verification_success_sets_ready_and_resets_zero_hold(void) {
    const uint32_t periodUs = 100U;
    const uint32_t nowUs = (ADJUSTOMETER_BASELINE_VERIFY_MS * 1000U) + (kPulseWindow * periodUs) + 1000U;

    adj_sensors_test_state_t state = {};
    state.filteredHz = 10000U;
    state.baselineEstimate = 10020U;
    state.baseline = 10000U;
    state.verifying = true;
    state.verifyStartUs = nowUs - (ADJUSTOMETER_BASELINE_VERIFY_MS * 1000U);
    state.zeroHold = false;
    state.zeroCandidateSign = -1;
    state.zeroCandidateWindows = 7U;
    state.pulse = 321;
    adj_test_sensors_set_state(&state);

    runCompletedWindow(nowUs, periodUs);

    adj_sensors_test_state_t actual = {};
    adj_test_sensors_get_state(&actual);

    TEST_ASSERT_TRUE(actual.baselineReady);
    TEST_ASSERT_EQUAL_UINT32(10020U, actual.baseline);
    TEST_ASSERT_EQUAL_UINT32(10020U, actual.filteredHz);
    TEST_ASSERT_EQUAL_UINT32(10020U, actual.signalHz);
    TEST_ASSERT_EQUAL_INT32(0, actual.pulse);
    TEST_ASSERT_TRUE(actual.zeroHold);
    TEST_ASSERT_EQUAL_INT8(0, actual.zeroCandidateSign);
    TEST_ASSERT_EQUAL_UINT8(0U, actual.zeroCandidateWindows);
}

void test_zero_hold_requires_two_same_sign_windows_to_release(void) {
    adj_sensors_test_state_t state = {};
    state.filteredHz = 1000U;
    state.signalHz = 1000U;
    state.baseline = 1000U;
    state.baselineReady = true;
    state.zeroHold = true;
    adj_test_sensors_set_state(&state);

    runCompletedWindow(64000U, 500U);

    adj_sensors_test_state_t afterFirst = {};
    adj_test_sensors_get_state(&afterFirst);
    TEST_ASSERT_TRUE(afterFirst.zeroHold);
    TEST_ASSERT_EQUAL_INT8(1, afterFirst.zeroCandidateSign);
    TEST_ASSERT_EQUAL_UINT8(1U, afterFirst.zeroCandidateWindows);
    TEST_ASSERT_EQUAL_UINT32(1125U, afterFirst.filteredHz);
    TEST_ASSERT_EQUAL_INT32(0, afterFirst.pulse);

    runCompletedWindow(128000U, 500U);

    adj_sensors_test_state_t afterSecond = {};
    adj_test_sensors_get_state(&afterSecond);
    TEST_ASSERT_FALSE(afterSecond.zeroHold);
    TEST_ASSERT_EQUAL_INT8(0, afterSecond.zeroCandidateSign);
    TEST_ASSERT_EQUAL_UINT8(0U, afterSecond.zeroCandidateWindows);
    TEST_ASSERT_EQUAL_UINT32(1234U, afterSecond.filteredHz);
    TEST_ASSERT_EQUAL_INT32(234, afterSecond.pulse);
}

void test_zero_hold_sign_change_restarts_candidate_sequence(void) {
    adj_sensors_test_state_t state = {};
    state.filteredHz = 940U;
    state.signalHz = 940U;
    state.baseline = 1000U;
    state.baselineReady = true;
    state.zeroHold = true;
    state.zeroCandidateSign = 1;
    state.zeroCandidateWindows = 1U;
    adj_test_sensors_set_state(&state);

    runCompletedWindow(256000U, 2000U);

    adj_sensors_test_state_t actual = {};
    adj_test_sensors_get_state(&actual);

    TEST_ASSERT_TRUE(actual.zeroHold);
    TEST_ASSERT_EQUAL_INT8(-1, actual.zeroCandidateSign);
    TEST_ASSERT_EQUAL_UINT8(1U, actual.zeroCandidateWindows);
    TEST_ASSERT_EQUAL_UINT32(885U, actual.filteredHz);
    TEST_ASSERT_EQUAL_INT32(0, actual.pulse);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_adjustometer_ema_uses_minimum_positive_step);
    RUN_TEST(test_adjustometer_ema_uses_minimum_negative_step);
    RUN_TEST(test_adc_ema_initializes_from_negative_previous);
    RUN_TEST(test_internal_state_round_trip);
    RUN_TEST(test_reset_state_clears_internal_runtime);
    RUN_TEST(test_signal_loss_uses_minimum_timeout_clamp);
    RUN_TEST(test_signal_loss_uses_maximum_timeout_clamp);
    RUN_TEST(test_baseline_convergence_enters_verification_after_last_stable_window);
    RUN_TEST(test_baseline_verification_drift_restarts_convergence);
    RUN_TEST(test_baseline_verification_success_sets_ready_and_resets_zero_hold);
    RUN_TEST(test_zero_hold_requires_two_same_sign_windows_to_release);
    RUN_TEST(test_zero_hold_sign_change_restarts_candidate_sequence);
    return UNITY_END();
}