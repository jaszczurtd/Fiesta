#include "config.h"

#include <hal/serial/hal_serial.h>
#include <hal/serial/hal_serial_session.h>

#include "../common/scDefinitions/sc_command_handlers.h"
#include "../common/scDefinitions/sc_fiesta_module_tokens.h"
#include "../common/scDefinitions/sc_param_types.h"
#include "../common/scDefinitions/sc_protocol.h"

static sc_config_session_t s_sc;

/* Read-only parameter catalog. Compile-time intervals exposed for the
 * configurator host catalog browser. min/max are the validation bounds
 * that would apply if/when these become writable. */
typedef struct {
  int16_t oil_pressure_read_interval_ms;
  int16_t thermocouple_read_interval_ms;
} oas_values_t;

static const oas_values_t k_oas_values = {
    .oil_pressure_read_interval_ms = (int16_t)OIL_PRESSURE_READ_INTERVAL,
    .thermocouple_read_interval_ms = (int16_t)THERMOCOUPLE_READ_INTERVAL,
};

static const sc_param_descriptor_t k_oas_params[] = {
    SC_PARAM_SCALAR_I16_RO_NOT_PERSISTED(
        "oil_pressure_read_interval_ms", oas_values_t,
        oil_pressure_read_interval_ms, 50, 500,
        (int16_t)OIL_PRESSURE_READ_INTERVAL, 1, "sampling"),
    SC_PARAM_SCALAR_I16_RO_NOT_PERSISTED(
        "thermocouple_read_interval_ms", oas_values_t,
        thermocouple_read_interval_ms, 500, 5000,
        (int16_t)THERMOCOUPLE_READ_INTERVAL, 1, "sampling"),
};
static const size_t k_oas_params_count = COUNTOF(k_oas_params);

void configSessionInit(void) {
  if (sc_config_session_stop(&s_sc, "OilAndSpeed") != HAL_OK) {
    return;
  }

  sc_command_service_config_t serviceConfig = {};
  serviceConfig.module_token = SC_MODULE_TOKEN_OIL_AND_SPEED;
  serviceConfig.firmware_version = FW_VERSION;
  serviceConfig.build_id = BUILD_ID;
  serviceConfig.params = k_oas_params;
  serviceConfig.param_count = k_oas_params_count;
  serviceConfig.active_values = &k_oas_values;
  serviceConfig.allowed_sources =
      HAL_COMMAND_SOURCE_MASK(HAL_COMMAND_SOURCE_SERIAL_SESSION);
  (void)sc_config_session_start(&s_sc, &serviceConfig, nullptr, nullptr,
                                "OilAndSpeed");
}

void configSessionTick(void) {
  hal_debug_set_muted(sc_config_session_poll(&s_sc));
}

bool configSessionActive(void) {
  return hal_serial_session_is_active(&s_sc.session);
}

uint32_t configSessionId(void) { return hal_serial_session_id(&s_sc.session); }
