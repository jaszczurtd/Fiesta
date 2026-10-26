#include "egt.h"

#include <JaszczurHAL.h>

#include <math.h>

typedef struct {
  uint8_t address;
  hal_thermocouple_t handle;
  uint32_t lastAttemptMs; // a missing converter is looked for again
  uint32_t retryDelayMs;  // this long after lastAttemptMs
  uint8_t failedReads;
} egt_channel_t;

static egt_channel_t channels[EGT_CHANNELS] = {
    {JH_HW_NODE_EGT_PRE_DPF_ADDRESS, NULL, 0u, 0u, 0u},
    {JH_HW_NODE_EGT_MID_DPF_ADDRESS, NULL, 0u, 0u, 0u}};

static void attachChannel(int index, uint32_t nowMs) {
  egt_channel_t *channel = &channels[index];

  hal_thermocouple_config_t egt_cfg = {};
  egt_cfg.chip = HAL_THERMOCOUPLE_CHIP_MCP9600;
  egt_cfg.bus.i2c.sda_pin = JH_HW_BUS_I2C_PIN_SDA;
  egt_cfg.bus.i2c.scl_pin = JH_HW_BUS_I2C_PIN_SCL;
  egt_cfg.bus.i2c.clock_hz = JH_HW_BUS_I2C_FREQUENCY_HZ;
  egt_cfg.bus.i2c.i2c_bus = JH_HW_BUS_I2C_INDEX;
  egt_cfg.bus.i2c.i2c_addr = channel->address;

  hal_thermocouple_t handle = NULL;
  channel->lastAttemptMs = nowMs;
  const hal_status_t st = hal_thermocouple_init_ex(&egt_cfg, &handle);
  if (st != HAL_OK || handle == NULL) {
    // The two channels retry half a period apart.
    channel->retryDelayMs =
        THERMOCOUPLE_RETRY_INTERVAL +
        (uint32_t)index * (THERMOCOUPLE_RETRY_INTERVAL / 2u);
    return;
  }
  hal_thermocouple_set_type(handle, HAL_THERMOCOUPLE_TYPE_K);
  hal_thermocouple_set_adc_resolution(handle, HAL_THERMOCOUPLE_ADC_RES_18);
  hal_thermocouple_set_ambient_resolution(handle,
                                          HAL_THERMOCOUPLE_AMBIENT_RES_0_0625);
  hal_thermocouple_set_filter(handle, 3);
  hal_thermocouple_enable(handle, true);
  channel->handle = handle;
  channel->failedReads = 0u;
  deb("[EGT] MCP9600 0x%02x OK", channel->address);
}

static void dropChannel(int index, uint32_t nowMs) {
  egt_channel_t *channel = &channels[index];
  hal_thermocouple_deinit(channel->handle);
  channel->handle = NULL;
  channel->lastAttemptMs = nowMs;
  channel->retryDelayMs = THERMOCOUPLE_RETRY_INTERVAL;
  derr("[EGT] MCP9600 0x%02x lost", channel->address);
}

void egtInit(void) {
  const uint32_t now = hal_millis();
  for (int index = 0; index < EGT_CHANNELS; index++) {
    channels[index].handle = NULL;
    attachChannel(index, now);
    if (channels[index].handle == NULL) {
      derr("[EGT] MCP9600 0x%02x not found", channels[index].address);
    }
  }
}

void egtRead(clocks_local_snapshot_t *local, uint32_t nowMs) {
  for (int index = 0; index < EGT_CHANNELS; index++) {
    egt_channel_t *channel = &channels[index];
    local->egtValid[index] = false;
    local->egtC[index] = 0.0f;

    if (channel->handle == NULL) {
      if (hal_elapsed_u32(nowMs, channel->lastAttemptMs,
                          channel->retryDelayMs)) {
        attachChannel(index, nowMs);
      }
      continue;
    }

    float celsius = NAN;
    const hal_status_t st = hal_thermocouple_read_ex(channel->handle, &celsius);
    if (st == HAL_OK && isfinite(celsius)) {
      channel->failedReads = 0u;
      local->egtValid[index] = true;
      local->egtC[index] = celsius;
      local->egtUpdatedMs[index] = nowMs;
    } else if (st == HAL_EHW) {
      // The converter answers but its thermocouple input is out of range,
      // open or shorted: no measurement, the converter stays.
      channel->failedReads = 0u;
    } else if (++channel->failedReads >= THERMOCOUPLE_FAILED_READS_LIMIT) {
      dropChannel(index, nowMs);
    }
  }
}
