#include "can.h"
#include "dtcManager.h"
#include "gps.h"

void receivedCanMessage(void);

typedef struct {
  uint8_t frameNumberVal;
  bool interruptPending;
  bool dpfConnectedFlag;
  bool dpfEverSeenFlag;
  unsigned long dpfMessagesCount;
  unsigned long lastDpfMessagesCount;
  hal_can_t canBusHandle;
  bool isInitialized;
  int32_t lastRpmSent;
  float lastTurboHiSent;
  float lastTurboLoSent;
  float lastTurboHiDesiredSent;
  float lastTurboLoDesiredSent;
  int32_t lastThrottleSent;
} can_state_t;

static can_state_t s_canState = {
  .frameNumberVal = 0u,
  .interruptPending = false,
  .dpfConnectedFlag = false,
  .dpfEverSeenFlag = false,
  .dpfMessagesCount = 0uL,
  .lastDpfMessagesCount = 0uL,
  .canBusHandle = NULL,
  .isInitialized = false,
  .lastRpmSent = (int32_t)C_INIT_VAL,
  .lastTurboHiSent = (float)C_INIT_VAL,
  .lastTurboLoSent = (float)C_INIT_VAL,
  .lastTurboHiDesiredSent = (float)C_INIT_VAL,
  .lastTurboLoDesiredSent = (float)C_INIT_VAL,
  .lastThrottleSent = (int32_t)C_INIT_VAL
};

void canInit(int retries) {
  s_canState.dpfConnectedFlag = false;
  s_canState.dpfMessagesCount = 0uL;
  s_canState.lastDpfMessagesCount = 0uL;

  s_canState.canBusHandle = hal_can_create_with_retry(CAN0_GPIO,
                                                      CAN0_INT,
                                                      receivedCanMessage,
                                                      retries > 0 ? retries - 1 : 0,
                                                      watchdog_feed);
  s_canState.isInitialized = (s_canState.canBusHandle != NULL);

  if(s_canState.isInitialized) {
    deb("CAN BUS Shield init ok!");
  } else {
    derr("CAN BUS Shield init problem. CAN communication would not be possible.");
  }
}

uint32_t CAN_packGpsDateTime(uint32_t dateYYMMDD, uint32_t timeHHMM) {
  int32_t yy = (int32_t)(dateYYMMDD / 10000u);
  int32_t mm = (int32_t)((dateYYMMDD / 100u) % 100u);
  int32_t dd = (int32_t)(dateYYMMDD % 100u);
  int32_t hh = (int32_t)(timeHHMM / 100u);
  int32_t mi = (int32_t)(timeHHMM % 100u);

  if(mm < 1 || mm > 12 || dd < 1 || dd > 31 || hh < 0 || hh > 23 || mi < 0 || mi > 59) {
    return 0u;
  }

  int32_t yearOffset = yy - 20; // base year 2020
  if(yearOffset < 0) {
    yearOffset = 0;
  } else if(yearOffset > 15) {
    yearOffset = 15;
  }

  return ((uint32_t)yearOffset << 20)
       | ((uint32_t)mm << 16)
       | ((uint32_t)dd << 11)
       | ((uint32_t)hh << 6)
       | (uint32_t)mi;
}

static int32_t gpsCoordToMicroDeg(float coord, int32_t minVal, int32_t maxVal) {
  int32_t scaled = (int32_t)roundf(coord * 1000000.0f);
  if(scaled < minVal) {
    return minVal;
  }
  if(scaled > maxVal) {
    return maxVal;
  }
  return scaled;
}

bool CAN_buildGpsLatFrame(uint8_t frameNo, uint8_t *outBuf, int outLen) {
  if(outBuf == NULL || outLen < CAN_FRAME_MAX_LENGTH) {
    return false;
  }

  int32_t latMicro = gpsCoordToMicroDeg(getGlobalValue(F_LATITUDE), -90000000, 90000000);
  uint32_t latRaw = (uint32_t)latMicro;

  memset(outBuf, 0, (size_t)outLen);
  outBuf[CAN_FRAME_NUMBER] = frameNo;
  outBuf[CAN_FRAME_GPS_EXT_LAT_B3] = (uint8_t)((latRaw >> 24) & 0xFFu);
  outBuf[CAN_FRAME_GPS_EXT_LAT_B2] = (uint8_t)((latRaw >> 16) & 0xFFu);
  outBuf[CAN_FRAME_GPS_EXT_LAT_B1] = (uint8_t)((latRaw >> 8) & 0xFFu);
  outBuf[CAN_FRAME_GPS_EXT_LAT_B0] = (uint8_t)(latRaw & 0xFFu);
  outBuf[CAN_FRAME_GPS_EXT_STATUS] = (uint8_t)getGlobalValue(F_GPS_IS_AVAILABLE);
  return true;
}

bool CAN_buildGpsLonTimeFrame(uint8_t frameNo, uint8_t *outBuf, int outLen) {
  if(outBuf == NULL || outLen < CAN_FRAME_MAX_LENGTH) {
    return false;
  }

  int32_t lonMicro = gpsCoordToMicroDeg(getGlobalValue(F_LONGITUDE), -180000000, 180000000);
  uint32_t lonRaw = (uint32_t)lonMicro;
  uint32_t packedDt = CAN_packGpsDateTime((uint32_t)getGlobalValue(F_GPS_DATE),
                                          (uint32_t)getGlobalValue(F_GPS_TIME));

  memset(outBuf, 0, (size_t)outLen);
  outBuf[CAN_FRAME_NUMBER] = frameNo;
  outBuf[CAN_FRAME_GPS_EXT_LON_B3] = (uint8_t)((lonRaw >> 24) & 0xFFu);
  outBuf[CAN_FRAME_GPS_EXT_LON_B2] = (uint8_t)((lonRaw >> 16) & 0xFFu);
  outBuf[CAN_FRAME_GPS_EXT_LON_B1] = (uint8_t)((lonRaw >> 8) & 0xFFu);
  outBuf[CAN_FRAME_GPS_EXT_LON_B0] = (uint8_t)(lonRaw & 0xFFu);
  outBuf[CAN_FRAME_GPS_EXT_DT_HI] = (uint8_t)((packedDt >> 16) & 0xFFu);
  outBuf[CAN_FRAME_GPS_EXT_DT_MD] = (uint8_t)((packedDt >> 8) & 0xFFu);
  outBuf[CAN_FRAME_GPS_EXT_DT_LO] = (uint8_t)(packedDt & 0xFFu);
  return true;
}

void CAN_sendGpsExtended(void) {
  if(!s_canState.isInitialized) {
    return;
  }

  uint8_t latBuf[CAN_FRAME_MAX_LENGTH] = {0};
  uint8_t lonTimeBuf[CAN_FRAME_MAX_LENGTH] = {0};

  if(CAN_buildGpsLatFrame(s_canState.frameNumberVal++, latBuf, (int)sizeof(latBuf))) {
    hal_can_send(s_canState.canBusHandle, CAN_ID_GPS_EXT_LAT, CAN_FRAME_MAX_LENGTH, latBuf);
  }
  if(CAN_buildGpsLonTimeFrame(s_canState.frameNumberVal++, lonTimeBuf, (int)sizeof(lonTimeBuf))) {
    hal_can_send(s_canState.canBusHandle, CAN_ID_GPS_EXT_LON_TIME, CAN_FRAME_MAX_LENGTH, lonTimeBuf);
  }
}

void CAN_sendAll(void) {
  CAN_updaterecipients_01();
  hal_delay_ms(CORE_OPERATION_DELAY);
  CAN_updaterecipients_02();
  hal_delay_ms(CORE_OPERATION_DELAY);
  CAN_sendThrottleUpdate();
  hal_delay_ms(CORE_OPERATION_DELAY);
  CAN_sendTurboUpdate();
}

void CAN_updaterecipients_01(void) {

  if(s_canState.isInitialized) {
    int hi, lo;

    uint8_t buf[CAN_FRAME_MAX_LENGTH];
    buf[CAN_FRAME_NUMBER] = s_canState.frameNumberVal++;
    
    buf[CAN_FRAME_ECU_UPDATE_ENGINE_LOAD] =
      (uint8_t)getGlobalValue(F_CALCULATED_ENGINE_LOAD);

    floatToDec(getGlobalValue(F_VOLTS), &hi, &lo);
    buf[CAN_FRAME_ECU_UPDATE_VOLTS_HI] = (uint8_t)hi;
    buf[CAN_FRAME_ECU_UPDATE_VOLTS_LO] = (uint8_t)lo;

    buf[CAN_FRAME_ECU_UPDATE_COOLANT] = (uint8_t)getGlobalValue(F_COOLANT_TEMP);
    buf[CAN_FRAME_ECU_UPDATE_OIL] = (uint8_t)getGlobalValue(F_OIL_TEMP);

    int16_t exh = (int16_t)getGlobalValue(F_EGT);
    buf[CAN_FRAME_ECU_UPDATE_EGT_HI] = MSB(exh);
    buf[CAN_FRAME_ECU_UPDATE_EGT_LO] = LSB(exh);

    hal_can_send(s_canState.canBusHandle, CAN_ID_ECU_UPDATE_01, CAN_FRAME_MAX_LENGTH, buf);

    buf[CAN_FRAME_NUMBER] = s_canState.frameNumberVal++;
    buf[CAN_FRAME_ECU_UPDATE_INTAKE] = (uint8_t)getGlobalValue(F_INTAKE_TEMP);

    int16_t fuel = (int16_t)getGlobalValue(F_FUEL);
    buf[CAN_FRAME_ECU_UPDATE_FUEL_HI] = MSB(fuel);
    buf[CAN_FRAME_ECU_UPDATE_FUEL_LO] = LSB(fuel);

    buf[CAN_FRAME_ECU_UPDATE_GPS_AVAILABLE] = isGPSAvailable();
    buf[CAN_FRAME_ECU_UPDATE_VEHICLE_SPEED] = getGlobalValue(F_GPS_CAR_SPEED);

    hal_can_send(s_canState.canBusHandle, CAN_ID_ECU_UPDATE_02, CAN_FRAME_MAX_LENGTH, buf);

    buf[CAN_FRAME_NUMBER] = s_canState.frameNumberVal++;
    buf[CAN_FRAME_ECU_UPDATE_PRESSURE_PERCENTAGE] = getGlobalValue(F_PRESSURE_PERCENTAGE);
    buf[CAN_FRAME_ECU_UPDATE_FUEL_TEMP] = getGlobalValue(F_FUEL_TEMP);
    buf[CAN_FRAME_ECU_UPDATE_FAN_ENABLED] = getGlobalValue(F_FAN_ENABLED);

    hal_can_send(s_canState.canBusHandle, CAN_ID_ECU_UPDATE_03, CAN_FRAME_MAX_LENGTH, buf);
  }
}

void CAN_updaterecipients_02(void) {
  if(s_canState.isInitialized) {
    int32_t rpm = (int32_t)(getGlobalValue(F_RPM));
    if(s_canState.lastRpmSent != rpm) {
      s_canState.lastRpmSent = rpm;

      uint8_t buf[CAN_FRAME_MAX_LENGTH];
      buf[CAN_FRAME_NUMBER] = s_canState.frameNumberVal++;
      buf[CAN_FRAME_RPM_UPDATE_HI] = MSB(rpm);
      buf[CAN_FRAME_RPM_UPDATE_LO] = LSB(rpm);

      hal_can_send(s_canState.canBusHandle, CAN_ID_RPM, CAN_FRAME_MAX_LENGTH, buf);
    }
  }
}

void CAN_sendTurboUpdate(void) {
  if(s_canState.isInitialized) {
    uint8_t buf[CAN_FRAME_MAX_LENGTH];
    int hi, lo;
    int hi_d, lo_d;

    floatToDec(getGlobalValue(F_PRESSURE), &hi, &lo);
    floatToDec(getGlobalValue(F_PRESSURE_DESIRED), &hi_d, &lo_d);
    if(lo != s_canState.lastTurboLoSent || hi != s_canState.lastTurboHiSent || hi_d != s_canState.lastTurboHiDesiredSent || lo_d != s_canState.lastTurboLoDesiredSent) {
      s_canState.lastTurboLoSent = lo;
      s_canState.lastTurboHiSent = hi;

      s_canState.lastTurboLoDesiredSent = lo_d;
      s_canState.lastTurboHiDesiredSent = hi_d;

      buf[CAN_FRAME_NUMBER] = s_canState.frameNumberVal++;
      buf[CAN_FRAME_ECU_UPDATE_PRESSURE_HI] = (uint8_t)hi;
      buf[CAN_FRAME_ECU_UPDATE_PRESSURE_LO] = (uint8_t)lo;      
      buf[CAN_FRAME_ECU_UPDATE_PRESSURE_DESIRED_HI] = (uint8_t)hi_d;
      buf[CAN_FRAME_ECU_UPDATE_PRESSURE_DESIRED_LO] = (uint8_t)lo_d;

      hal_can_send(s_canState.canBusHandle, CAN_ID_TURBO_PRESSURE, sizeof(buf), buf);
    }
  }
}

void CAN_sendThrottleUpdate(void) {
  if(s_canState.isInitialized) {
    uint8_t buf[CAN_FRAME_MAX_LENGTH];

    int32_t throttle = (int32_t)(getGlobalValue(F_THROTTLE_POS));
    if(s_canState.lastThrottleSent != throttle) {
      s_canState.lastThrottleSent = throttle;

      buf[CAN_FRAME_NUMBER] = s_canState.frameNumberVal++;
      buf[CAN_FRAME_THROTTLE_UPDATE_HI] = MSB(throttle);
      buf[CAN_FRAME_THROTTLE_UPDATE_LO] = LSB(throttle);

      hal_can_send(s_canState.canBusHandle, CAN_ID_THROTTLE, sizeof(buf), buf);
    }
  }
}

void receivedCanMessage(void) {
    s_canState.interruptPending = true;
}

static void onCanFrame(uint32_t canID, uint8_t len, const uint8_t *buf) {
  s_canState.interruptPending = false;

  switch(canID) {
    case CAN_ID_DPF: {
      s_canState.dpfMessagesCount++;
      s_canState.dpfEverSeenFlag = true;
      setGlobalValue(F_DPF_TEMP,
        MsbLsbToInt(buf[CAN_FRAME_DPF_UPDATE_DPF_TEMP_HI],
                    buf[CAN_FRAME_DPF_UPDATE_DPF_TEMP_LO]));
      setGlobalValue(F_DPF_REGEN, buf[CAN_FRAME_DPF_UPDATE_DPF_REGEN]);
    }
    break;

    case CAN_ID_CLOCK_BRIGHTNESS: {
      setGlobalValue(F_CLOCK_BRIGHTNESS,
        MsbLsbToInt(buf[CAN_FRAME_CLOCK_BRIGHTNESS_UPDATE_HI],
                    buf[CAN_FRAME_CLOCK_BRIGHTNESS_UPDATE_LO]));
    }
    break;

    case CAN_ID_LUMENS: {
      setGlobalValue(F_OUTSIDE_LUMENS,
        decToFloat(buf[CAN_FRAME_LIGHTS_UPDATE_HI],
                   buf[CAN_FRAME_LIGHTS_UPDATE_LO]));
    }
    break;

    case CAN_ID_OIL_AND_SPEED_MODULE_UPDATE: {
      setGlobalValue(F_OIL_PRESSURE, decToFloat(buf[CAN_FRAME_ECU_UPDATE_OIL_PRESSURE_HI],
                                              buf[CAN_FRAME_ECU_UPDATE_OIL_PRESSURE_LO]));
      setGlobalValue(F_ABS_CAR_SPEED, buf[CAN_FRAME_ECU_UPDATE_ABS_CAR_SPEED]);
    }
    break;

    default:
      deb("received unknown CAN frame:%03x len:%d\n", canID, len);
      break;
  }
}

void canMainLoop(void) {
  if(s_canState.isInitialized) {
    hal_can_process_all(s_canState.canBusHandle, onCanFrame);
  }
}

bool isDPFConnected(void) {
  return s_canState.dpfConnectedFlag;
}

void canCheckConnection(void) {
  s_canState.lastRpmSent = C_INIT_VAL;
  s_canState.lastThrottleSent = C_INIT_VAL;

  s_canState.dpfConnectedFlag = (s_canState.dpfMessagesCount != s_canState.lastDpfMessagesCount);
  s_canState.lastDpfMessagesCount = s_canState.dpfMessagesCount;

  bool dpfCommLost = s_canState.dpfEverSeenFlag && !s_canState.dpfConnectedFlag;
  dtcManagerSetActive(DTC_DPF_COMM_LOST, dpfCommLost);
}
