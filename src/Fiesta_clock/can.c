#include "can.h"

#include "clockTime.h"
#include "config.h"
#include "ds18b20.h"
#include "hardwareConfig.h"
#include "rtcSync.h"

#include "../common/canDefinitions/canDefinitions.h"

#include <JaszczurHAL.h>

#include <string.h>

#define CLOCK_CAN_RETRIES 4
#define CLOCK_CAN_SEND_INTERVAL_MS 1000u

typedef struct {
  hal_can_t handle;
  bool initialized;
  uint8_t frameNumber;
  uint32_t nextSendMs;
  /* The last air temperature frame; its number is not compared. */
  uint8_t airTempSent[CAN_FRAME_MAX_LENGTH];
} clock_can_state_t;

static clock_can_state_t s_clockCanState = {.handle = NULL,
                                            .initialized = false,
                                            .frameNumber = 0u,
                                            .nextSendMs = 0u,
                                            .airTempSent = {0}};

/* The clock receives only the GPS UTC of Clocks, so other traffic cannot
 * fill the receive buffers. */
static hal_status_t clockCanConfigureFilters(void) {
  const hal_can_filter_t filter = {CAN_ID_GPS_UTC_UPDATE, HAL_CAN_STD_ID_MASK,
                                   0u};
  hal_status_t st = HAL_OK;
  for (uint8_t index = 0u; index < HAL_CAN_MAX_FILTERS && st == HAL_OK;
       index++) {
    st = hal_can_set_filter(s_clockCanState.handle, index, &filter);
  }
  return st;
}

hal_status_t clockCanInit(void) {
  rtcSyncInit(hal_millis());
  s_clockCanState.frameNumber = 0u;
  s_clockCanState.nextSendMs = hal_millis() + CLOCK_CAN_SEND_INTERVAL_MS;
  /* Open the MCP2515 bus on its own pins; the HAL would otherwise start
   * SPI 0 lazily on its default pins. */
  hal_gpio_set_mode(JH_HW_NODE_CAN_PIN_CS, HAL_GPIO_OUTPUT);
  hal_gpio_write(JH_HW_NODE_CAN_PIN_CS, true);
  const hal_status_t spiStatus =
      hal_spi_init(JH_HW_BUS_SPI_INDEX, JH_HW_BUS_SPI_PIN_MISO,
                   JH_HW_BUS_SPI_PIN_MOSI, JH_HW_BUS_SPI_PIN_SCK);
  if (spiStatus != HAL_OK) {
    s_clockCanState.initialized = false;
    derr("SPI init failed, the clock runs without CAN: %s",
         hal_status_to_string(spiStatus));
    return spiStatus;
  }
  hal_can_config_t canCfg = JH_HW_NODE_CAN_CONFIG_INIT;

  hal_status_t st = hal_can_create_with_retry(&canCfg, JH_HW_NODE_CAN_PIN_INT,
                                              NULL, CLOCK_CAN_RETRIES - 1, NULL,
                                              &s_clockCanState.handle);
  if (st == HAL_OK) {
    st = clockCanConfigureFilters();
    if (st != HAL_OK) {
      hal_can_destroy(s_clockCanState.handle);
      s_clockCanState.handle = NULL;
    }
  }
  s_clockCanState.initialized = (st == HAL_OK);
  if (st != HAL_OK) {
    derr("CAN init failed, the clock runs without CAN: %s",
         hal_status_to_string(st));
  }
  return st;
}

static void clockCanOnFrame(uint32_t id, uint8_t len, const uint8_t *data) {
  if (id == CAN_ID_GPS_UTC_UPDATE) {
    rtcSyncReceive(data, len);
  }
}

/* The RTC time as UTC; without trusted UTC the fields stay zero. */
static void clockCanSendRtc(void) {
  uint32_t epoch = 0u;
  bool trusted = false;
  (void)clockTimeReadUtc(&epoch, &trusted);

  uint8_t out[CAN_FRAME_MAX_LENGTH] = {0};
  out[CAN_FRAME_NUMBER] = s_clockCanState.frameNumber++;
  /* A date the frame format cannot hold goes out without integrity. */
  if (trusted &&
      fiesta_can_put_utc_datetime(&out[CAN_FRAME_RTC_UPDATE_DT_HI], epoch)) {
    out[CAN_FRAME_RTC_UPDATE_SECOND] = (uint8_t)(epoch % 60u);
    out[CAN_FRAME_RTC_UPDATE_INTEGRITY] = 1u;
  }

  const hal_status_t st = hal_can_send(
      s_clockCanState.handle, CAN_ID_RTC_UPDATE, CAN_FRAME_MAX_LENGTH, out);
  if (st != HAL_OK) {
    derr_limited("can", "RTC frame not sent: %s", hal_status_to_string(st));
  }
}

/* Both DS18B20 readings in 0.1 °C; a sensor without a fresh reading has its
 * valid bit clear and carries 0. */
static void clockCanPutAirTemp(uint8_t out[CAN_FRAME_MAX_LENGTH]) {
  if (fiesta_can_put_temp_tenths_i16(
          &out[CAN_FRAME_AIR_TEMP_INSIDE_HI],
          ds18b20_fresh_temp(JH_HW_NODE_TEMP_INSIDE_PIN_DATA,
                             CFG_TEMP_MAX_AGE_MS))) {
    out[CAN_FRAME_AIR_TEMP_STATUS] |= CAN_AIR_TEMP_STATUS_INSIDE_VALID;
  }
  if (fiesta_can_put_temp_tenths_i16(
          &out[CAN_FRAME_AIR_TEMP_OUTSIDE_HI],
          ds18b20_fresh_temp(JH_HW_NODE_TEMP_OUTSIDE_PIN_DATA,
                             CFG_TEMP_MAX_AGE_MS))) {
    out[CAN_FRAME_AIR_TEMP_STATUS] |= CAN_AIR_TEMP_STATUS_OUTSIDE_VALID;
  }
}

static void clockCanSendAirTemp(uint8_t out[CAN_FRAME_MAX_LENGTH]) {
  out[CAN_FRAME_NUMBER] = s_clockCanState.frameNumber++;
  memcpy(s_clockCanState.airTempSent, out, CAN_FRAME_MAX_LENGTH);
  const hal_status_t st =
      hal_can_send(s_clockCanState.handle, CAN_ID_AIR_TEMP_UPDATE,
                   CAN_FRAME_MAX_LENGTH, out);
  if (st != HAL_OK) {
    derr_limited("can", "air temperature frame not sent: %s",
                 hal_status_to_string(st));
  }
}

void clockCanTick(bool timeEditActive) {
  const uint32_t nowMs = hal_millis();
  if (s_clockCanState.initialized) {
    const hal_status_t rx =
        hal_can_process_all(s_clockCanState.handle, clockCanOnFrame, NULL);
    if (rx != HAL_OK) {
      derr_limited("can", "receive failed: %s", hal_status_to_string(rx));
    }
  }
  /* Also without CAN: it finishes an unfinished change to UTC. */
  rtcSyncTick(nowMs, timeEditActive);
  if (!s_clockCanState.initialized) {
    return;
  }

  uint8_t airTemp[CAN_FRAME_MAX_LENGTH] = {0};
  clockCanPutAirTemp(airTemp);
  const bool airTempChanged =
      memcmp(&airTemp[CAN_FRAME_NUMBER + 1],
             &s_clockCanState.airTempSent[CAN_FRAME_NUMBER + 1],
             CAN_FRAME_MAX_LENGTH - (CAN_FRAME_NUMBER + 1)) != 0;

  if ((int32_t)(nowMs - s_clockCanState.nextSendMs) >= 0) {
    s_clockCanState.nextSendMs = nowMs + CLOCK_CAN_SEND_INTERVAL_MS;
    clockCanSendRtc();
    clockCanSendAirTemp(airTemp);
  } else if (airTempChanged) {
    clockCanSendAirTemp(airTemp);
  }
}
