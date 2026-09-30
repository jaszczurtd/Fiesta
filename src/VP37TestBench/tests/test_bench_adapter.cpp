// The bench side of the VP37 module: the board services of
// vp37_bench_adapter.c on the test stand's own hardware. The module's own
// suite lives in src/common/vp37/tests/test_vp37.cpp.
#include "hal/impl/.mock/hal_mock.h"
#include "hardwareConfig.h"
#include "unity.h"
#include "vp37_bench_adapter.h"

static VP37Pump s_pump;

void setUp(void) {
  hal_mock_set_millis(0);
  hal_mock_i2c_set_busy(false);
  hal_i2c_init(PIN_SDA, PIN_SCL, HAL_I2C_CLOCK_FAST_HZ);
  (void)memset(&s_pump, 0, sizeof(s_pump));
}

void tearDown(void) {
  (void)VP37_currentScanStop();
  hal_mock_i2c_set_busy(false);
}

void test_callbacks_cover_every_board_service(void) {
  const VP37Callbacks *cb = vp37BenchCallbacks();
  TEST_ASSERT_NOT_NULL(cb->writeQuantityPwm);
  TEST_ASSERT_NOT_NULL(cb->writeTimingPwm);
  TEST_ASSERT_NOT_NULL(cb->setDriveEnabled);
  TEST_ASSERT_NOT_NULL(cb->driveEnabled);
  TEST_ASSERT_NOT_NULL(cb->adjustometerTransfer);
  TEST_ASSERT_NOT_NULL(cb->feedWatchdog);
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_setCallbacks(&s_pump, cb));
}

void test_start_without_adjustometer_ends_baseline_not_ready(void) {
  // No Adjustometer answers on the bus: the start waits the full baseline
  // window and hands back a stopped pump, and the bench keeps running.
  const VP37InitStatus status = vp37BenchAdapterStart(&s_pump);
  TEST_ASSERT_EQUAL_INT(VP37_INIT_BASELINE_NOT_READY, status);
  TEST_ASSERT_FALSE(VP37_isReady(&s_pump));
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(VP37_ADJUSTOMETER_BASELINE_WAIT_MS,
                                      hal_millis());
  // The drive outputs exist with the power-stage parameters.
  hal_pwm_freq_channel_t quantity = vp37BenchQuantityChannel();
  hal_pwm_freq_channel_t timing = vp37BenchTimingChannel();
  TEST_ASSERT_NOT_NULL(quantity);
  TEST_ASSERT_NOT_NULL(timing);
  TEST_ASSERT_EQUAL_UINT8(VP37_QUANTITY_PWM_PIN,
                          hal_mock_pwm_freq_get_pin(quantity));
  TEST_ASSERT_EQUAL_UINT8(VP37_TIMING_PWM_PIN,
                          hal_mock_pwm_freq_get_pin(timing));
  TEST_ASSERT_EQUAL_UINT32(VP37_PWM_FREQUENCY_HZ,
                           hal_mock_pwm_freq_get_frequency(quantity));
  TEST_ASSERT_EQUAL_UINT32(VP37_TIMING_PWM_FREQUENCY_HZ,
                           hal_mock_pwm_freq_get_frequency(timing));
  // The enable output starts driven low: the drive stays off with no pump.
  TEST_ASSERT_TRUE(hal_mock_gpio_is_output(PIN_VP37_ENABLE));
  TEST_ASSERT_FALSE(hal_mock_gpio_get_state(PIN_VP37_ENABLE));
}

void test_pwm_writes_are_active_low_like_the_ecu(void) {
  (void)vp37BenchAdapterStart(&s_pump);
  const VP37Callbacks *cb = vp37BenchCallbacks();
  cb->writeQuantityPwm(500);
  cb->writeTimingPwm(300);
  TEST_ASSERT_EQUAL_INT(
      VP37_PWM_RESOLUTION - 500,
      hal_mock_pwm_freq_get_value(vp37BenchQuantityChannel()));
  TEST_ASSERT_EQUAL_INT(VP37_PWM_RESOLUTION - 300,
                        hal_mock_pwm_freq_get_value(vp37BenchTimingChannel()));
  cb->writeQuantityPwm(VP37_PWM_RESOLUTION);
  TEST_ASSERT_EQUAL_INT(
      0, hal_mock_pwm_freq_get_value(vp37BenchQuantityChannel()));
}

void test_drive_enable_is_the_gpio_state(void) {
  (void)vp37BenchAdapterStart(&s_pump);
  const VP37Callbacks *cb = vp37BenchCallbacks();
  cb->setDriveEnabled(true);
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(PIN_VP37_ENABLE));
  TEST_ASSERT_TRUE(cb->driveEnabled());
  cb->setDriveEnabled(false);
  TEST_ASSERT_FALSE(hal_mock_gpio_get_state(PIN_VP37_ENABLE));
  TEST_ASSERT_FALSE(cb->driveEnabled());
}

void test_restart_of_a_running_pump_leaves_the_drive_alone(void) {
  (void)vp37BenchAdapterStart(&s_pump);
  // The running state needs bench hardware; the test takes it directly.
  s_pump.vp37Initialized = true;
  s_pump.feedback.calibrationDone = true;
  const VP37Callbacks *cb = vp37BenchCallbacks();
  cb->setDriveEnabled(true);
  cb->writeQuantityPwm(500);

  TEST_ASSERT_EQUAL_INT(VP37_INIT_ALREADY_INITIALIZED,
                        vp37BenchAdapterStart(&s_pump));
  // The drive stayed untouched: enable high, the command still on the pin.
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(PIN_VP37_ENABLE));
  TEST_ASSERT_EQUAL_INT(
      VP37_PWM_RESOLUTION - 500,
      hal_mock_pwm_freq_get_value(vp37BenchQuantityChannel()));
}

void test_restart_does_not_exhaust_the_pwm_pool(void) {
  // Reinitialization replaces the channels; without the destroy calls the
  // mock pool drains after HAL_PWM_FREQ_MAX_CHANNELS starts.
  for (unsigned int i = 0; i <= HAL_PWM_FREQ_MAX_CHANNELS; ++i) {
    const VP37InitStatus status = vp37BenchAdapterStart(&s_pump);
    TEST_ASSERT_EQUAL_INT(VP37_INIT_BASELINE_NOT_READY, status);
    (void)VP37_currentScanStop();
  }
}

void test_adjustometer_transfer_reads_registers_over_the_bus(void) {
  (void)vp37BenchAdapterStart(&s_pump);
  const VP37Callbacks *cb = vp37BenchCallbacks();
  const uint8_t frame[4] = {0x12, 0x34, 0x56, 0x78};
  hal_mock_i2c_inject_rx_bus(0, frame, (int)sizeof(frame));
  uint8_t data[4] = {0};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        cb->adjustometerTransfer(0x02U, data, sizeof(data)));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(frame, data, sizeof(frame));
  TEST_ASSERT_EQUAL_UINT16(ADJUSTOMETER_I2C_ADDR,
                           hal_mock_i2c_get_last_addr_bus(0));
  // A busy bus surfaces as an error, like on the ECU; the module treats it
  // as a failed read and keeps its last frame.
  hal_mock_i2c_set_busy(true);
  TEST_ASSERT_NOT_EQUAL(HAL_OK,
                        cb->adjustometerTransfer(0x02U, data, sizeof(data)));
}

void test_demand_percent_follows_the_pot(void) {
  hal_mock_adc_inject(VP37_SCAN_AUX_ADC_PIN, 0);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, vp37BenchDemandPercent());
  hal_mock_adc_inject(VP37_SCAN_AUX_ADC_PIN, 4095);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, vp37BenchDemandPercent());
  hal_mock_adc_inject(VP37_SCAN_AUX_ADC_PIN, 2048);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 50.0f, vp37BenchDemandPercent());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_callbacks_cover_every_board_service);
  RUN_TEST(test_start_without_adjustometer_ends_baseline_not_ready);
  RUN_TEST(test_pwm_writes_are_active_low_like_the_ecu);
  RUN_TEST(test_drive_enable_is_the_gpio_state);
  RUN_TEST(test_restart_of_a_running_pump_leaves_the_drive_alone);
  RUN_TEST(test_restart_does_not_exhaust_the_pwm_pool);
  RUN_TEST(test_adjustometer_transfer_reads_registers_over_the_bus);
  RUN_TEST(test_demand_percent_follows_the_pot);
  return UNITY_END();
}
