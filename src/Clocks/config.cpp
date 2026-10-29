#include "config.h"
#include "../common/scDefinitions/sc_clocks_measurements.h"
#include "clocksState.h"
#include <hal/core/hal_math.h>
#include <math.h>

#include <hal/serial/hal_serial.h>
#include <hal/serial/hal_serial_session.h>

#include "../common/scDefinitions/sc_command_handlers.h"
#include "../common/scDefinitions/sc_fiesta_module_tokens.h"
#include "../common/scDefinitions/sc_param_types.h"
#include "../common/scDefinitions/sc_protocol.h"

static sc_config_session_t s_sc;

/* Read-only parameter catalog for the configurator host.
 *
 * These are compile-time thresholds today (no runtime calibration layer
 * on Clocks yet, unlike ECU's `ecuParams`). They are exposed read-only +
 * not-persisted so the desktop catalog browser is meaningful for this
 * module; min/max are the validation bounds that would apply if/when
 * this becomes writable. */
typedef struct {
  int16_t coolant_warn_c;
  int16_t coolant_max_c;
  int16_t oil_warn_c;
  int16_t oil_max_c;
  int16_t egt_warn_c;
  int16_t egt_max_c;
  int16_t valid;
  int16_t abs_speed_kmh, abs_valid;
  int16_t oil_pressure_cbar, oil_valid;
  int16_t egt_pre_dpf_c, egt_pre_dpf_valid;
  int16_t egt_mid_dpf_c, egt_mid_dpf_valid;
} clocks_values_t;

static clocks_values_t s_clocks_values = {
    .coolant_warn_c = (int16_t)TEMP_OK_HI,
    .coolant_max_c = (int16_t)TEMP_MAX,
    .oil_warn_c = (int16_t)TEMP_OIL_OK_HI,
    .oil_max_c = (int16_t)TEMP_OIL_MAX,
    .egt_warn_c = (int16_t)TEMP_EGT_OK_HI,
    .egt_max_c = (int16_t)TEMP_EGT_MAX,
    .valid = 0,
    .abs_speed_kmh = 0,
    .abs_valid = 0,
    .oil_pressure_cbar = 0,
    .oil_valid = 0,
    .egt_pre_dpf_c = 0,
    .egt_pre_dpf_valid = 0,
    .egt_mid_dpf_c = 0,
    .egt_mid_dpf_valid = 0,
};

static const sc_param_descriptor_t k_clocks_params[] = {
    SC_PARAM_SCALAR_I16_RO_NOT_PERSISTED("coolant_warn_c", clocks_values_t,
                                         coolant_warn_c, 80, 120,
                                         (int16_t)TEMP_OK_HI, 1, "coolant"),
    SC_PARAM_SCALAR_I16_RO_NOT_PERSISTED("coolant_max_c", clocks_values_t,
                                         coolant_max_c, 100, 140,
                                         (int16_t)TEMP_MAX, 1, "coolant"),
    SC_PARAM_SCALAR_I16_RO_NOT_PERSISTED("oil_warn_c", clocks_values_t,
                                         oil_warn_c, 90, 130,
                                         (int16_t)TEMP_OIL_OK_HI, 1, "oil"),
    SC_PARAM_SCALAR_I16_RO_NOT_PERSISTED("oil_max_c", clocks_values_t,
                                         oil_max_c, 120, 160,
                                         (int16_t)TEMP_OIL_MAX, 1, "oil"),
    SC_PARAM_SCALAR_I16_RO_NOT_PERSISTED("egt_warn_c", clocks_values_t,
                                         egt_warn_c, 700, 1100,
                                         (int16_t)TEMP_EGT_OK_HI, 1, "egt"),
    SC_PARAM_SCALAR_I16_RO_NOT_PERSISTED("egt_max_c", clocks_values_t,
                                         egt_max_c, 1300, 1800,
                                         (int16_t)TEMP_EGT_MAX, 1, "egt"),
    SC_PARAM_SCALAR_I16_RO_NOT_PERSISTED(SC_CLOCKS_ABS_ID, clocks_values_t,
                                         abs_speed_kmh, 0, 32767, 0, 1,
                                         "measurements"),
    SC_PARAM_SCALAR_I16_RO_NOT_PERSISTED(SC_CLOCKS_OIL_ID, clocks_values_t,
                                         oil_pressure_cbar, 0, 32767, 0, 1,
                                         "measurements"),
    SC_PARAM_SCALAR_I16_RO_NOT_PERSISTED(SC_CLOCKS_EGT0_ID, clocks_values_t,
                                         egt_pre_dpf_c, -32768, 32767, 0, 1,
                                         "measurements"),
    SC_PARAM_SCALAR_I16_RO_NOT_PERSISTED(SC_CLOCKS_EGT1_ID, clocks_values_t,
                                         egt_mid_dpf_c, -32768, 32767, 0, 1,
                                         "measurements"),
    SC_PARAM_SCALAR_I16_RO_NOT_PERSISTED(SC_CLOCKS_VALID_ID, clocks_values_t,
                                         valid, 0, SC_CLOCKS_VALID_ALL, 0, 1,
                                         "validity"),
};
static const size_t k_clocks_params_count = COUNTOF(k_clocks_params);

static int16_t measurement(float value) {
  return isfinite(value)
             ? (int16_t)hal_constrain(value, (float)INT16_MIN, (float)INT16_MAX)
             : 0;
}

static void refreshValues(void *user) {
  (void)user;
  clocks_local_snapshot_t local = {};
  clocksStateCopyLocal(&local);
  s_clocks_values.abs_valid = isAbsSpeedValid();
  s_clocks_values.abs_speed_kmh =
      s_clocks_values.abs_valid ? measurement((float)getCurrentCarSpeed()) : 0;
  s_clocks_values.oil_valid = local.oilValid && isfinite(local.oilBar);
  s_clocks_values.oil_pressure_cbar =
      s_clocks_values.oil_valid ? measurement(local.oilBar * 100.0f) : 0;
  s_clocks_values.egt_pre_dpf_valid =
      local.egtValid[EGT_PRE_DPF] && isfinite(local.egtC[EGT_PRE_DPF]);
  s_clocks_values.egt_pre_dpf_c = s_clocks_values.egt_pre_dpf_valid
                                      ? measurement(local.egtC[EGT_PRE_DPF])
                                      : 0;
  s_clocks_values.egt_mid_dpf_valid =
      local.egtValid[EGT_IN_DPF] && isfinite(local.egtC[EGT_IN_DPF]);
  s_clocks_values.egt_mid_dpf_c = s_clocks_values.egt_mid_dpf_valid
                                      ? measurement(local.egtC[EGT_IN_DPF])
                                      : 0;
  s_clocks_values.valid =
      (s_clocks_values.abs_valid ? SC_CLOCKS_VALID_ABS : 0) |
      (s_clocks_values.oil_valid ? SC_CLOCKS_VALID_OIL : 0) |
      (s_clocks_values.egt_pre_dpf_valid ? SC_CLOCKS_VALID_EGT0 : 0) |
      (s_clocks_values.egt_mid_dpf_valid ? SC_CLOCKS_VALID_EGT1 : 0);
}

static void readGpsForSession(void *user, sc_command_gps_snapshot_t *out) {
  (void)user;
  if (out == nullptr)
    return;
  clocks_local_snapshot_t local = {};
  clocksStateCopyLocal(&local);
  *out = {};
  out->available = local.gps.available;
  if (out->available) {
    out->lat_e6 = clocksGpsMicrodegrees(local.gps.latitude);
    out->lon_e6 = clocksGpsMicrodegrees(local.gps.longitude);
    const uint16_t speed = clocksGpsSpeedX10(&local.gps);
    out->speed_kmh_x10 = speed > INT16_MAX ? INT16_MAX : (int16_t)speed;
    out->epoch = local.gps.epochUtc;
  }
}

void configSessionInit(void) {
  if (sc_config_session_stop(&s_sc, "Clocks") != HAL_OK) {
    return;
  }

  sc_command_service_config_t serviceConfig = {};
  serviceConfig.module_token = SC_MODULE_TOKEN_CLOCKS;
  serviceConfig.firmware_version = FW_VERSION;
  serviceConfig.build_id = BUILD_ID;
  serviceConfig.params = k_clocks_params;
  serviceConfig.param_count = k_clocks_params_count;
  serviceConfig.active_values = &s_clocks_values;
  serviceConfig.read_gps = readGpsForSession;
  serviceConfig.refresh = refreshValues;
  serviceConfig.allowed_sources =
      HAL_COMMAND_SOURCE_MASK(HAL_COMMAND_SOURCE_SERIAL_SESSION);
  (void)sc_config_session_start(&s_sc, &serviceConfig, nullptr, nullptr,
                                "Clocks");
}

void configSessionTick(void) {
  hal_debug_set_muted(sc_config_session_poll(&s_sc));
}

bool configSessionActive(void) {
  return hal_serial_session_is_active(&s_sc.session);
}

uint32_t configSessionId(void) { return hal_serial_session_id(&s_sc.session); }
