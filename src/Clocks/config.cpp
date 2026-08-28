#include "config.h"

#include <hal/serial/hal_serial.h>
#include <hal/serial/hal_serial_commands.h>
#include <hal/serial/hal_serial_session.h>

#include "../common/scDefinitions/sc_command_handlers.h"
#include "../common/scDefinitions/sc_fiesta_module_tokens.h"
#include "../common/scDefinitions/sc_param_types.h"
#include "../common/scDefinitions/sc_protocol.h"
#include "../common/scDefinitions/sc_session_vocabulary.h"

static hal_serial_session_t s_configSession;
static hal_serial_commands_t s_serialCommands;
static sc_command_service_t s_commandService;

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
  if (s_serialCommands.initialized) {
    const hal_status_t detachStatus =
        hal_serial_commands_deinit(&s_serialCommands);
    if (detachStatus != HAL_OK) {
      hal_derr("Clocks SC adapter detach failed: %s",
               hal_status_to_string(detachStatus));
      return;
    }
  }
  if (s_commandService.initialized) {
    const hal_status_t serviceStatus =
        sc_command_service_deinit(&s_commandService);
    if (serviceStatus != HAL_OK) {
      hal_derr("Clocks SC service detach failed: %s",
               hal_status_to_string(serviceStatus));
      return;
    }
  }

  hal_serial_session_init_with_vocabulary(&s_configSession,
                                          SC_MODULE_TOKEN_CLOCKS, FW_VERSION,
                                          BUILD_ID, &fiesta_default_vocabulary);

  sc_command_service_config_t serviceConfig = {};
  serviceConfig.module_token = SC_MODULE_TOKEN_CLOCKS;
  serviceConfig.firmware_version = FW_VERSION;
  serviceConfig.build_id = BUILD_ID;
  serviceConfig.params = k_clocks_params;
  serviceConfig.param_count = k_clocks_params_count;
  serviceConfig.active_values = &k_clocks_values;
  serviceConfig.allowed_sources =
      HAL_COMMAND_SOURCE_MASK(HAL_COMMAND_SOURCE_SERIAL_SESSION);

  hal_status_t status =
      sc_command_service_init(&s_commandService, nullptr, &serviceConfig);
  if (status == HAL_OK) {
    hal_serial_commands_config_t adapterConfig =
        hal_serial_commands_config_defaults(&s_configSession);
    adapterConfig.router = sc_command_service_router(&s_commandService);
    adapterConfig.command_prefix = SC_COMMAND_PREFIX;
    adapterConfig.formatter = sc_command_format_serial_response;
    adapterConfig.allow_inactive = sc_command_allow_inactive_reboot;
    adapterConfig.fallback = sc_command_reply_legacy_unknown;
    adapterConfig.fallback_user = &s_configSession;
    status = hal_serial_commands_init(&s_serialCommands, &adapterConfig);
  }
  if (status != HAL_OK) {
    if (s_commandService.initialized) {
      (void)sc_command_service_deinit(&s_commandService);
    }
    hal_derr("Clocks SC adapter init failed: %s", hal_status_to_string(status));
  }
}

void configSessionTick(void) {
  hal_serial_session_poll(&s_configSession);
  sc_command_service_process_deferred(&s_commandService);
  hal_debug_set_muted(hal_serial_session_is_active(&s_configSession));
}

bool configSessionActive(void) {
  return hal_serial_session_is_active(&s_configSession);
}

uint32_t configSessionId(void) {
  return hal_serial_session_id(&s_configSession);
}
