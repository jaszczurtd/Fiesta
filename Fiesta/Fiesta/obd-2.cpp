
//based on Open-Ecu-Sim-OBD2-FW
//https://github.com/spoonieau/OBD2-ECU-Simulator

#include "obd-2.h"

// Set CS to pin 6 for the CAB-BUS sheild                                
MCP_CAN CAN0(6);                                      

//Current Firmware Version
char FW_Version[] = "0.10";  

// Incoming CAN-BUS message
long unsigned int canId = 0x000;

// This is the length of the incoming CAN-BUS message
unsigned char len = 0;

// This the eight byte buffer of the incoming message data payload
unsigned char buf[8];

//char str[20];
String canMessageRead = "";

// MIL on and DTC Present 
bool MIL = true;

//Stored Vechicle VIN 
static unsigned char vehicle_Vin[18] = "JASZCZUR FIESTA";

//Stored Calibration ID
static unsigned char calibration_ID[18] = "FW00108MHZ1111111";

//Stored ECU Name
static unsigned char ecu_Name[19] = "FIESTA_TDI";

//OBD standards https://en.wikipedia.org/wiki/OBD-II_PIDs#Service_01_PID_1C
int obd_Std = 11;

//Fuel Type Coding https://en.wikipedia.org/wiki/OBD-II_PIDs#Fuel_Type_Coding
int fuel_Type = 4; //diesel

//Default PID values
int timing_Advance  = 10;
int maf_Air_Flow_Rate =  0;


//=================================================================
//Init CAN-BUS and Serial
//=================================================================

static bool initialized = false;
void obdInit(int retries) {

#ifdef OBD2

  for(int a = 0; a < retries; a++) {
    initialized = (CAN_OK == CAN0.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ));
    if(initialized) {
      break;
    }
    derr("OBD-2 CAN init error!");
    delay(1000);
    watchdog_update();
  }

  if(initialized) {
    deb("OBD-2 CAN Shield init ok!");
    CAN0.setMode(MCP_NORMAL); 
    pinMode(CAN1_INT, INPUT); 
  } else {
    derr("OBD0-2 CAN Shield init problem. The OBD-2 connection will not be possible.");
  }

#endif
}

int getNumberFromMessage(String str) {
  int len = str.length();
  int num = 0;
  int factor = 1;
  
  for (int i = len - 2; i >= 0; i--) {
      if (str[i] == ',') {
          continue;
      }
      
      int digit = str[i] - '0';
      num += digit * factor;
      factor *= 10;
  }
  return num;
}

void obdLoop(void) {
  if(!initialized) {
    return;
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
  byte mode1Supported0x00PID[8] = {0x06, 0x41, 0x00, 0x88, 0x1F, 0x00, 0x00, 0x00};
  byte mode1Supported0x20PID[8] = {0x06, 0x41, 0x20, 0x02, 0x00, 0x00, 0x00, 0x00};
  byte mode1Supported0x40PID[8] = {0x06, 0x41, 0x40, 0x04, 0x00, 0x00, 0x00, 0x00};

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

//=================================================================
//Vars to help build msg
//=================================================================

  ////Build setting return msg
  byte obd_Std_Msg[8] = {4, 65, 0x1C, (byte)(obd_Std)};
  byte fuel_Type_Msg[8] = {4, 65, 0x51, (byte)(fuel_Type)};


  int engine_Rpm = int(valueFields[F_RPM] * 4);
  int engine_Coolant_Temperature = int(valueFields[F_COOLANT_TEMP] + 40);
  int intake_Temp = int(valueFields[F_INTAKE_TEMP] + 40);
  int vehicle_Speed = int(valueFields[F_CAR_SPEED]);


  //Build sensor return msg
  byte engine_Coolant_Temperature_Msg[8] = {3, 65, 0x05, (byte)(engine_Coolant_Temperature)};
  byte engine_Rpm_Msg[8] = {4, 65, 0x0C, MSB(engine_Rpm), LSB(engine_Rpm)};
  byte vehicle_Speed_Msg[8] = {3, 65, 0x0D, (byte)(vehicle_Speed)};
  byte timing_Advance_Msg[8] = {3, 65, 0x0E, (byte)((timing_Advance + 64) * 2)};
  byte intake_Temp_Msg[8] = {3, 65, 0x0F, (byte)(intake_Temp)};
  byte maf_Air_Flow_Rate_Msg[8] = {4, 65, 0x10, MSB(maf_Air_Flow_Rate), LSB(maf_Air_Flow_Rate)};

  //Serial return message
  String reply;

//=================================================================
//Handel Recived CAN-BUS frames from service tool
//=================================================================

  //if(CAN_MSGAVAIL == CAN.checkReceive())
  if (!digitalRead(CAN1_INT))
  {

    CAN0.readMsgBuf(&canId, &len, buf);
    //https://en.wikipedia.org/wiki/OBD-II_PIDs#CAN_(11-bit)_bus_format
    
    Serial.print("Received: "); Serial.print(canId, HEX); Serial.print(",");

    for (int i = 0; i < 3; i++)
    {
      canMessageRead = canMessageRead + buf[i] + ",";
    }
    Serial.println(canMessageRead);

//=================================================================
//Return CAN-BUS Messages - SUPPORTED PID's 
//=================================================================

    if (canMessageRead == "2,1,0,")
    {
      CAN0.sendMsgBuf(REPLY_ID, 0, 8, mode1Supported0x00PID);
      reply = String(String(REPLY_ID, HEX) + ",0,8," + String((char*)mode1Supported0x00PID));
      Serial.println("Reply: " + reply);
      reply = "";
    }

    if (canMessageRead == "2,1,32,")
    {
      CAN0.sendMsgBuf(REPLY_ID, 0, 8, mode1Supported0x20PID);
      reply = String(String(REPLY_ID, HEX) + ",0,8," + String((char*)mode1Supported0x20PID));
      Serial.println("Reply: " + reply);
      reply = "";
    }

    if (canMessageRead == "2,1,64,")
    {
      CAN0.sendMsgBuf(REPLY_ID, 0, 8, mode1Supported0x40PID);
      reply = String(String(REPLY_ID, HEX) + ",0,8," + String((char*)mode1Supported0x40PID));
      Serial.println("Reply: " + reply);
      reply = "";
    }

    if (canMessageRead == "2,9,0,")
    {
      CAN0.sendMsgBuf(REPLY_ID, 0, 8, mode9Supported0x00PID);
      reply = String(String(REPLY_ID, HEX) + ",0,8," + String((char*)mode9Supported0x00PID));
      Serial.println("Reply: " + reply);
      reply = "";
    }
    
//=================================================================
//Return CAN-BUS Messages - RETURN PID VALUES - SENSORS 
//=================================================================

    //Engine Coolant
    if (canMessageRead == "2,1,5,")
    {
      CAN0.sendMsgBuf(REPLY_ID, 0, 8, engine_Coolant_Temperature_Msg);
      reply = String(String(REPLY_ID, HEX) + ",0,8," + String((char*)engine_Coolant_Temperature_Msg));
      Serial.println("Reply: " + reply);
      reply = "";
    }

    //Rpm
    if (canMessageRead == "2,1,12,")
    {
      CAN0.sendMsgBuf(REPLY_ID, 0, 8, engine_Rpm_Msg);
      reply = String(String(REPLY_ID, HEX) + ",0,8," + String((char*)engine_Rpm_Msg));
      Serial.println("Reply: " + reply);
      reply = "";
    }

    //Speed
    if (canMessageRead == "2,1,13,")
    {
      CAN0.sendMsgBuf(REPLY_ID, 0, 8, vehicle_Speed_Msg);
      reply = String(String(REPLY_ID, HEX) + ",0,8," + String((char*)vehicle_Speed_Msg));
      Serial.println("Reply: " + reply);
      reply = "";
    }

    //Timing Adv
    if (canMessageRead == "2,1,14,")
    {
      CAN0.sendMsgBuf(REPLY_ID, 0, 8, timing_Advance_Msg);
      reply = String(String(REPLY_ID, HEX) + ",0,8," + String((char*)timing_Advance_Msg));
      Serial.println("Reply: " + reply);
      reply = "";
    }

    //Intake Tempture
    if (canMessageRead == "2,1,15,")
    {
      CAN0.sendMsgBuf(REPLY_ID, 0, 8, intake_Temp_Msg);
      reply = String(String(REPLY_ID, HEX) + ",0,8," + String((char*)intake_Temp_Msg));
      Serial.println("Reply: " + reply);
      reply = "";
    }

    //MAF
    if (canMessageRead == "2,1,16,")
    {
      CAN0.sendMsgBuf(REPLY_ID, 0, 8, maf_Air_Flow_Rate_Msg);
      reply = String(String(REPLY_ID, HEX) + ",0,8," + String((char*)maf_Air_Flow_Rate_Msg));
      Serial.println("Reply: " + reply);
      reply = "";
    }

    //OBD standard
    if (canMessageRead == "2,1,28,")
    {
      CAN0.sendMsgBuf(REPLY_ID, 0, 8, obd_Std_Msg);
      reply = String(String(REPLY_ID, HEX) + ",0,8," + String((char*)obd_Std_Msg));
      Serial.println("Reply: " + reply);
      reply = "";
    }

    //Fuel Type Coding
    if (canMessageRead == "2,1,58,")
    {
      CAN0.sendMsgBuf(REPLY_ID, 0, 8, fuel_Type_Msg);
      reply = String(String(REPLY_ID, HEX) + ",0,8," + String((char*)fuel_Type_Msg));
      Serial.println("Reply: " + reply);
      reply = "";
    }

//=================================================================
//Return CAN-BUS Messages - RETURN PID VALUES - DATA 
//=================================================================

    //VIN
    if (canMessageRead == "2,9,2,")
    {

      unsigned char frame1[8] = {16, 20, 73, 2, 1, vehicle_Vin[0], vehicle_Vin[1], vehicle_Vin[2]};
      unsigned char frame2[8] = {33, vehicle_Vin[3], vehicle_Vin[4], vehicle_Vin[5], vehicle_Vin[6], vehicle_Vin[7], vehicle_Vin[8], vehicle_Vin[9]};
      unsigned char frame3[8] = {34, vehicle_Vin[10], vehicle_Vin[11], vehicle_Vin[12], vehicle_Vin[13], vehicle_Vin[14], vehicle_Vin[15], vehicle_Vin[16]};

      CAN0.sendMsgBuf(REPLY_ID, 0, 8, frame1);
      CAN0.sendMsgBuf(REPLY_ID, 0, 8, frame2);
      CAN0.sendMsgBuf(REPLY_ID, 0, 8, frame3);
    }

    //CAL ID
    if (canMessageRead == "2,9,4,")
    {
       unsigned char frame1[8] = {16, 20, 73, 4, 1, calibration_ID[0], calibration_ID[1], calibration_ID[2]};
       unsigned char frame2[8] = {33, calibration_ID[3], calibration_ID[4], calibration_ID[5], calibration_ID[6], calibration_ID[7], calibration_ID[8], calibration_ID[9]};
       unsigned char frame3[8] = {34, calibration_ID[10], calibration_ID[11], calibration_ID[12], calibration_ID[13], calibration_ID[14], calibration_ID[15], calibration_ID[16]};

      CAN0.sendMsgBuf(REPLY_ID, 0, 8, frame1);
      CAN0.sendMsgBuf(REPLY_ID, 0, 8, frame2);
      CAN0.sendMsgBuf(REPLY_ID, 0, 8, frame3);
    }

    //ECU NAME
    if (canMessageRead == "2,9,10,")
    {

      unsigned char frame1[8] = {10, 14, 49, 10, 01, ecu_Name[0], ecu_Name[1], ecu_Name[2]};
      unsigned char frame2[8] = {21, ecu_Name[3], ecu_Name[4], ecu_Name[5], ecu_Name[6], ecu_Name[7], ecu_Name[8], ecu_Name[9]};
      unsigned char frame3[8] = {22, ecu_Name[10], ecu_Name[11], ecu_Name[12], ecu_Name[13], ecu_Name[14], ecu_Name[15], ecu_Name[16]};
      unsigned char frame4[8] = {23, ecu_Name[17], ecu_Name[18]};

      CAN0.sendMsgBuf(REPLY_ID, 0, 8, frame1);
      CAN0.sendMsgBuf(REPLY_ID, 0, 8, frame2);
      CAN0.sendMsgBuf(REPLY_ID, 0, 8, frame3);
      CAN0.sendMsgBuf(REPLY_ID, 0, 8, frame4);
    }

//=================================================================
//Return CAN-BUS Messages - RETURN PID VALUES - DTC
//=================================================================

    //DTC
    if (canMessageRead == "1,3,0,")
    {
      if (MIL)
      {
        unsigned char DTC[] = {6, 67, 1, 2, 23, 0, 0, 0}; //P0217
        CAN0.sendMsgBuf(REPLY_ID, 0, 8, DTC);
        reply = String(String(REPLY_ID, HEX) + ",0,8, " + String((char*)DTC));
        Serial.println("Reply: " + reply);
        reply = "";
      }
      else
      {
        unsigned char DTC[] = {6, 67, 0, 0, 0, 0, 0, 0}; //No Stored DTC
        CAN0.sendMsgBuf(REPLY_ID, 0, 8, DTC);
        reply = String(String(REPLY_ID, HEX) + ",0,8, " + String((char*)DTC));
        Serial.println("Reply: " + reply);
        reply = "";
      }
    }

    //DTC Clear 
    if (canMessageRead == "1,4,0,")
    {
      MIL = false;
    }

      canMessageRead = "";
    }

  }

