#include "config.h"

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
} clocks_values_t;

static const clocks_values_t k_clocks_values = {
    .coolant_warn_c = (int16_t)TEMP_OK_HI,
    .coolant_max_c = (int16_t)TEMP_MAX,
    .oil_warn_c = (int16_t)TEMP_OIL_OK_HI,
    .oil_max_c = (int16_t)TEMP_OIL_MAX,
    .egt_warn_c = (int16_t)TEMP_EGT_OK_HI,
    .egt_max_c = (int16_t)TEMP_EGT_MAX,
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
};
static const size_t k_clocks_params_count = COUNTOF(k_clocks_params);

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
  serviceConfig.active_values = &k_clocks_values;
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
