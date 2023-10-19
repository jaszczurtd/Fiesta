
//based on Open-Ecu-Sim-OBD2-FW
//https://github.com/spoonieau/OBD2-ECU-Simulator

#include "obd-2.h"

// Set CS to pin 6 for the CAB-BUS sheild                                
MCP_CAN CAN0(CAN1_GPIO);                                      

//Current Firmware Version
char FW_Version[] = "0.10";  

// Incoming CAN-BUS message
long unsigned int canId = 0x000;

// This is the length of the incoming CAN-BUS message
unsigned char len = 0;

// This the eight byte buffer of the incoming message data payload
unsigned char buf[8];

// MIL on and DTC Present 
bool MIL = true;

//Stored Vechicle VIN 
static unsigned char vehicle_Vin[18] = "JASZCZUR FIESTA";

//Stored Calibration ID
static unsigned char calibration_ID[18] = "FW00108MHZ1111111";

//Stored ECU Name
static unsigned char ecu_Name[19] = "FIESTA_TDI";


//Default PID values
int timing_Advance  = 10;
int maf_Air_Flow_Rate =  0;


//=================================================================
//Init CAN-BUS and Serial
//=================================================================

static bool initialized = false;
void obdInit(int retries) {

  for(int a = 0; a < retries; a++) {
    initialized = (CAN_OK == CAN0.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ));
    if(initialized) {
      break;
    }
    derr("OBD-2 CAN init error!");
    delay(SECOND);
    watchdog_update();
  }

  if(initialized) {
    deb("OBD-2 CAN Shield init ok!");
    CAN0.setMode(MCP_NORMAL); 
    pinMode(CAN1_INT, INPUT); 
  } else {
    derr("OBD0-2 CAN Shield init problem. The OBD-2 connection will not be possible.");
  }
}

//=================================================================
//Define ECU Supported PID's
//=================================================================

// Define the set of PIDs for MODE01 you wish you ECU to support.  For more information, see:
// https://en.wikipedia.org/wiki/OBD-II_PIDs#Mode_1_PID_00
//
// PID 0x01 (1) - Monitor status since DTCs cleared. (Includes malfunction indicator lamp (MIL) status and number of DTCs.)
// |   PID 0x05 (05) - Engine Coolant Temperature
// |   |      PID 0x0C (12) - Engine RPM
// |   |      |PID 0x0D (13) - Vehicle speed
// |   |      ||PID 0x0E (14) - Timing advance
// |   |      |||PID 0x0F (15) - Intake air temperature
// |   |      ||||PID 0x10 (16) - MAF Air Flow Rate
// |   |      |||||            PID 0x1C (28) - OBD standards this vehicle conforms to
// |   |      |||||            |                              PID 0x51 (58) - Fuel Type
// |   |      |||||            |                              |
// v   V      VVVVV            V                              v
// 10001000000111110000:000000010000000000000:0000000000000000100
// Converted to hex, that is the following four byte value binary to hex
// 0x881F0000 0x00 PID 01 -20
// 0x02000000 0x20 PID 21 - 40
// 0x04000000 0x40 PID 41 - 60

// Next, we'll create the bytearray that will be the Supported PID query response data payload using the four bye supported pi hex value
// we determined above (0x081F0000):

//                               0x06 - additional meaningful bytes after this one (1 byte Service Mode, 1 byte PID we are sending, and the four by Supported PID value)
//                                |    0x41 - This is a response (0x40) to a service mode 1 (0x01) query.  0x40 + 0x01 = 0x41
//                                |     |    0x00 - The response is for PID 0x00 (Supported PIDS 1-20)
//                                |     |     |    0x88 - The first of four bytes of the Supported PIDS value
//                                |     |     |     |    0x1F - The second of four bytes of the Supported PIDS value
//                                |     |     |     |     |    0x00 - The third of four bytes of the Supported PIDS value
//                                |     |     |     |     |      |   0x00 - The fourth of four bytes of the Supported PIDS value
//                                |     |     |     |     |      |    |    0x00 - OPTIONAL - Just extra zeros to fill up the 8 byte CAN message data payload)
//                                |     |     |     |     |      |    |     |
//                                V     V     V     V     V      V    V     V
byte mode1Supported0x00PID[8] = {0x06, 0x41, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff};
byte mode1Supported0x20PID[8] = {0x06, 0x41, 0x20, 0xff, 0xff, 0xff, 0xff, 0xff};
byte mode1Supported0x40PID[8] = {0x06, 0x41, 0x40, 0xff, 0xff, 0xff, 0xff, 0xff};

// Define the set of PIDs for MODE09 you wish you ECU to support.
// As per the information on bitwise encoded PIDs (https://en.wikipedia.org/wiki/OBD-II_PIDs#Mode_1_PID_00)
// Our supported PID value is:
//
//  PID 0x02 - Vehicle Identification Number (VIN)
//  | PID 0x04 (04) - Calibration ID
//  | |     PID 0x0C (12) - ECU NAME
//  | |     |
//  V V     V
// 01010000010  // Converted to hex, that is the following four byte value binary to hex
// 0x28200000 0x00 PID 01-11

// Next, we'll create the bytearray that will be the Supported PID query response data payload using the four bye supported pi hex value
// we determined above (0x28200000):

//                               0x06 - additional meaningful bytes after this one (1 byte Service Mode, 1 byte PID we are sending, and the four by Supported PID value)
//                                |    0x41 - This is a response (0x40) to a service mode 1 (0x01) query.  0x40 + 0x01 = 0x41
//                                |     |    0x00 - The response is for PID 0x00 (Supported PIDS 1-20)
//                                |     |     |    0x28 - The first of four bytes of the Supported PIDS value
//                                |     |     |     |    0x20 - The second of four bytes of the Supported PIDS value
//                                |     |     |     |     |    0x00 - The third of four bytes of the Supported PIDS value
//                                |     |     |     |     |      |   0x00 - The fourth of four bytes of the Supported PIDS value
//                                |     |     |     |     |      |    |    0x00 - OPTIONAL - Just extra zeros to fill up the 8 byte CAN message data payload)
//                                |     |     |     |     |      |    |     |
//                                V     V     V     V     V      V    V     V
byte mode9Supported0x00PID[8] = {0x06, 0x49, 0x00, 0x28, 0x28, 0x00, 0x00, 0x00};

void obdLoop(void) {
  if(!initialized) {
    return;
  }

//=================================================================
//Handel Recived CAN-BUS frames from service tool
//=================================================================

  //if(CAN_MSGAVAIL == CAN.checkReceive())
  if (!digitalRead(CAN1_INT))  {
    CAN0.readMsgBuf(&canId, &len, buf);
    //https://en.wikipedia.org/wiki/OBD-II_PIDs#CAN_(11-bit)_bus_format
    
    int len = buf[0];
    int service = buf[1];
    int pid = buf[2];

    char pids[128];
    if(len == L2 && service == SHOW_CURRENT_DATA) {
      snprintf(pids, sizeof(pids) - 1, "0x%02x/%s", pid, getPIDName(pid));
    } else {
      snprintf(pids, sizeof(pids) - 1, "0x%02x", pid);
    }

    deb("OBD-2/(%x) l:0x%02x service:0x%02x PID:%s / (%hhu,%hhu,%hhu)", 
                        canId, 
                        len, service, pids, 
                        len, service, pid);

//=================================================================
//DTC support 
//=================================================================
    if(len == L1) {
      const byte *DTC = NULL;
      switch(service) { 
        case SHOW_STORED_DIAGNOSTIC_TROUBLE_CODES:
          deb("DTC show");
          switch(pid) {
            case 0x00: { 
              if (MIL) {
                //P0217
                DTC = (const byte[]){6, MODE3_RESPONSE, 1, 2, 23, 0, 0, 0}; 
              } else {
                DTC = (const byte[]){6, MODE3_RESPONSE, 0, 0, 0, 0, 0, 0}; 
              }
              CAN0.sendMsgBuf(REPLY_ID, 0, 8, (byte*)DTC);
            }
            break;
          }
          break;
        case CLEAR_DIAGNOSTIC_TROUBLE_CODES_AND_STORED_VALUES:
          deb("DTC clear");
          switch(pid) {
            case 0x00:
              MIL = false;
              DTC = (const byte[]){6, MODE3_RESPONSE, 0, 0, 0, 0, 0, 0}; 
              CAN0.sendMsgBuf(REPLY_ID, 0, 8, (byte*)DTC);
            break;
          }
          break;
      }
    }

//=================================================================
//Return CAN-BUS Messages - SUPPORTED PID's 
//=================================================================
    if(len == L2) {
      switch(service) { 
        case SHOW_CURRENT_DATA: {
          switch(pid) {  
            case PIDS_SUPPORT_01_20:
              CAN0.sendMsgBuf(REPLY_ID, 0, 8, mode1Supported0x00PID);
              break;
            case PIDS_SUPPORT_21_40:
              CAN0.sendMsgBuf(REPLY_ID, 0, 8, mode1Supported0x20PID);
              break;
            case PIDS_SUPPORT_41_60:
              CAN0.sendMsgBuf(REPLY_ID, 0, 8, mode1Supported0x40PID);
              break;
            case OBD_STANDARDS_THIS_VEHICLE_CONFORMS_TO: { 
              byte obd_Std_Msg[8] = {4, MODE1_RESPONSE, OBD_STANDARDS_THIS_VEHICLE_CONFORMS_TO, 
                EOBD_OBD_OBD_II};
              CAN0.sendMsgBuf(REPLY_ID, 0, 8, obd_Std_Msg);
              break;
            }
            case FUEL_TYPE: { 
              byte fuel_Type_Msg[8] = {4, MODE1_RESPONSE, FUEL_TYPE, 
                FUEL_TYPE_DIESEL};
              CAN0.sendMsgBuf(REPLY_ID, 0, 8, fuel_Type_Msg);
              break;
            }

            //=================================================================
            //Return CAN-BUS Messages - RETURN PID VALUES - SENSORS 
            //=================================================================
            case ENGINE_COOLANT_TEMPERATURE: { 
              int engine_Coolant_Temperature = int(valueFields[F_COOLANT_TEMP] + 40);
              byte engine_Coolant_Temperature_Msg[8] = {3, MODE1_RESPONSE, ENGINE_COOLANT_TEMPERATURE, 
                (byte)(engine_Coolant_Temperature)};
              CAN0.sendMsgBuf(REPLY_ID, 0, 8, engine_Coolant_Temperature_Msg);
              break;
            }
            case INTAKE_MANIFOLD_ABSOLUTE_PRESSURE: {
              int intake_Pressure = (valueFields[F_PRESSURE] * 255.0f / 2.55f);
              if(intake_Pressure > 255) {
                intake_Pressure = 255;
              }
              byte intake_Pressure_Msg[8] = {3, MODE1_RESPONSE, INTAKE_MANIFOLD_ABSOLUTE_PRESSURE, 
                (byte)(intake_Pressure)};
              CAN0.sendMsgBuf(REPLY_ID, 0, 8, intake_Pressure_Msg);
              break;
            }
            case THROTTLE_POSITION: {
              float percent = (valueFields[F_THROTTLE_POS] * 100) / PWM_RESOLUTION;
              byte throttle_Position = percentToGivenVal(percent, 255);
              byte throttle_Position_Msg[8] = {3, MODE1_RESPONSE, THROTTLE_POSITION, 
                (throttle_Position)};
              CAN0.sendMsgBuf(REPLY_ID, 0, 8, throttle_Position_Msg);
              break;
            }
            case CALCULATED_ENGINE_LOAD: {
              byte engine_Load = percentToGivenVal(valueFields[F_CALCULATED_ENGINE_LOAD], 255);
              byte engine_Load_Msg[8] = {3, MODE1_RESPONSE, CALCULATED_ENGINE_LOAD, 
                (engine_Load)};
              CAN0.sendMsgBuf(REPLY_ID, 0, 8, engine_Load_Msg);
              break;
            }
            case ENGINE_RPM: { 
              int engine_Rpm = int(valueFields[F_RPM] * 4);
              byte engine_Rpm_Msg[8] = {4, MODE1_RESPONSE, ENGINE_RPM, 
                MSB(engine_Rpm), LSB(engine_Rpm)};
              CAN0.sendMsgBuf(REPLY_ID, 0, 8, engine_Rpm_Msg);
              break;
            }
            case VEHICLE_SPEED: { 
              int vehicle_Speed = int(valueFields[F_CAR_SPEED]);
              byte vehicle_Speed_Msg[8] = {3, MODE1_RESPONSE, VEHICLE_SPEED, 
                (byte)(vehicle_Speed)};
              CAN0.sendMsgBuf(REPLY_ID, 0, 8, vehicle_Speed_Msg);
              break;
            }
            case TIMING_ADVANCE: { 
              byte timing_Advance_Msg[8] = {3, MODE1_RESPONSE, TIMING_ADVANCE, 
                (byte)((timing_Advance + 64) * 2)};
              CAN0.sendMsgBuf(REPLY_ID, 0, 8, timing_Advance_Msg);
              break;
            }
            case AIR_INTAKE_TEMPERATURE: { 
              int intake_Temp = int(valueFields[F_INTAKE_TEMP] + 40);
              byte intake_Temp_Msg[8] = {3, MODE1_RESPONSE, AIR_INTAKE_TEMPERATURE, 
                (byte)(intake_Temp)};
              CAN0.sendMsgBuf(REPLY_ID, 0, 8, intake_Temp_Msg);
              break;
            }
            case MAF_AIR_FLOW_RATE: { 
              byte maf_Air_Flow_Rate_Msg[8] = {4, MODE1_RESPONSE, MAF_AIR_FLOW_RATE, 
                MSB(maf_Air_Flow_Rate), LSB(maf_Air_Flow_Rate)};
              CAN0.sendMsgBuf(REPLY_ID, 0, 8, maf_Air_Flow_Rate_Msg);
              break;
            }
            default:
              deb("unknown device data requested: 0x%02x/%s", pid, getPIDName(pid));
              break;
          }
        }
        break;

        //=================================================================
        //Return CAN-BUS Messages - RETURN PID VALUES - DATA 
        //=================================================================
        case REQUEST_VEHICLE_INFORMATION: {
          switch(pid) {  
            case REQUEST_MODE_9_SUPPORTED:
              CAN0.sendMsgBuf(REPLY_ID, 0, 8, mode9Supported0x00PID);
              break;

            case REQUEST_VIN: { 
              unsigned char frame1[8] = {16, 20, 73, REQUEST_VIN, 1, vehicle_Vin[0], vehicle_Vin[1], vehicle_Vin[2]};
              unsigned char frame2[8] = {33, vehicle_Vin[3], vehicle_Vin[4], vehicle_Vin[5], vehicle_Vin[6], vehicle_Vin[7], vehicle_Vin[8], vehicle_Vin[9]};
              unsigned char frame3[8] = {34, vehicle_Vin[10], vehicle_Vin[11], vehicle_Vin[12], vehicle_Vin[13], vehicle_Vin[14], vehicle_Vin[15], vehicle_Vin[16]};

              CAN0.sendMsgBuf(REPLY_ID, 0, 8, frame1);
              CAN0.sendMsgBuf(REPLY_ID, 0, 8, frame2);
              CAN0.sendMsgBuf(REPLY_ID, 0, 8, frame3);
              break;
            }

            case REQUEST_CALLIBRATION_ID: { 
              unsigned char frame1[8] = {16, 20, 73, REQUEST_CALLIBRATION_ID, 1, calibration_ID[0], calibration_ID[1], calibration_ID[2]};
              unsigned char frame2[8] = {33, calibration_ID[3], calibration_ID[4], calibration_ID[5], calibration_ID[6], calibration_ID[7], calibration_ID[8], calibration_ID[9]};
              unsigned char frame3[8] = {34, calibration_ID[10], calibration_ID[11], calibration_ID[12], calibration_ID[13], calibration_ID[14], calibration_ID[15], calibration_ID[16]};

              CAN0.sendMsgBuf(REPLY_ID, 0, 8, frame1);
              CAN0.sendMsgBuf(REPLY_ID, 0, 8, frame2);
              CAN0.sendMsgBuf(REPLY_ID, 0, 8, frame3);
              break;
            }

            case REQUEST_ECU_NAME: { 
              unsigned char frame1[8] = {10, 14, 49, REQUEST_ECU_NAME, 01, ecu_Name[0], ecu_Name[1], ecu_Name[2]};
              unsigned char frame2[8] = {21, ecu_Name[3], ecu_Name[4], ecu_Name[5], ecu_Name[6], ecu_Name[7], ecu_Name[8], ecu_Name[9]};
              unsigned char frame3[8] = {22, ecu_Name[10], ecu_Name[11], ecu_Name[12], ecu_Name[13], ecu_Name[14], ecu_Name[15], ecu_Name[16]};
              unsigned char frame4[8] = {23, ecu_Name[17], ecu_Name[18]};

              CAN0.sendMsgBuf(REPLY_ID, 0, 8, frame1);
              CAN0.sendMsgBuf(REPLY_ID, 0, 8, frame2);
              CAN0.sendMsgBuf(REPLY_ID, 0, 8, frame3);
              CAN0.sendMsgBuf(REPLY_ID, 0, 8, frame4);
              break;
            }

            default:
              deb("not supported request MODE9: 0x%02x", pid);
              break;
          }
        }
        break;

        default:
          deb("not supported service: 0x%02x", service);
          break;
      }
    }
  }
}

