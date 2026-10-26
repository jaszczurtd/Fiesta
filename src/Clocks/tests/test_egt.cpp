#include "egt.h"
#include "hal/impl/.mock/hal_mock.h"
#include "unity.h"

#include <math.h>
#include <string.h>

// The MCP9600 converters behind the HAL thermocouple API: each one can be
// absent or fail its reads, which the HAL mock does not model.
struct hal_thermocouple_impl_s {
  int channel;
};

static hal_thermocouple_impl_s s_handles[EGT_CHANNELS] = {{EGT_PRE_DPF},
                                                          {EGT_IN_DPF}};
static bool s_present[EGT_CHANNELS];
static bool s_attached[EGT_CHANNELS];
static float s_celsius[EGT_CHANNELS];
static hal_status_t s_readStatus[EGT_CHANNELS];
static int s_initCalls[EGT_CHANNELS];

static int channelOfAddress(uint8_t address) {
  return (address == JH_HW_NODE_EGT_PRE_DPF_ADDRESS) ? EGT_PRE_DPF : EGT_IN_DPF;
}

hal_status_t hal_thermocouple_init_ex(const hal_thermocouple_config_t *cfg,
                                      hal_thermocouple_t *out_handle) {
  const int channel = channelOfAddress(cfg->bus.i2c.i2c_addr);
  s_initCalls[channel]++;
  if (!s_present[channel]) {
    return HAL_EIO;
  }
  s_attached[channel] = true;
  *out_handle = &s_handles[channel];
  return HAL_OK;
}

void hal_thermocouple_deinit(hal_thermocouple_t h) {
  if (h != NULL) {
    s_attached[h->channel] = false;
  }
}

hal_status_t hal_thermocouple_read_ex(hal_thermocouple_t h, float *out_c) {
  *out_c = s_celsius[h->channel];
  return s_readStatus[h->channel];
}

hal_status_t hal_thermocouple_set_type(hal_thermocouple_t,
                                       hal_thermocouple_type_t) {
  return HAL_OK;
}
hal_status_t hal_thermocouple_set_adc_resolution(hal_thermocouple_t,
                                                 hal_thermocouple_adc_res_t) {
  return HAL_OK;
}
hal_status_t
hal_thermocouple_set_ambient_resolution(hal_thermocouple_t,
                                        hal_thermocouple_ambient_res_t) {
  return HAL_OK;
}
hal_status_t hal_thermocouple_set_filter(hal_thermocouple_t, uint8_t) {
  return HAL_OK;
}
hal_status_t hal_thermocouple_enable(hal_thermocouple_t, bool) {
  return HAL_OK;
}

static clocks_local_snapshot_t s_local;

static void start(bool preDpf, bool inDpf, uint32_t nowMs) {
  memset(&s_local, 0, sizeof(s_local));
  memset(s_initCalls, 0, sizeof(s_initCalls));
  memset(s_attached, 0, sizeof(s_attached));
  s_present[EGT_PRE_DPF] = preDpf;
  s_present[EGT_IN_DPF] = inDpf;
  for (int channel = 0; channel < EGT_CHANNELS; channel++) {
    s_celsius[channel] = 20.0f;
    s_readStatus[channel] = HAL_OK;
  }
  hal_mock_set_millis(nowMs);
  egtInit();
}

void setUp(void) {}

void tearDown(void) {}

void test_both_channels_measure_independently_with_negative_values(void) {
  start(true, true, 1000u);
  s_celsius[EGT_PRE_DPF] = 651.5f;
  s_celsius[EGT_IN_DPF] = -12.25f;

  egtRead(&s_local, 2000u);

  TEST_ASSERT_TRUE(s_local.egtValid[EGT_PRE_DPF]);
  TEST_ASSERT_TRUE(s_local.egtValid[EGT_IN_DPF]);
  TEST_ASSERT_EQUAL_FLOAT(651.5f, s_local.egtC[EGT_PRE_DPF]);
  TEST_ASSERT_EQUAL_FLOAT(-12.25f, s_local.egtC[EGT_IN_DPF]);
  TEST_ASSERT_EQUAL_UINT32(2000u, s_local.egtUpdatedMs[EGT_IN_DPF]);
}

void test_missing_converter_leaves_only_its_channel_without_measurement(void) {
  start(false, true, 1000u);

  egtRead(&s_local, 2000u);

  TEST_ASSERT_FALSE(s_local.egtValid[EGT_PRE_DPF]);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s_local.egtC[EGT_PRE_DPF]);
  TEST_ASSERT_TRUE(s_local.egtValid[EGT_IN_DPF]);
}

void test_missing_converter_is_looked_for_again_after_the_retry_interval(void) {
  start(false, false, 1000u);
  TEST_ASSERT_EQUAL_INT(1, s_initCalls[EGT_PRE_DPF]);
  TEST_ASSERT_EQUAL_INT(1, s_initCalls[EGT_IN_DPF]);
  s_present[EGT_PRE_DPF] = true;
  s_present[EGT_IN_DPF] = true;

  egtRead(&s_local, 1000u + THERMOCOUPLE_RETRY_INTERVAL - 1u);
  TEST_ASSERT_EQUAL_INT(1, s_initCalls[EGT_PRE_DPF]);

  egtRead(&s_local, 1000u + THERMOCOUPLE_RETRY_INTERVAL);
  TEST_ASSERT_EQUAL_INT(2, s_initCalls[EGT_PRE_DPF]);
  // The second channel retries half an interval later.
  TEST_ASSERT_EQUAL_INT(1, s_initCalls[EGT_IN_DPF]);

  egtRead(&s_local, 1000u + THERMOCOUPLE_RETRY_INTERVAL +
                        THERMOCOUPLE_RETRY_INTERVAL / 2u);
  TEST_ASSERT_EQUAL_INT(2, s_initCalls[EGT_IN_DPF]);
  TEST_ASSERT_TRUE(s_local.egtValid[EGT_PRE_DPF]);
}

void test_nan_reading_is_no_measurement(void) {
  start(true, true, 1000u);
  s_celsius[EGT_PRE_DPF] = NAN;

  egtRead(&s_local, 2000u);

  TEST_ASSERT_FALSE(s_local.egtValid[EGT_PRE_DPF]);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s_local.egtC[EGT_PRE_DPF]);
  TEST_ASSERT_TRUE(s_local.egtValid[EGT_IN_DPF]);
}

void test_converter_failing_in_a_row_is_dropped_and_searched_again(void) {
  start(true, true, 1000u);
  s_readStatus[EGT_IN_DPF] = HAL_EIO;

  uint32_t now = 2000u;
  for (int read = 1; read < THERMOCOUPLE_FAILED_READS_LIMIT; read++) {
    egtRead(&s_local, now);
    TEST_ASSERT_FALSE(s_local.egtValid[EGT_IN_DPF]);
    TEST_ASSERT_TRUE(s_attached[EGT_IN_DPF]);
    now += THERMOCOUPLE_READ_INTERVAL;
  }
  egtRead(&s_local, now);
  TEST_ASSERT_FALSE(s_attached[EGT_IN_DPF]);
  TEST_ASSERT_TRUE(s_local.egtValid[EGT_PRE_DPF]);

  s_readStatus[EGT_IN_DPF] = HAL_OK;
  egtRead(&s_local, now + THERMOCOUPLE_RETRY_INTERVAL);
  TEST_ASSERT_TRUE(s_attached[EGT_IN_DPF]);
  egtRead(&s_local,
          now + THERMOCOUPLE_RETRY_INTERVAL + THERMOCOUPLE_READ_INTERVAL);
  TEST_ASSERT_TRUE(s_local.egtValid[EGT_IN_DPF]);
}

// The converter answers, but its thermocouple input is out of range, open or
// shorted: no measurement, yet nothing to search for again.
void test_input_fault_is_no_measurement_and_keeps_the_converter(void) {
  start(true, true, 1000u);
  s_celsius[EGT_PRE_DPF] = 400.0f;
  s_readStatus[EGT_PRE_DPF] = HAL_EHW;

  uint32_t now = 2000u;
  for (int read = 0; read < 2 * THERMOCOUPLE_FAILED_READS_LIMIT; read++) {
    egtRead(&s_local, now);
    TEST_ASSERT_FALSE(s_local.egtValid[EGT_PRE_DPF]);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s_local.egtC[EGT_PRE_DPF]);
    TEST_ASSERT_TRUE(s_attached[EGT_PRE_DPF]);
    now += THERMOCOUPLE_READ_INTERVAL;
  }
  TEST_ASSERT_EQUAL_INT(1, s_initCalls[EGT_PRE_DPF]);

  s_readStatus[EGT_PRE_DPF] = HAL_OK;
  egtRead(&s_local, now);
  TEST_ASSERT_TRUE(s_local.egtValid[EGT_PRE_DPF]);
  TEST_ASSERT_EQUAL_FLOAT(400.0f, s_local.egtC[EGT_PRE_DPF]);
}

void test_a_single_failed_read_keeps_the_converter(void) {
  start(true, true, 1000u);
  s_readStatus[EGT_PRE_DPF] = HAL_EIO;
  egtRead(&s_local, 2000u);
  TEST_ASSERT_FALSE(s_local.egtValid[EGT_PRE_DPF]);

  s_readStatus[EGT_PRE_DPF] = HAL_OK;
  egtRead(&s_local, 3000u);
  TEST_ASSERT_TRUE(s_local.egtValid[EGT_PRE_DPF]);
  TEST_ASSERT_EQUAL_INT(1, s_initCalls[EGT_PRE_DPF]);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_both_channels_measure_independently_with_negative_values);
  RUN_TEST(test_missing_converter_leaves_only_its_channel_without_measurement);
  RUN_TEST(test_missing_converter_is_looked_for_again_after_the_retry_interval);
  RUN_TEST(test_nan_reading_is_no_measurement);
  RUN_TEST(test_converter_failing_in_a_row_is_dropped_and_searched_again);
  RUN_TEST(test_a_single_failed_read_keeps_the_converter);
  RUN_TEST(test_input_fault_is_no_measurement_and_keeps_the_converter);
  return UNITY_END();
}
