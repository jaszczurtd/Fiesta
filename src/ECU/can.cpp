#include "can.h"

MCP_CAN CAN(CAN0_GPIO);

void receivedCanMessage(void);

static byte frameNumber = 0;
// This the eight byte buffer of the incoming message data payload
static byte buf[CAN_FRAME_MAX_LENGTH];

static bool interrupt = false;

// Incoming CAN-BUS message
static long unsigned int canID = 0x000;

// This is the length of the incoming CAN-BUS message
static unsigned char len = 0;

static bool dpfConnected = false;
static unsigned long dpfMessages = 0, lastDPFMessages = 0;
static byte lastFrame = 0;

static bool initialized = false;
void canInit(int retries) {
  int at = 1;

  dpfConnected = false;
  dpfMessages = lastDPFMessages = 0;

  for(int a = 0; a < retries; a++) {
    initialized = (CAN_OK == CAN.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ));
    if(initialized) {
      break;
    }

    derr("ERROR!!!! CAN-BUS Shield init fail");

    m_delay(SECOND);
    watchdog_feed();
  }

  if(initialized) {
    deb("CAN BUS Shield init ok!");
    CAN.setMode(MCP_NORMAL); 
    CAN.setSleepWakeup(1); // Enable wake up interrupt when in sleep mode

    pinMode(CAN0_INT, INPUT); 
    attachInterrupt(digitalPinToInterrupt(CAN0_INT), receivedCanMessage, FALLING);
  } else {
    derr("CAN BUS Shield init problem. CAN communication would not be possible.");
  }
}

void CAN_sendAll(void) {
  CAN_updaterecipients_01(NULL);
  m_delay(CORE_OPERATION_DELAY);
  CAN_updaterecipients_02();
  m_delay(CORE_OPERATION_DELAY);
  CAN_sendThrottleUpdate();
  m_delay(CORE_OPERATION_DELAY);
  CAN_sendTurboUpdate();
}

bool CAN_updaterecipients_01(void *argument) {

  if(initialized) {
    int hi, lo;

    byte buf[CAN_FRAME_MAX_LENGTH];
    buf[CAN_FRAME_NUMBER] = frameNumber++;
    
    buf[CAN_FRAME_ECU_UPDATE_ENGINE_LOAD] = 
      (byte)valueFields[F_CALCULATED_ENGINE_LOAD];

    floatToDec(valueFields[F_VOLTS], &hi, &lo);
    buf[CAN_FRAME_ECU_UPDATE_VOLTS_HI] = (byte)hi;
    buf[CAN_FRAME_ECU_UPDATE_VOLTS_LO] = (byte)lo;

    buf[CAN_FRAME_ECU_UPDATE_COOLANT] = (byte)valueFields[F_COOLANT_TEMP];
    buf[CAN_FRAME_ECU_UPDATE_OIL] = (byte)valueFields[F_OIL_TEMP];

    short exh = valueFields[F_EGT];
    buf[CAN_FRAME_ECU_UPDATE_EGT_HI] = MSB(exh);
    buf[CAN_FRAME_ECU_UPDATE_EGT_LO] = LSB(exh);

    CAN.sendMsgBuf(CAN_ID_ECU_UPDATE_01, CAN_FRAME_MAX_LENGTH, buf);

    buf[CAN_FRAME_NUMBER] = frameNumber++;
    buf[CAN_FRAME_ECU_UPDATE_INTAKE] = (byte)valueFields[F_INTAKE_TEMP];

    short fuel = valueFields[F_FUEL];
    buf[CAN_FRAME_ECU_UPDATE_FUEL_HI] = MSB(fuel);
    buf[CAN_FRAME_ECU_UPDATE_FUEL_LO] = LSB(fuel);

    buf[CAN_FRAME_ECU_UPDATE_GPS_AVAILABLE] = isGPSAvailable();
    buf[CAN_FRAME_ECU_UPDATE_VEHICLE_SPEED] = valueFields[F_GPS_CAR_SPEED];
    
    CAN.sendMsgBuf(CAN_ID_ECU_UPDATE_02, CAN_FRAME_MAX_LENGTH, buf); 

    buf[CAN_FRAME_NUMBER] = frameNumber++;
    buf[CAN_FRAME_ECU_UPDATE_PRESSURE_PERCENTAGE] = valueFields[F_PRESSURE_PERCENTAGE];
    buf[CAN_FRAME_ECU_UPDATE_FUEL_TEMP] = valueFields[F_FUEL_TEMP];
    buf[CAN_FRAME_ECU_UPDATE_FAN_ENABLED] = valueFields[F_FAN_ENABLED];

    CAN.sendMsgBuf(CAN_ID_ECU_UPDATE_03, CAN_FRAME_MAX_LENGTH, buf); 
  }
  return true;  
}

static int lastRPM = C_INIT_VAL;
void CAN_updaterecipients_02(void) {
  if(initialized) {
    int rpm = int(valueFields[F_RPM]);
    if(lastRPM != rpm) {
      lastRPM = rpm;

      byte buf[CAN_FRAME_MAX_LENGTH];
      buf[CAN_FRAME_NUMBER] = frameNumber++;
      buf[CAN_FRAME_RPM_UPDATE_HI] = MSB(rpm);
      buf[CAN_FRAME_RPM_UPDATE_LO] = LSB(rpm);

      CAN.sendMsgBuf(CAN_ID_RPM, CAN_FRAME_MAX_LENGTH, buf);  
    }
  }
}

static float cLastTurboHI = C_INIT_VAL;
static float cLastTurboLO = C_INIT_VAL;
static float cLastTurboHI_d = C_INIT_VAL;
static float cLastTurboLO_d = C_INIT_VAL;
void CAN_sendTurboUpdate(void) {
  if(initialized) {
    byte buf[CAN_FRAME_MAX_LENGTH];
    int hi, lo;
    int hi_d, lo_d;

    floatToDec(valueFields[F_PRESSURE], &hi, &lo);
    floatToDec(valueFields[F_PRESSURE_DESIRED], &hi_d, &lo_d);
    if(lo != cLastTurboLO || hi != cLastTurboHI || hi_d != cLastTurboHI_d || lo_d != cLastTurboLO_d) {
      cLastTurboLO = lo;
      cLastTurboHI = hi;

      cLastTurboLO_d = lo_d;
      cLastTurboHI_d = hi_d;

      buf[CAN_FRAME_NUMBER] = frameNumber++;
      buf[CAN_FRAME_ECU_UPDATE_PRESSURE_HI] = (byte)hi;
      buf[CAN_FRAME_ECU_UPDATE_PRESSURE_LO] = (byte)lo;      
      buf[CAN_FRAME_ECU_UPDATE_PRESSURE_DESIRED_HI] = (byte)hi_d;
      buf[CAN_FRAME_ECU_UPDATE_PRESSURE_DESIRED_LO] = (byte)lo_d;

      CAN.sendMsgBuf(CAN_ID_TURBO_PRESSURE, sizeof(buf), buf); 
    }
  }
}

static int cLastThrottle = C_INIT_VAL;
void CAN_sendThrottleUpdate(void) {
  if(initialized) {
    byte buf[CAN_FRAME_MAX_LENGTH];

    int throttle = int(valueFields[F_THROTTLE_POS]);
    if(cLastThrottle != throttle) {
      cLastThrottle = throttle;

      buf[CAN_FRAME_NUMBER] = frameNumber++;
      buf[CAN_FRAME_THROTTLE_UPDATE_HI] = MSB(throttle);
      buf[CAN_FRAME_THROTTLE_UPDATE_LO] = LSB(throttle);

      CAN.sendMsgBuf(CAN_ID_THROTTLE, sizeof(buf), buf); 
    }
  }
}

void receivedCanMessage(void) {
    interrupt = true;
}

bool canMainLoop(void *argument) {
  if(initialized) {
    CAN.readMsgBuf(&canID, &len, buf);
    if(canID == 0 || len < 1) {
        return true;
    }

    if(lastFrame != buf[CAN_FRAME_NUMBER] || interrupt) {
        interrupt = false;
        lastFrame = buf[CAN_FRAME_NUMBER];

        switch(canID) {
            case CAN_ID_DPF: {
              dpfMessages++;
              valueFields[F_DPF_TEMP] = 
                MsbLsbToInt(buf[CAN_FRAME_DPF_UPDATE_DPF_TEMP_HI], 
                            buf[CAN_FRAME_DPF_UPDATE_DPF_TEMP_LO]);
              valueFields[F_DPF_REGEN] = buf[CAN_FRAME_DPF_UPDATE_DPF_REGEN];
            }
            break;

            case CAN_ID_CLOCK_BRIGHTNESS: {
              valueFields[F_CLOCK_BRIGHTNESS] =
                MsbLsbToInt(buf[CAN_FRAME_CLOCK_BRIGHTNESS_UPDATE_HI], 
                            buf[CAN_FRAME_CLOCK_BRIGHTNESS_UPDATE_LO]);
            }
            break;

            case CAN_ID_LUMENS: {
              valueFields[F_OUTSIDE_LUMENS] =
                decToFloat(buf[CAN_FRAME_LIGHTS_UPDATE_HI], 
                           buf[CAN_FRAME_LIGHTS_UPDATE_LO]);
            }
            break;

            case CAN_ID_OIL_AND_SPEED_MODULE_UPDATE: {
              valueFields[F_OIL_PRESSURE] = decToFloat(buf[CAN_FRAME_ECU_UPDATE_OIL_PRESSURE_HI],
                                                        buf[CAN_FRAME_ECU_UPDATE_OIL_PRESSURE_LO]);
              valueFields[F_ABS_CAR_SPEED] = buf[CAN_FRAME_ECU_UPDATE_ABS_CAR_SPEED];
            }
            break;

            default:
              deb("received unknown CAN frame:%03x len:%d\n", canID, len);
              break;
        }
    }
  }
  return true;
}

bool isDPFConnected(void) {
  return dpfConnected;
}

bool canCheckConnection(void *message) {
  lastRPM = C_INIT_VAL;
  cLastThrottle = C_INIT_VAL;

  dpfConnected = (dpfMessages != lastDPFMessages);
  lastDPFMessages = dpfMessages;
  return true;  
}



