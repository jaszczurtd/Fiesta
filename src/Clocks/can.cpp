#include "can.h"

volatile float valueFields[F_LAST];

#ifdef UNIT_TEST
hal_can_t clocksTestGetCanHandle(void);
#endif

static unsigned char frameNumber = 0;
static unsigned long ecuMessages = 0, lastEcuMessages = 0;
static bool ecuConnected = false;
static bool lastConnected = false;
static unsigned long dpfMessages = 0, lastDPFMessages = 0;
static bool dpfConnected = false;

static unsigned long oilSpeedModuleMessages = 0, lastOilSpeedModuleMessages = 0;
static bool oilSpeedModuleConnected = false;
static bool lastOilSpeedModuleConnected = false;

static bool interrupt = false;
static hal_can_t canHandle = NULL;

#ifdef UNIT_TEST
hal_can_t clocksTestGetCanHandle(void) {
  return canHandle;
}
#endif

bool canInit(void) {
  ecuConnected = false;
  ecuMessages = lastEcuMessages = 0;
  dpfMessages = lastDPFMessages = 0;
  interrupt = false;

  bool error = false;

  canHandle = hal_can_create_with_retry(CAN_CS, CAN_INT, receivedCanMessage,
                                         MAX_RETRIES, hal_watchdog_feed);
  error = (canHandle == NULL);
  if(!error) {
    deb("CAN BUS Shield init ok!");
    canMainLoop();
  }
  return error;
}

void updateCANrecipients(void) {
  uint8_t out[CAN_FRAME_MAX_LENGTH] = {};

  out[CAN_FRAME_NUMBER] = frameNumber++;

  unsigned short br = (unsigned short)valueFields[F_CLOCK_BRIGHTNESS];
  out[CAN_FRAME_CLOCK_BRIGHTNESS_UPDATE_HI] = MSB(br);
  out[CAN_FRAME_CLOCK_BRIGHTNESS_UPDATE_LO] = LSB(br);

  hal_can_send(canHandle, CAN_ID_CLOCK_BRIGHTNESS, CAN_FRAME_MAX_LENGTH, out);
}

void receivedCanMessage(void) {
  interrupt = true;
}

static void onCanFrame(uint32_t canID, uint8_t len, const uint8_t *buf) {
  interrupt = false;

  if(buf == NULL || len > CAN_FRAME_MAX_LENGTH) {
    derr("Received invalid CAN frame with ID: %03x, len: %d\n", canID, len);
    return;
  }

  switch(canID) {
    case CAN_ID_ECU_UPDATE_01: {
      if(len <= CAN_FRAME_ECU_UPDATE_OIL) {
        derr("Received truncated ECU_UPDATE_01 frame with len: %d", len);
        return;
      }
      ecuMessages++; ecuConnected = true;

      valueFields[F_CALCULATED_ENGINE_LOAD] = buf[CAN_FRAME_ECU_UPDATE_ENGINE_LOAD];
      valueFields[F_VOLTS] = decToFloat(buf[CAN_FRAME_ECU_UPDATE_VOLTS_HI],
                                          buf[CAN_FRAME_ECU_UPDATE_VOLTS_LO]);
      valueFields[F_COOLANT_TEMP] = static_cast<int8_t>(buf[CAN_FRAME_ECU_UPDATE_COOLANT]);
      valueFields[F_OIL_TEMP] = static_cast<int8_t>(buf[CAN_FRAME_ECU_UPDATE_OIL]);
    }
    break;

    case CAN_ID_DPF: {
      if(len <= CAN_FRAME_DPF_UPDATE_DPF_REGEN) {
        derr("Received truncated DPF frame with len: %d", len);
        return;
      }
      dpfMessages++;
      valueFields[F_DPF_REGEN] = buf[CAN_FRAME_DPF_UPDATE_DPF_REGEN];
    }
    break;

    case CAN_ID_THROTTLE: {
      if(len <= CAN_FRAME_THROTTLE_UPDATE_LO) {
        derr("Received truncated throttle frame with len: %d", len);
        return;
      }
      ecuMessages++; ecuConnected = true;

      valueFields[F_THROTTLE_POS] = MsbLsbToInt(buf[CAN_FRAME_THROTTLE_UPDATE_HI],
                                                buf[CAN_FRAME_THROTTLE_UPDATE_LO]);
      triggerDrawHighImportanceValue(true);
    }
    break;

    case CAN_ID_TURBO_PRESSURE: {
      if(len <= CAN_FRAME_ECU_UPDATE_PRESSURE_DESIRED_LO) {
        derr("Received truncated turbo-pressure frame with len: %d", len);
        return;
      }
      ecuMessages++; ecuConnected = true;

      valueFields[F_PRESSURE] = decToFloat(buf[CAN_FRAME_ECU_UPDATE_PRESSURE_HI],
                                            buf[CAN_FRAME_ECU_UPDATE_PRESSURE_LO]);
      valueFields[F_PRESSURE_DESIRED] = decToFloat(buf[CAN_FRAME_ECU_UPDATE_PRESSURE_DESIRED_HI],
                                            buf[CAN_FRAME_ECU_UPDATE_PRESSURE_DESIRED_LO]);
      triggerDrawHighImportanceValue(true);
    }
    break;

    case CAN_ID_RPM: {
      if(len <= CAN_FRAME_RPM_UPDATE_LO) {
        derr("Received truncated RPM frame with len: %d", len);
        return;
      }
      ecuMessages++; ecuConnected = true;

      valueFields[F_RPM] = MsbLsbToInt(buf[CAN_FRAME_RPM_UPDATE_HI],
                                        buf[CAN_FRAME_RPM_UPDATE_LO]);
      updateCluster();
    }
    break;

    case CAN_ID_ECU_UPDATE_02: {
      if(len <= CAN_FRAME_ECU_UPDATE_VEHICLE_SPEED) {
        derr("Received truncated ECU_UPDATE_02 frame with len: %d", len);
        return;
      }
      ecuMessages++; ecuConnected = true;

      valueFields[F_INTAKE_TEMP] = static_cast<int8_t>(buf[CAN_FRAME_ECU_UPDATE_INTAKE]);
      valueFields[F_FUEL] = MsbLsbToInt(buf[CAN_FRAME_ECU_UPDATE_FUEL_HI],
                                        buf[CAN_FRAME_ECU_UPDATE_FUEL_LO]);
      valueFields[F_GPS_IS_AVAILABLE] = buf[CAN_FRAME_ECU_UPDATE_GPS_AVAILABLE];
      valueFields[F_GPS_CAR_SPEED] = buf[CAN_FRAME_ECU_UPDATE_VEHICLE_SPEED];
    }
    break;

    case CAN_ID_ECU_UPDATE_03: {
      if(len <= CAN_FRAME_ECU_UPDATE_FAN_ENABLED) {
        derr("Received truncated ECU_UPDATE_03 frame with len: %d", len);
        return;
      }
      ecuMessages++; ecuConnected = true;

      valueFields[F_PRESSURE_PERCENTAGE] = buf[CAN_FRAME_ECU_UPDATE_PRESSURE_PERCENTAGE];
      valueFields[F_FUEL_TEMP] = static_cast<int8_t>(buf[CAN_FRAME_ECU_UPDATE_FUEL_TEMP]);
      valueFields[F_FAN_ENABLED] = buf[CAN_FRAME_ECU_UPDATE_FAN_ENABLED];
    }
    break;

    case CAN_ID_OIL_AND_SPEED_MODULE_UPDATE: {
      if(len <= CAN_FRAME_ECU_UPDATE_ABS_CAR_SPEED) {
        derr("Received truncated OilAndSpeed frame with len: %d", len);
        return;
      }
      oilSpeedModuleMessages++; oilSpeedModuleConnected = true;

      valueFields[F_OIL_PRESSURE] = decToFloat(buf[CAN_FRAME_ECU_UPDATE_OIL_PRESSURE_HI],
                                                buf[CAN_FRAME_ECU_UPDATE_OIL_PRESSURE_LO]);
      valueFields[F_ABS_CAR_SPEED] = buf[CAN_FRAME_ECU_UPDATE_ABS_CAR_SPEED];
      updateCluster();
    }
    break;

    case CAN_ID_EGT_UPDATE: {
      if(len <= CAN_FRAME_EGT_UPDATE_DPF_TEMP_LO) {
        derr("Received truncated EGT frame with len: %d", len);
        return;
      }
      oilSpeedModuleMessages++; oilSpeedModuleConnected = true;

      valueFields[F_EGT] = MsbLsbToInt(buf[CAN_FRAME_EGT_UPDATE_EGT_HI],
                                       buf[CAN_FRAME_EGT_UPDATE_EGT_LO]);
      valueFields[F_DPF_TEMP] =
        MsbLsbToInt(buf[CAN_FRAME_EGT_UPDATE_DPF_TEMP_HI],
                    buf[CAN_FRAME_EGT_UPDATE_DPF_TEMP_LO]);

      deb("EGT: %dC DPF: %dC", int(valueFields[F_EGT]),
                               int(valueFields[F_DPF_TEMP]));
    }
    break;

    case CAN_ID_LUMENS: {
      if(len <= CAN_FRAME_LIGHTS_UPDATE_LO) {
        derr("Received truncated lumens frame with len: %d", len);
        return;
      }
      valueFields[F_OUTSIDE_LUMENS] =
        decToFloat(buf[CAN_FRAME_LIGHTS_UPDATE_HI],
                    buf[CAN_FRAME_LIGHTS_UPDATE_LO]);
    }
    break;
    default:
      deb("received unknown CAN frame:%03x len:%d\n", canID, len);
      break;
  }
}

void canMainLoop(void) {
  hal_can_process_all(canHandle, onCanFrame);
}

bool isEcuConnected(void) {
  return ecuConnected;
}

bool isOilSpeedModuleConnected(void) {
  return oilSpeedModuleConnected;
}

void canCheckConnection(void) {
  oilSpeedModuleConnected = (oilSpeedModuleMessages != lastOilSpeedModuleMessages);
  lastOilSpeedModuleMessages = oilSpeedModuleMessages;

  ecuConnected = (ecuMessages != lastEcuMessages);
  lastEcuMessages = ecuMessages;

  dpfConnected = (dpfMessages != lastDPFMessages);
  lastDPFMessages = dpfMessages;

  if(lastOilSpeedModuleConnected != oilSpeedModuleConnected) {
    lastOilSpeedModuleConnected = oilSpeedModuleConnected;

    if(!lastOilSpeedModuleConnected) {
      valueFields[F_ABS_CAR_SPEED] = 0.0;
      valueFields[F_OIL_PRESSURE] = 0.0;
      valueFields[F_EGT] = 0.0;
      valueFields[F_DPF_TEMP] = 0.0;
    }
  }

  if(lastConnected != ecuConnected) {
    lastConnected = ecuConnected;

    if(!ecuConnected) {
      for(int a = 0; a < F_LAST; a++) {
        switch(a) {
          case F_ABS_CAR_SPEED:
          case F_OIL_PRESSURE:
            break;
          default:
            valueFields[a] = 0.0;
            break;
        }
      }
    }
    triggerDrawHighImportanceValue(true);
  }
}

int getEngineRPM(void) {
  return int(valueFields[F_RPM]);
}

bool isEngineRunning(void) {
  return (getEngineRPM() != 0);
}

bool isFanEnabled(void) {
  return valueFields[F_FAN_ENABLED] > 0;
}

bool isDPFRegenerating(void) {
  return valueFields[F_DPF_REGEN] > 0;
}

bool isDPFConnected(void) {
  return dpfConnected;
}

float readFuel(void) {
  return valueFields[F_FUEL];
}

int getCurrentCarSpeed(void) {
  return int(valueFields[F_ABS_CAR_SPEED]);
}

int getGPSSpeed(void) {
  return int(valueFields[F_GPS_CAR_SPEED]);
}

float getOilPressure(void) {
  return valueFields[F_OIL_PRESSURE];
}

bool isGPSAvailable(void) {
  return valueFields[F_GPS_IS_AVAILABLE] > 0;
}
