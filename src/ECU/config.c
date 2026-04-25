
#include "config.h"
#include "tests.h"
#include "ecu_unit_testing.h"

#include <hal/hal_kv.h>
#include <hal/hal_serial_session.h>
#include <stddef.h>

const char *err = "ERR";

#define ECU_PARAMS_SCHEMA_V1    1u
#define ECU_PARAMS_SCHEMA_V2    2u
#define ECU_PARAMS_BLOB_SIZE_V1 16u
#define ECU_PARAMS_BLOB_SIZE_V2 18u
#define ECU_PARAMS_BLOB_KEY     0xDA10u

static ecu_params_values_t s_active = {
  .fanCoolantStartC = TEMP_FAN_START,
  .fanCoolantStopC = TEMP_FAN_STOP,
  .fanAirStartC = AIR_TEMP_FAN_START,
  .fanAirStopC = AIR_TEMP_FAN_STOP,
  .heaterStopC = TEMP_HEATER_STOP,
  .nominalRpm = NOMINAL_RPM_VALUE
};

static ecu_params_values_t s_staging = {
  .fanCoolantStartC = TEMP_FAN_START,
  .fanCoolantStopC = TEMP_FAN_STOP,
  .fanAirStartC = AIR_TEMP_FAN_START,
  .fanAirStopC = AIR_TEMP_FAN_STOP,
  .heaterStopC = TEMP_HEATER_STOP,
  .nominalRpm = NOMINAL_RPM_VALUE
};

static bool s_initialized = false;

#ifdef UNIT_TEST
static void configWriteU16LE(uint8_t *dst, uint16_t value) {
  dst[0] = (uint8_t)(value & 0xFFu);
  dst[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static void configWriteU32LE(uint8_t *dst, uint32_t value) {
  dst[0] = (uint8_t)(value & 0xFFu);
  dst[1] = (uint8_t)((value >> 8) & 0xFFu);
  dst[2] = (uint8_t)((value >> 16) & 0xFFu);
  dst[3] = (uint8_t)((value >> 24) & 0xFFu);
}
#endif

static uint16_t configReadU16LE(const uint8_t *src) {
  return (uint16_t)src[0] | ((uint16_t)src[1] << 8);
}

static uint32_t configReadU32LE(const uint8_t *src) {
  return (uint32_t)src[0]
    | ((uint32_t)src[1] << 8)
    | ((uint32_t)src[2] << 16)
    | ((uint32_t)src[3] << 24);
}

static uint32_t configCrc32(const uint8_t *data, uint16_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for(uint16_t i = 0; i < len; i++) {
    crc ^= (uint32_t)data[i];
    for(uint8_t bit = 0; bit < 8u; bit++) {
      if((crc & 1u) != 0u) {
        crc = (crc >> 1) ^ 0xEDB88320u;
      } else {
        crc >>= 1;
      }
    }
  }
  return ~crc;
}

TESTABLE_STATIC void ecuParamsLoadDefaults(ecu_params_values_t *outValues) {
  if(outValues == NULL) {
    return;
  }
  outValues->fanCoolantStartC = TEMP_FAN_START;
  outValues->fanCoolantStopC = TEMP_FAN_STOP;
  outValues->fanAirStartC = AIR_TEMP_FAN_START;
  outValues->fanAirStopC = AIR_TEMP_FAN_STOP;
  outValues->heaterStopC = TEMP_HEATER_STOP;
  outValues->nominalRpm = NOMINAL_RPM_VALUE;
}

TESTABLE_STATIC bool ecuParamsValidate(const ecu_params_values_t *candidate, const char **reason) {
  if(reason != NULL) {
    *reason = NULL;
  }

  if(candidate == NULL) {
    if(reason != NULL) {
      *reason = "null";
    }
    return false;
  }

  if(candidate->fanCoolantStartC < ECU_PARAMS_COOLANT_START_MIN
    || candidate->fanCoolantStartC > ECU_PARAMS_COOLANT_START_MAX) {
    if(reason != NULL) {
      *reason = "fan_coolant_start_range";
    }
    return false;
  }

  if(candidate->fanCoolantStopC < ECU_PARAMS_COOLANT_STOP_MIN
    || candidate->fanCoolantStopC > ECU_PARAMS_COOLANT_STOP_MAX) {
    if(reason != NULL) {
      *reason = "fan_coolant_stop_range";
    }
    return false;
  }

  if(candidate->fanAirStartC < ECU_PARAMS_AIR_START_MIN
    || candidate->fanAirStartC > ECU_PARAMS_AIR_START_MAX) {
    if(reason != NULL) {
      *reason = "fan_air_start_range";
    }
    return false;
  }

  if(candidate->fanAirStopC < ECU_PARAMS_AIR_STOP_MIN
    || candidate->fanAirStopC > ECU_PARAMS_AIR_STOP_MAX) {
    if(reason != NULL) {
      *reason = "fan_air_stop_range";
    }
    return false;
  }

  if(candidate->heaterStopC < ECU_PARAMS_HEATER_STOP_MIN
    || candidate->heaterStopC > ECU_PARAMS_HEATER_STOP_MAX) {
    if(reason != NULL) {
      *reason = "heater_stop_range";
    }
    return false;
  }

  if(candidate->nominalRpm < ECU_PARAMS_NOMINAL_RPM_MIN
    || candidate->nominalRpm > ECU_PARAMS_NOMINAL_RPM_MAX) {
    if(reason != NULL) {
      *reason = "nominal_rpm_range";
    }
    return false;
  }

  if(candidate->fanCoolantStopC >= candidate->fanCoolantStartC) {
    if(reason != NULL) {
      *reason = "fan_coolant_hysteresis";
    }
    return false;
  }

  if(candidate->fanAirStopC >= candidate->fanAirStartC) {
    if(reason != NULL) {
      *reason = "fan_air_hysteresis";
    }
    return false;
  }

  if(candidate->heaterStopC >= candidate->fanCoolantStartC) {
    if(reason != NULL) {
      *reason = "heater_vs_fan_order";
    }
    return false;
  }

  return true;
}

TESTABLE_STATIC bool ecuParamsStage(const ecu_params_values_t *candidate, const char **reason) {
  if(!ecuParamsValidate(candidate, reason)) {
    return false;
  }
  s_staging = *candidate;
  return true;
}

TESTABLE_STATIC void ecuParamsApply(void) {
  s_active = s_staging;
}

#ifdef UNIT_TEST
static bool configEncodeBlobV2(const ecu_params_values_t *values,
                               uint8_t outBlob[ECU_PARAMS_BLOB_SIZE_V2]) {
  if(values == NULL || outBlob == NULL) {
    return false;
  }

  configWriteU16LE(&outBlob[0], ECU_PARAMS_SCHEMA_V2);
  configWriteU16LE(&outBlob[2], (uint16_t)values->fanCoolantStartC);
  configWriteU16LE(&outBlob[4], (uint16_t)values->fanCoolantStopC);
  configWriteU16LE(&outBlob[6], (uint16_t)values->fanAirStartC);
  configWriteU16LE(&outBlob[8], (uint16_t)values->fanAirStopC);
  configWriteU16LE(&outBlob[10], (uint16_t)values->heaterStopC);
  configWriteU16LE(&outBlob[12], (uint16_t)values->nominalRpm);

  const uint32_t crc = configCrc32(outBlob, 14u);
  configWriteU32LE(&outBlob[14], crc);
  return true;
}
#endif

static bool configDecodeBlobV1(const uint8_t *blob, ecu_params_values_t *outValues) {
  if(blob == NULL || outValues == NULL) {
    return false;
  }

  const uint32_t expectedCrc = configReadU32LE(&blob[12]);
  const uint32_t actualCrc = configCrc32(blob, 12u);
  if(expectedCrc != actualCrc) {
    return false;
  }

  ecu_params_values_t decoded = {
    .fanCoolantStartC = (int16_t)configReadU16LE(&blob[2]),
    .fanCoolantStopC = (int16_t)configReadU16LE(&blob[4]),
    .fanAirStartC = (int16_t)configReadU16LE(&blob[6]),
    .fanAirStopC = (int16_t)configReadU16LE(&blob[8]),
    .heaterStopC = (int16_t)configReadU16LE(&blob[10]),
    .nominalRpm = NOMINAL_RPM_VALUE
  };

  if(!ecuParamsValidate(&decoded, NULL)) {
    return false;
  }

  *outValues = decoded;
  return true;
}

static bool configDecodeBlobV2(const uint8_t *blob, ecu_params_values_t *outValues) {
  if(blob == NULL || outValues == NULL) {
    return false;
  }

  const uint32_t expectedCrc = configReadU32LE(&blob[14]);
  const uint32_t actualCrc = configCrc32(blob, 14u);
  if(expectedCrc != actualCrc) {
    return false;
  }

  ecu_params_values_t decoded = {
    .fanCoolantStartC = (int16_t)configReadU16LE(&blob[2]),
    .fanCoolantStopC = (int16_t)configReadU16LE(&blob[4]),
    .fanAirStartC = (int16_t)configReadU16LE(&blob[6]),
    .fanAirStopC = (int16_t)configReadU16LE(&blob[8]),
    .heaterStopC = (int16_t)configReadU16LE(&blob[10]),
    .nominalRpm = (int16_t)configReadU16LE(&blob[12])
  };

  if(!ecuParamsValidate(&decoded, NULL)) {
    return false;
  }

  *outValues = decoded;
  return true;
}

TESTABLE_STATIC bool ecuParamsLoadPersisted(ecu_params_values_t *outValues) {
  if(outValues == NULL) {
    return false;
  }

  uint16_t blobLen = 0u;
  if(!hal_kv_get_blob(ECU_PARAMS_BLOB_KEY, NULL, 0u, &blobLen)) {
    return false;
  }
  if(blobLen != ECU_PARAMS_BLOB_SIZE_V1 && blobLen != ECU_PARAMS_BLOB_SIZE_V2) {
    return false;
  }

  uint8_t blob[ECU_PARAMS_BLOB_SIZE_V2] = {0};
  if(!hal_kv_get_blob(ECU_PARAMS_BLOB_KEY, blob, (uint16_t)sizeof(blob), &blobLen)) {
    return false;
  }

  const uint16_t schema = configReadU16LE(&blob[0]);
  if(schema == ECU_PARAMS_SCHEMA_V1 && blobLen == ECU_PARAMS_BLOB_SIZE_V1) {
    return configDecodeBlobV1(blob, outValues);
  }
  if(schema == ECU_PARAMS_SCHEMA_V2 && blobLen == ECU_PARAMS_BLOB_SIZE_V2) {
    return configDecodeBlobV2(blob, outValues);
  }
  return false;
}

#ifdef UNIT_TEST
TESTABLE_STATIC bool ecuParamsPersist(const ecu_params_values_t *values) {
  if(!ecuParamsValidate(values, NULL)) {
    return false;
  }

  uint8_t blob[ECU_PARAMS_BLOB_SIZE_V2] = {0};
  if(!configEncodeBlobV2(values, blob)) {
    return false;
  }
  return hal_kv_set_blob(ECU_PARAMS_BLOB_KEY, blob, (uint16_t)sizeof(blob));
}

TESTABLE_STATIC uint16_t ecuParamsBlobKeyForTest(void) {
  return ECU_PARAMS_BLOB_KEY;
}

TESTABLE_STATIC void ecuParamsResetRuntimeStateForTest(void) {
  ecuParamsLoadDefaults(&s_active);
  s_staging = s_active;
  s_initialized = false;
}
#endif

void ecuParamsInit(void) {
  if(s_initialized) {
    return;
  }

  ecuParamsLoadDefaults(&s_staging);
  ecuParamsApply();

  ecu_params_values_t loaded = {0};
  if(ecuParamsLoadPersisted(&loaded) && ecuParamsStage(&loaded, NULL)) {
    ecuParamsApply();
  }

  s_initialized = true;
}

const ecu_params_values_t *ecuParamsActive(void) {
  return &s_active;
}

int16_t ecuParamsFanCoolantStart(void) {
  return s_active.fanCoolantStartC;
}

int16_t ecuParamsFanCoolantStop(void) {
  return s_active.fanCoolantStopC;
}

int16_t ecuParamsFanAirStart(void) {
  return s_active.fanAirStartC;
}

int16_t ecuParamsFanAirStop(void) {
  return s_active.fanAirStopC;
}

int16_t ecuParamsHeaterStop(void) {
  return s_active.heaterStopC;
}

int16_t ecuParamsNominalRpm(void) {
  return s_active.nominalRpm;
}

static hal_serial_session_t s_configSession;

static void configSession_onUnknownLine(const char *line, void *user) {
  (void)user;
  /* Forward non-protocol command lines (PID tuning, etc.) to test fixtures.
   * This keeps the bootstrap session parser as the sole consumer of raw
   * serial bytes; secondary consumers receive whole lines only after HELLO
   * and friends have been handled. */
  tickTestsHandleSerialLine(line);
}

void configSessionInit(void) {
  hal_serial_session_init(&s_configSession, MODULE_NAME, FW_VERSION, BUILD_ID);
  hal_serial_session_set_unknown_handler(&s_configSession,
                                         &configSession_onUnknownLine,
                                         NULL);
}

void configSessionTick(void) {
  hal_serial_session_poll(&s_configSession);
}

bool configSessionActive(void) {
  return hal_serial_session_is_active(&s_configSession);
}

uint32_t configSessionId(void) {
  return hal_serial_session_id(&s_configSession);
}
