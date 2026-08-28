
#include "config.h"
#include "ecuPersistence.h"
#include "ecu_unit_testing.h"
#include "gps.h"
#include "tests.h"

#include <hal/serial/hal_serial.h>
#include <hal/serial/hal_serial_commands.h>
#include <hal/serial/hal_serial_session.h>
#include <hal/storage/hal_kv.h>
#include <hal/system/hal_system.h>
#include <stddef.h>
#include <stdint.h>

#include "../common/scDefinitions/sc_command_handlers.h"
#include "../common/scDefinitions/sc_fiesta_module_tokens.h"
#include "../common/scDefinitions/sc_param_handlers.h"
#include "../common/scDefinitions/sc_param_types.h"
#include "../common/scDefinitions/sc_protocol.h"
#include "../common/scDefinitions/sc_session_vocabulary.h"

const char *err = "ERR";

#define ECU_PARAMS_SCHEMA_V1 1u
#define ECU_PARAMS_SCHEMA_V2 2u
#define ECU_PARAMS_BLOB_SIZE_V1 16u
#define ECU_PARAMS_BLOB_SIZE_V2 18u
#define ECU_PARAMS_BLOB_KEY 0xDA10u

/* Wire-visible parameter catalogue. R1.2 routes every SC reply for these
 * params through the descriptor-driven helpers in src/common/scDefinitions/.
 * The schema_since column drives V1 (5 fields, 16-byte blob) vs V2 (6
 * fields, 18-byte blob) backwards compatibility. */
static const sc_param_descriptor_t k_ecu_params[] = {
    SC_PARAM_SCALAR_I16("fan_coolant_start_c", ecu_params_values_t,
                        fanCoolantStartC, ECU_PARAMS_COOLANT_START_MIN,
                        ECU_PARAMS_COOLANT_START_MAX, TEMP_FAN_START, 1,
                        "cooling_fan"),
    SC_PARAM_SCALAR_I16("fan_coolant_stop_c", ecu_params_values_t,
                        fanCoolantStopC, ECU_PARAMS_COOLANT_STOP_MIN,
                        ECU_PARAMS_COOLANT_STOP_MAX, TEMP_FAN_STOP, 1,
                        "cooling_fan"),
    SC_PARAM_SCALAR_I16("fan_air_start_c", ecu_params_values_t, fanAirStartC,
                        ECU_PARAMS_AIR_START_MIN, ECU_PARAMS_AIR_START_MAX,
                        AIR_TEMP_FAN_START, 1, "intercooler_fan"),
    SC_PARAM_SCALAR_I16("fan_air_stop_c", ecu_params_values_t, fanAirStopC,
                        ECU_PARAMS_AIR_STOP_MIN, ECU_PARAMS_AIR_STOP_MAX,
                        AIR_TEMP_FAN_STOP, 1, "intercooler_fan"),
    SC_PARAM_SCALAR_I16("heater_stop_c", ecu_params_values_t, heaterStopC,
                        ECU_PARAMS_HEATER_STOP_MIN, ECU_PARAMS_HEATER_STOP_MAX,
                        TEMP_HEATER_STOP, 1, "engine_heater"),
    SC_PARAM_SCALAR_I16("nominal_rpm", ecu_params_values_t, nominalRpm,
                        ECU_PARAMS_NOMINAL_RPM_MIN, ECU_PARAMS_NOMINAL_RPM_MAX,
                        NOMINAL_RPM_VALUE, 2, "idle"),
};
static const size_t k_ecu_params_count = COUNTOF(k_ecu_params);

/* The adapter attaches the shared SC router service to this serial session. */
static hal_serial_session_t s_configSession;
static hal_serial_commands_t s_serialCommands;
static sc_command_service_t s_commandService;

static ecu_params_values_t s_active = {.fanCoolantStartC = TEMP_FAN_START,
                                       .fanCoolantStopC = TEMP_FAN_STOP,
                                       .fanAirStartC = AIR_TEMP_FAN_START,
                                       .fanAirStopC = AIR_TEMP_FAN_STOP,
                                       .heaterStopC = TEMP_HEATER_STOP,
                                       .nominalRpm = NOMINAL_RPM_VALUE};

static ecu_params_values_t s_staging = {.fanCoolantStartC = TEMP_FAN_START,
                                        .fanCoolantStopC = TEMP_FAN_STOP,
                                        .fanAirStartC = AIR_TEMP_FAN_START,
                                        .fanAirStopC = AIR_TEMP_FAN_STOP,
                                        .heaterStopC = TEMP_HEATER_STOP,
                                        .nominalRpm = NOMINAL_RPM_VALUE};

static bool s_initialized = false;
static bool s_loadPending = true;
static uint32_t s_lastLoadAttemptMs = 0u;

#define ECU_PARAMS_LOAD_RETRY_MS 1000u

TESTABLE_STATIC void ecuParamsLoadDefaults(ecu_params_values_t *outValues) {
  if (outValues == NULL) {
    return;
  }
  (void)sc_param_load_defaults(k_ecu_params, k_ecu_params_count, outValues);
}

TESTABLE_STATIC bool ecuParamsValidate(const ecu_params_values_t *candidate,
                                       const char **reason) {
  if (reason != NULL) {
    *reason = NULL;
  }

  if (candidate == NULL) {
    if (reason != NULL) {
      *reason = "null";
    }
    return false;
  }

  if (candidate->fanCoolantStartC < ECU_PARAMS_COOLANT_START_MIN ||
      candidate->fanCoolantStartC > ECU_PARAMS_COOLANT_START_MAX) {
    if (reason != NULL) {
      *reason = "fan_coolant_start_range";
    }
    return false;
  }

  if (candidate->fanCoolantStopC < ECU_PARAMS_COOLANT_STOP_MIN ||
      candidate->fanCoolantStopC > ECU_PARAMS_COOLANT_STOP_MAX) {
    if (reason != NULL) {
      *reason = "fan_coolant_stop_range";
    }
    return false;
  }

  if (candidate->fanAirStartC < ECU_PARAMS_AIR_START_MIN ||
      candidate->fanAirStartC > ECU_PARAMS_AIR_START_MAX) {
    if (reason != NULL) {
      *reason = "fan_air_start_range";
    }
    return false;
  }

  if (candidate->fanAirStopC < ECU_PARAMS_AIR_STOP_MIN ||
      candidate->fanAirStopC > ECU_PARAMS_AIR_STOP_MAX) {
    if (reason != NULL) {
      *reason = "fan_air_stop_range";
    }
    return false;
  }

  if (candidate->heaterStopC < ECU_PARAMS_HEATER_STOP_MIN ||
      candidate->heaterStopC > ECU_PARAMS_HEATER_STOP_MAX) {
    if (reason != NULL) {
      *reason = "heater_stop_range";
    }
    return false;
  }

  if (candidate->nominalRpm < ECU_PARAMS_NOMINAL_RPM_MIN ||
      candidate->nominalRpm > ECU_PARAMS_NOMINAL_RPM_MAX) {
    if (reason != NULL) {
      *reason = "nominal_rpm_range";
    }
    return false;
  }

  if (candidate->fanCoolantStopC >= candidate->fanCoolantStartC) {
    if (reason != NULL) {
      *reason = "fan_coolant_hysteresis";
    }
    return false;
  }

  if (candidate->fanAirStopC >= candidate->fanAirStartC) {
    if (reason != NULL) {
      *reason = "fan_air_hysteresis";
    }
    return false;
  }

  if (candidate->heaterStopC >= candidate->fanCoolantStartC) {
    if (reason != NULL) {
      *reason = "heater_vs_fan_order";
    }
    return false;
  }

  return true;
}

#ifdef UNIT_TEST
TESTABLE_STATIC bool ecuParamsStage(const ecu_params_values_t *candidate,
                                    const char **reason) {
  if (!ecuParamsValidate(candidate, reason)) {
    return false;
  }
  s_staging = *candidate;
  return true;
}
#endif

TESTABLE_STATIC void ecuParamsApply(void) { s_active = s_staging; }

static hal_status_t ecuParamsLoadPersistedEx(ecu_params_values_t *outValues) {
  if (outValues == NULL) {
    return HAL_EINVAL;
  }

  uint16_t blobLen = 0u;
  hal_status_t status =
      hal_kv_get_blob_ex(ECU_PARAMS_BLOB_KEY, NULL, 0u, &blobLen);
  if (status != HAL_OK) {
    return status;
  }
  if (blobLen != ECU_PARAMS_BLOB_SIZE_V1 &&
      blobLen != ECU_PARAMS_BLOB_SIZE_V2) {
    return HAL_EPROTO;
  }

  uint8_t blob[ECU_PARAMS_BLOB_SIZE_V2] = {0};
  status = hal_kv_get_blob_ex(ECU_PARAMS_BLOB_KEY, blob, (uint16_t)sizeof(blob),
                              &blobLen);
  if (status != HAL_OK) {
    return status;
  }

  /* Pre-load defaults so V2-only fields keep a sane value when a V1
   * blob (5 fields) is being decoded into a V2-shaped struct. */
  ecu_params_values_t decoded;
  ecuParamsLoadDefaults(&decoded);

  uint16_t schema = 0u;
  if (!sc_param_blob_decode(k_ecu_params, k_ecu_params_count, &decoded, blob,
                            blobLen, &schema)) {
    return HAL_EPROTO;
  }

  if (!ecuParamsValidate(&decoded, NULL)) {
    return HAL_EPROTO;
  }

  *outValues = decoded;
  return HAL_OK;
}

#ifdef UNIT_TEST
TESTABLE_STATIC bool ecuParamsLoadPersisted(ecu_params_values_t *outValues) {
  return ecuParamsLoadPersistedEx(outValues) == HAL_OK;
}
#endif

/* Encode the descriptor-driven blob and write it to KV. */
typedef struct {
  const uint8_t *blob;
  uint16_t size;
} ecu_params_persist_context_t;

static hal_status_t ecuParamsPersistBlob(const void *user) {
  const ecu_params_persist_context_t *context =
      (const ecu_params_persist_context_t *)user;
  if (context == NULL) {
    return HAL_EINVAL;
  }
  return hal_kv_set_blob_ex(ECU_PARAMS_BLOB_KEY, context->blob, context->size);
}

TESTABLE_STATIC hal_status_t
ecuParamsPersist(const ecu_params_values_t *values) {
  if (!ecuParamsValidate(values, NULL)) {
    return HAL_EINVAL;
  }

  uint8_t blob[ECU_PARAMS_BLOB_SIZE_V2] = {0};
  const size_t written =
      sc_param_blob_encode(k_ecu_params, k_ecu_params_count, values,
                           ECU_PARAMS_SCHEMA_V2, blob, sizeof(blob));
  if (written != ECU_PARAMS_BLOB_SIZE_V2) {
    return HAL_EIO;
  }

  ecu_params_persist_context_t context = {
      .blob = blob,
      .size = (uint16_t)sizeof(blob),
  };
  hal_status_t resumeStatus = HAL_NONE;
  const hal_status_t persistStatus =
      ecuPersistenceExecute(ecuParamsPersistBlob, &context, &resumeStatus);
  if (persistStatus != HAL_OK) {
    hal_derr("ECU params persistence failed: %s",
             hal_status_to_string(persistStatus));
  } else if (resumeStatus != HAL_OK) {
    hal_derr("ECU params committed; GPS resume queued: %s",
             hal_status_to_string(resumeStatus));
  }
  return persistStatus;
}

#ifdef UNIT_TEST
TESTABLE_STATIC uint16_t ecuParamsBlobKeyForTest(void) {
  return ECU_PARAMS_BLOB_KEY;
}

TESTABLE_STATIC void ecuParamsResetRuntimeStateForTest(void) {
  ecuParamsLoadDefaults(&s_active);
  s_staging = s_active;
  s_initialized = false;
  s_loadPending = true;
  s_lastLoadAttemptMs = 0u;
}
#endif

static void ecuParamsTryLoadPersisted(void) {
  ecu_params_values_t loaded = {0};
  const hal_status_t loadStatus = ecuParamsLoadPersistedEx(&loaded);
  s_lastLoadAttemptMs = hal_millis();

  if (loadStatus == HAL_OK) {
    s_active = loaded;
    s_staging = loaded;
    s_loadPending = false;
    return;
  }
  if (loadStatus == HAL_ENOENT || loadStatus == HAL_EPROTO) {
    s_staging = s_active;
    s_loadPending = false;
    return;
  }
  s_loadPending = true;
}

void ecuParamsInit(void) {
  if (!s_initialized) {
    ecuParamsLoadDefaults(&s_staging);
    ecuParamsApply();
    s_initialized = true;
    s_loadPending = true;
  }

  ecuParamsTryLoadPersisted();
}

void ecuParamsPoll(void) {
  if (!s_initialized) {
    ecuParamsInit();
    return;
  }
  const uint32_t now = hal_millis();
  if (s_loadPending &&
      (uint32_t)(now - s_lastLoadAttemptMs) >= ECU_PARAMS_LOAD_RETRY_MS) {
    ecuParamsTryLoadPersisted();
  }
}

const ecu_params_values_t *ecuParamsActive(void) { return &s_active; }

int16_t ecuParamsFanCoolantStart(void) { return s_active.fanCoolantStartC; }

int16_t ecuParamsFanCoolantStop(void) { return s_active.fanCoolantStopC; }

int16_t ecuParamsFanAirStart(void) { return s_active.fanAirStartC; }

int16_t ecuParamsFanAirStop(void) { return s_active.fanAirStopC; }

int16_t ecuParamsHeaterStop(void) { return s_active.heaterStopC; }

int16_t ecuParamsNominalRpm(void) { return s_active.nominalRpm; }

/* Persist staging before promotion so a failed KV write leaves active state
 * and the stored blob at the previous known-good values. */
static hal_status_t configSessionCommit(void *user, const char **outReason,
                                        size_t *outCount) {
  (void)user;
  if (outReason == NULL || outCount == NULL) {
    return HAL_EINVAL;
  }
  *outReason = NULL;
  *outCount = 0u;

  if (s_loadPending) {
    *outReason = "storage_recovery";
    return HAL_EUNINIT;
  }

  if (!ecuParamsValidate(&s_staging, outReason)) {
    return HAL_EINVAL;
  }
  const hal_status_t persistStatus = ecuParamsPersist(&s_staging);
  if (persistStatus != HAL_OK) {
    *outReason = hal_status_to_string(persistStatus);
    return persistStatus;
  }

  *outCount = sc_param_copy_staging_to_active(k_ecu_params, k_ecu_params_count,
                                              &s_staging, &s_active);
  return HAL_OK;
}

static void configSessionRevert(void *user) {
  (void)user;
  (void)sc_param_copy_active_to_staging(k_ecu_params, k_ecu_params_count,
                                        &s_active, &s_staging);
}

static bool configSessionWritesReady(void *user) {
  (void)user;
  return !s_loadPending;
}

static void configSessionReadGps(void *user,
                                 sc_command_gps_snapshot_t *outSnapshot) {
  (void)user;
  if (outSnapshot == NULL) {
    return;
  }
  outSnapshot->available = isGPSAvailable();
  outSnapshot->lat_e6 = outSnapshot->available ? gpsGetLatE6() : 0;
  outSnapshot->lon_e6 = outSnapshot->available ? gpsGetLonE6() : 0;
  outSnapshot->speed_kmh_x10 = outSnapshot->available ? gpsGetSpeedKmhX10() : 0;
  outSnapshot->epoch = outSnapshot->available ? gpsGetEpoch() : 0u;
}

static void configSessionForwardNonSc(const char *line, void *user) {
  (void)user;
  tickTestsHandleSerialLine(line);
}

void configSessionInit(void) {
  if (s_serialCommands.initialized) {
    const hal_status_t detachStatus =
        hal_serial_commands_deinit(&s_serialCommands);
    if (detachStatus != HAL_OK) {
      hal_derr("ECU SC adapter detach failed: %s",
               hal_status_to_string(detachStatus));
      return;
    }
  }
  if (s_commandService.initialized) {
    const hal_status_t serviceStatus =
        sc_command_service_deinit(&s_commandService);
    if (serviceStatus != HAL_OK) {
      hal_derr("ECU SC service detach failed: %s",
               hal_status_to_string(serviceStatus));
      return;
    }
  }

  hal_serial_session_init_with_vocabulary(&s_configSession, SC_MODULE_TOKEN_ECU,
                                          FW_VERSION, BUILD_ID,
                                          &fiesta_default_vocabulary);

  sc_command_service_config_t serviceConfig = {0};
  serviceConfig.module_token = SC_MODULE_TOKEN_ECU;
  serviceConfig.firmware_version = FW_VERSION;
  serviceConfig.build_id = BUILD_ID;
  serviceConfig.params = k_ecu_params;
  serviceConfig.param_count = k_ecu_params_count;
  serviceConfig.active_values = &s_active;
  serviceConfig.staging_values = &s_staging;
  serviceConfig.commit = configSessionCommit;
  serviceConfig.revert = configSessionRevert;
  serviceConfig.writes_ready = configSessionWritesReady;
  serviceConfig.read_gps = configSessionReadGps;
  serviceConfig.allowed_sources =
      HAL_COMMAND_SOURCE_MASK(HAL_COMMAND_SOURCE_SERIAL_SESSION);

  hal_status_t status =
      sc_command_service_init(&s_commandService, NULL, &serviceConfig);
  if (status == HAL_OK) {
    hal_serial_commands_config_t adapterConfig =
        hal_serial_commands_config_defaults(&s_configSession);
    adapterConfig.router = sc_command_service_router(&s_commandService);
    adapterConfig.command_prefix = SC_COMMAND_PREFIX;
    adapterConfig.formatter = sc_command_format_serial_response;
    adapterConfig.allow_inactive = sc_command_allow_inactive_reboot;
    adapterConfig.fallback = configSessionForwardNonSc;
    status = hal_serial_commands_init(&s_serialCommands, &adapterConfig);
  }
  if (status != HAL_OK) {
    if (s_commandService.initialized) {
      (void)sc_command_service_deinit(&s_commandService);
    }
    hal_derr("ECU SC adapter init failed: %s", hal_status_to_string(status));
  }
}

void configSessionTick(void) {
  hal_serial_session_poll(&s_configSession);
  sc_command_service_process_deferred(&s_commandService);
  /* Keep debug chatter off the same CDC channel while SC session is active.
   * Without this, async deb()/derr() logs can interleave with framed replies
   * from another core and corrupt host parsing. */
  hal_debug_set_muted(hal_serial_session_is_active(&s_configSession));
}

bool configSessionActive(void) {
  return hal_serial_session_is_active(&s_configSession);
}

uint32_t configSessionId(void) {
  return hal_serial_session_id(&s_configSession);
}
