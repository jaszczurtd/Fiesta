
//based on Open-Ecu-Sim-OBD2-FW
//https://github.com/spoonieau/OBD2-ECU-Simulator
// and CAN OBD & UDS Simulator Written By: Cory J. Fowler  December 20th, 2016

#include "obd-2.h"

void obdReq(byte *data);
void unsupported(byte mode, byte pid);
void unsupportedPrint(byte mode, byte pid);
void iso_tp(byte mode, byte pid, int len, byte *data);
void negAck(byte mode, byte reason);

// Set CS to pin 6 for the CAB-BUS sheild                                
MCP_CAN CAN0(CAN1_GPIO);                                      

// CAN RX Variables
static unsigned long rxId;
static byte dlc;
static byte rxBuf[8];

static bool initialized = false;
void obdInit(int retries) {

  for(int a = 0; a < retries; a++) {
    initialized = (CAN0.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ) == CAN_OK);
    if(initialized) {
      deb("MCP2515 Initialized Successfully!");
    }
    else {
      derr("Error Initializing MCP2515...");
    }
    if(initialized) {
      break;
    }
    m_delay(SECOND);
    watchdog_feed();
  }

#if standard == 1
  // Standard ID Filters
  CAN0.init_Mask(0,0xFFfffff);                // Init first mask...
  CAN0.init_Filt(0,0xFFFffff);                // Init first filter...
  CAN0.init_Filt(1,0xFFfffff);                // Init second filter...
  
  CAN0.init_Mask(1,0xFFfffff);                // Init second mask... 
  CAN0.init_Filt(2,0xFFFffff);                // Init third filter...
  CAN0.init_Filt(3,0xFFfffff);                // Init fourth filter...
  CAN0.init_Filt(4,0xFFfffff);                // Init fifth filter...
  CAN0.init_Filt(5,0xFFfffff);                // Init sixth filter...

#else
  // Extended ID Filters
  CAN0.init_Mask(0,0x90FFFF00);                // Init first mask...
  CAN0.init_Filt(0,0x90DB3300);                // Init first filter...
  CAN0.init_Filt(1,0x90DA0100);                // Init second filter...
  
  CAN0.init_Mask(1,0x90FFFF00);                // Init second mask... 
  CAN0.init_Filt(2,0x90DB3300);                // Init third filter...
  CAN0.init_Filt(3,0x90DA0100);                // Init fourth filter...
  CAN0.init_Filt(4,0x90DB3300);                // Init fifth filter...
  CAN0.init_Filt(5,0x90DA0100);                // Init sixth filter...
#endif
  
  CAN0.setMode(MCP_NORMAL);                          // Set operation mode to normal so the MCP2515 sends acks to received data.

  pinMode(CAN1_INT, INPUT);                          // Configuring pin for /INT input
  
  deb("OBD-2 CAN Shield init ok!");
}

void obdLoop(void) {
  if(!initialized) {
    return;
  }
  
  if(!digitalRead(CAN1_INT))                         // If CAN1_INT pin is low, read receive buffer
  {
    CAN0.readMsgBuf(&rxId, &dlc, rxBuf);             // Get CAN data
    
    // First request from most adapters...
    if(rxId == FUNCTIONAL_ID){
      obdReq(rxBuf);
    }       
  }
}

void storeECUName(byte *tab, int idx) {
  for(int a = 0; a < (int)strlen(ecu_Name); a++) {
    tab[a + idx] = (byte)ecu_Name[a];
  }
}

void obdReq(byte *data){
  byte numofBytes = data[0];
  byte mode = data[1] & 0x0F;
  byte pid = data[2];
  bool tx = false;
  byte txData[] = {0x00,(byte)(0x40 | mode),pid,PAD,PAD,PAD,PAD,PAD};

  deb("OBD-2 pid:0x%02x (%s) length:0x%02x mode:0x%02x",  pid, getPIDName(pid), numofBytes, mode);
  
  //txData[1] = 0x40 | mode;
  //txData[2] = pid; 
  
  //=============================================================================
  // MODE $01 - Show current data
  //=============================================================================
  if(mode == L1){
    if(pid == PID_0_20){        // Supported PIDs 01-20
      txData[0] = 0x06;
      
      txData[3] = 0xff;
      txData[4] = 0xff;
      txData[5] = 0xff;
      txData[6] = 0xff;
      tx = true;
    }
    else if(pid == STATUS_DTC){    // Monitor status since DTs cleared.
      bool MIL = true;
      byte DTC = 5;
      txData[0] = 0x06;
      
      txData[3] = (MIL << 7) | (DTC & 0x7F);
      txData[4] = 0x07;
      txData[5] = 0xFF;
      txData[6] = 0x00;
      tx = true;
    }
    else if(pid == FUEL_SYS_STATUS){    // Fuel system status
      txData[0] = 0x04;
      txData[3] = 0;
      txData[4] = 0;
      tx = true;
    }
    else if(pid == ENGINE_LOAD){    // Calculated engine load
      txData[0] = 0x03;
      txData[3] = percentToGivenVal(valueFields[F_CALCULATED_ENGINE_LOAD], 255);
      tx = true;
    }
    else if(pid == ABSOLUTE_LOAD) {
      txData[0] = 0x04;
      int l = percentToGivenVal(valueFields[F_CALCULATED_ENGINE_LOAD], 255);
      txData[3] = MSB(l);
      txData[4] = LSB(l);
      tx = true;
    }
    else if(pid == ENGINE_COOLANT_TEMP){    // Engine coolant temperature
      txData[0] = 0x03;
      txData[3] = int(valueFields[F_COOLANT_TEMP] + 40);
      tx = true;
    }
    else if(pid == FUEL_PRESSURE) {
      txData[0] = 0x04;
      int p = isEngineRunning() ? DEFAULT_INJECTION_PRESSURE : 0;
      txData[3] = MSB(p);
      txData[4] = LSB(p);
      tx = true;
    }
    else if(pid == FUEL_RAIL_PRES_ALT ||
        pid == ABS_FUEL_RAIL_PRES) {
      txData[0] = 0x04;
      int p = isEngineRunning() ? (DEFAULT_INJECTION_PRESSURE * 10) : 0;
      txData[3] = MSB(p);
      txData[4] = LSB(p);
      tx = true;
    }
    else if(pid == FUEL_LEVEL) {
      txData[0] = 0x03;
      int fuelPercentage = ( (int(valueFields[F_FUEL]) * 100) / (FUEL_MIN - FUEL_MAX));
      if(fuelPercentage > 100) {
        fuelPercentage = 100;
      }
      txData[3] = percentToGivenVal(fuelPercentage, 255);

      tx = true;
    }
    else if(pid == INTAKE_PRESSURE){    // Intake manifold absolute pressure
      txData[0] = 0x03;
      
      int intake_Pressure = (valueFields[F_PRESSURE] * 255.0f / 2.55f);
      if(intake_Pressure > 255) {
        intake_Pressure = 255;
      }
      txData[3] = intake_Pressure;
      tx = true;
    }
    else if(pid == ENGINE_RPM){    // Engine RPM
      txData[0] = 0x04;
      int engine_Rpm = int(valueFields[F_RPM] * 4);
      txData[3] = MSB(engine_Rpm);
      txData[4] = LSB(engine_Rpm);
      tx = true;
    }
    else if(pid == VEHICLE_SPEED){    // Vehicle speed
      txData[0] = 0x03;
      txData[3] = int(valueFields[F_CAR_SPEED]);
      tx = true;
    }
    else if(pid == INTAKE_TEMP ||
        pid == AMB_AIR_TEMP){    // Intake air temperature
      txData[0] = 0x03;
      txData[3] = int(valueFields[F_INTAKE_TEMP] + 40);
      tx = true;
    }
    else if(pid == THROTTLE ||
        pid == REL_ACCEL_POS ||
        pid == REL_THROTTLE_POS ||
        pid == ABS_THROTTLE_POS_B ||
        pid == ABS_THROTTLE_POS_C ||
        pid == ACCEL_POS_D ||
        pid == ACCEL_POS_E ||
        pid == ACCEL_POS_F ||
        pid == COMMANDED_THROTTLE) { // Throttle position
      txData[0] = 0x03;
      float percent = (valueFields[F_THROTTLE_POS] * 100) / PWM_RESOLUTION;
      txData[3] = percentToGivenVal(percent, 255);
      tx = true;
    }
    else if(pid == OBDII_STANDARDS){    // OBD standards this vehicle conforms to
      txData[0] = 0x04;
      txData[3] = EOBD_OBD_OBD_II;
      tx = true;
    }
    else if(pid == ENGINE_RUNTIME){
      txData[0] = 0x04;
      txData[3] = 10;
      txData[4] = 10;
      tx = true;
    }
    else if(pid == PID_21_40){    // Supported PIDs 21-40
      txData[0] = 0x06;
      
      txData[3] = 0xff;
      txData[4] = 0xff;
      txData[5] = 0xff;
      txData[6] = 0xff;
      tx = true;
    }
    else if(pid == CAT_TEMP_B1S1 ||
      pid == CAT_TEMP_B1S2 ||
      pid == CAT_TEMP_B2S1 ||
      pid == CAT_TEMP_B2S2) {
      txData[0] = 0x04;

      int temp = (int(valueFields[F_EGT]) + 40) * 10;
      txData[3] = MSB(temp);
      txData[4] = LSB(temp);
      tx = true;
    }
    else if(pid == PID_41_60){    // Supported PIDs 41-60
      txData[0] = 0x06;
      
      txData[3] = 0xff;
      txData[4] = 0xff;
      txData[5] = 0xff;
      txData[6] = 0xff;
      tx = true;
    }
    else if(pid == ECU_VOLTAGE){    // Control module voltage

      txData[0] = 0x04;
      
      int volt = int(valueFields[F_VOLTS] * 1024);
      txData[3] = MSB(volt);
      txData[4] = LSB(volt);
      tx = true;    
    }
    else if(pid == FUEL_TYPE){    // Fuel Type
      txData[0] = 0x03;
      txData[3] = FUEL_TYPE_DIESEL;
      tx = true;
    }
    else if(pid == ENGINE_OIL_TEMP){    // Engine oil Temperature
      txData[0] = 0x03;
      txData[3] = int(valueFields[F_OIL_TEMP] + 40);
      tx = true;
    }
    else if(pid == FUEL_TIMING){    // Fuel injection timing
      txData[0] = 0x04;
      
      txData[3] = 0x61;
      txData[4] = 0x80;
      tx = true;
    }
    else if(pid == FUEL_RATE){    // Engine fuel rate
      txData[0] = 0x04;
      
      txData[3] = 0x07;
      txData[4] = 0xD0;
      tx = true;
    }
    else if(pid == EMISSIONS_STANDARD){    // Emissions requirements to which vehicle is designed
      txData[0] = 0x03;
      txData[3] = EURO_3;
      tx = true;
    }
    else if(pid == PID_61_80){    // Supported PIDs 61-80
      txData[0] = 0x06;
      
      txData[3] = 0xff;
      txData[4] = 0xff;
      txData[5] = 0xff;
      txData[6] = 0xff;
      tx = true;
    }
    else if(pid == P_DPF_TEMP) {

      txData[0] = 0x04;

      txData[3] = 0x40;
      txData[4] = 0x00;
      txData[5] = 0x00;
      txData[6] = 0x00;

      tx = true;
    }
    else if(pid == PID_81_A0){    // Supported PIDs 81-A0
      txData[0] = 0x06;
      
      txData[3] = 0x00;
      txData[4] = 0x00;
      txData[5] = 0x00;
      txData[6] = 0x01;
      tx = true;
    }
    else if(pid == PID_A1_C0){    // Supported PIDs A1-C0
      txData[0] = 0x06;
      
      txData[3] = 0x00;
      txData[4] = 0x00;
      txData[5] = 0x00;
      txData[6] = 0x01;
      tx = true;
    }
    else if(pid == PID_C1_E0){    // Supported PIDs C1-E0
      txData[0] = 0x06;
      
      txData[3] = 0x00;
      txData[4] = 0x00;
      txData[5] = 0x00;
      txData[6] = 0x01;
      tx = true;
    }
    else if(pid == PID_E1_FF){    // Supported PIDs E1-FF?
      txData[0] = 0x06;
      
      txData[3] = 0x00;
      txData[4] = 0x00;
      txData[5] = 0x00;
      txData[6] = 0x00;
      tx = true;
    }
    else{
      unsupported(mode, pid);
    }
  }
  
  //=============================================================================
  // MODE $02 - Show freeze frame data
  //=============================================================================
  else if(mode == L2){
      unsupported(mode, pid);
  }
  
  //=============================================================================
  // MODE $03 - Show stored DTCs
  //=============================================================================
  else if(mode == L3){
      byte DTCs[] = {(byte)(0x40 | mode), 0x05, 0xC0, 0xBA, 0x00, 0x11, 0x80, 0x13, 0x90, 0x45, 0xA0, 0x31};
      iso_tp(mode, pid, 12, DTCs);
  }
  
  //=============================================================================
  // MODE $04 - Clear DTCs and stored values
  //=============================================================================
  else if(mode == L4){
      // Need to cleat DTCs.  We just acknowledge the command for now.
      txData[0] = 0x01;
      tx = true;
  }
  
  //=============================================================================
  // MODE $05 - Test Results, oxygen sensor monitoring (non CAN only)
  //=============================================================================
  else if(mode == L5){
      unsupported(mode, pid);
  }
  
  //=============================================================================
  // MODE $06 - Test Results, On-Board Monitoring (Oxygen sensor monitoring for CAN only)
  //=============================================================================
  else if(mode == L6){
    if(pid == 0x00){        // Supported PIDs 01-20
      txData[0] = 0x06;
      
      txData[3] = 0x00;
      txData[4] = 0x00;
      txData[5] = 0x00;
      txData[6] = 0x00;
      tx = true;
    }
    else{
      unsupported(mode, pid);
    }
  }
  
  //=============================================================================
  // MODE $07 - Show pending DTCs (Detected during current or last driving cycle)
  //=============================================================================
  else if(mode == L7){
      byte DTCs[] = {(byte)(0x40 | mode), 0x05, 0xC0, 0xBA, 0x00, 0x11, 0x80, 0x13, 0x90, 0x45, 0xA0, 0x31};
      iso_tp(mode, pid, 12, DTCs);
  }
  
  //=============================================================================
  // MODE $08 - Control operation of on-board component/system
  //=============================================================================
  else if(mode == L8){
      unsupported(mode, pid);
  }
  
  //=============================================================================
  // MODE $09 - Request vehcile information
  //=============================================================================
  else if(mode == L9){
    if(pid == 0x00){        // Supported PIDs 01-20
      txData[0] = 0x06;
      
      txData[3] = 0x54;
      txData[4] = 0x40;
      txData[5] = 0x00;
      txData[6] = 0x00;
      tx = true;
    }
//    else if(pid == 0x01){    // VIN message count for PID 02. (Only for ISO 9141-2, ISO 14230-4 and SAE J1850.)
//    }
    else if(pid == REQUEST_VIN){    // VIN (17 to 20 Bytes) Uses ISO-TP
      byte VIN[] = {(byte)(0x40 | mode), pid, 0x01, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD};
      for(int a = 0; a < (int)strlen(vehicle_Vin); a++) {
        VIN[a + 3] = (byte)vehicle_Vin[a];
      }
      iso_tp(mode, pid, 20, VIN);
    }
//    else if(pid == 0x03){    // Calibration ID message count for PID 04. (Only for ISO 9141-2, ISO 14230-4 and SAE J1850.)
//    }
    else if(pid == 0x04){    // Calibration ID
      byte CID[] = {(byte)(0x40 | mode), pid, 0x01, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD};
      storeECUName(CID, 3);
      iso_tp(mode, pid, 18, CID);
    }
//    else if(pid == 0x05){    // Calibration Verification Number (CVN) message count for PID 06. (Only for ISO 9141-2, ISO 14230-4 and SAE J1850.)
//    }
    else if(pid == 0x06){    // CVN
      byte CVN[] = {(byte)(0x40 | mode), pid, 0x02, 0x11, 0x42, 0x42, 0x42, 0x22, 0x43, 0x43, 0x43};
      iso_tp(mode, pid, 11, CVN);
    }
//    else if(pid == 0x07){    // In-use performance tracking message count for PID 08 and 0B. (Only for ISO 9141-2, ISO 14230-4 and SAE J1850.)
//    }
//    else if(pid == 0x08){    // In-use performance tracking for spark ignition vehicles.
//    }
    else if(pid == 0x09){    // ECU name message count for PID 0A.
      byte ECUname[] = {(byte)(0x40 | mode), pid, 0x01, 'E', 'C', 'U', 0x00, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD};
      storeECUName(ECUname, 7);
      iso_tp(mode, pid, 23, ECUname);
    }
    else if(pid == 0x0A){    // ECM Name
      byte ECMname[] = {(byte)(0x40 | mode), pid, 0x01, 'E', 'C', 'M', 0x00, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD};
      storeECUName(ECMname, 7);
      iso_tp(mode, pid, 23, ECMname);
    }
//    else if(pid == 0x0B){    // In-use performance tracking for compression ignition vehicles.
//    }
//    else if(pid == 0x0C){    // ESN message count for PID 0D.
//    }
    else if(pid == 0x0D){    // ESN
      byte ESN[] = {(byte)(0x40 | mode), pid, 0x01, 0x41, 0x72, 0x64, 0x75, 0x69, 0x6E, 0x6F, 0x2D, 0x4F, 0x42, 0x44, 0x49, 0x49, 0x73, 0x69, 0x6D, 0x00};
      iso_tp(mode, pid, 20, ESN);
    }
    else{
      unsupported(mode, pid); 
    }
  }
  
  //=============================================================================
  // MODE $0A - Show permanent DTCs 
  //=============================================================================
  else if(mode == L10){
      byte DTCs[] = {(byte)(0x40 | mode), 0x05, 0xC0, 0xBA, 0x00, 0x11, 0x80, 0x13, 0x90, 0x45, 0xA0, 0x31};
      iso_tp(mode, pid, 12, DTCs);
  }
  
  // UDS Modes: Diagonstic and Communications Management =======================================
  //=============================================================================
  // MODE $10 - Diagnostic Session Control
  //=============================================================================
//  else if(mode == 0x10){
//      txData[0] = 0x03;
//      
//      txData[2] = mode;
//      txData[3] = 0x00;
//      tx = true;
//  }
  
  //=============================================================================
  // MODE $11 - ECU Reset
  //=============================================================================
//  else if(mode == 0x11){
//      txData[0] = 0x02;
//      
//      txData[2] = mode;
//      tx = true;
//  }
  
  //=============================================================================
  // MODE $27 - Security Access
  //=============================================================================
//  else if(mode == 0x27){
//      txData[0] = 0x02;
//      
//      txData[2] = mode;
//      txData[3] = 0x00;
//      tx = true;
//  }
  
  //=============================================================================
  // MODE $28 - Communication Control
  //=============================================================================
//  else if(mode == 0x28){
//      txData[0] = 0x03;
//      
//      txData[2] = mode;
//      txData[3] = 0x00;
//      tx = true;
//  }
  
  //=============================================================================
  // MODE $3E - Tester Present
  //=============================================================================
//  else if(mode == 0x3E){
//      txData[0] = 0x03;
//      
//      txData[2] = mode;
//      txData[3] = 0x00;
//      tx = true;
//  }
  
  //=============================================================================
  // MODE $83 - Access Timing Parameters
  //=============================================================================
//  else if(mode == 0x83){
//      txData[0] = 0x03;
//      
//      txData[2] = mode;
//      txData[3] = 0x00;
//      tx = true;
//  }
  
  //=============================================================================
  // MODE $84 - Secured Data Transmission
  //=============================================================================
//  else if(mode == 0x84){
//      txData[0] = 0x03;
//      
//      txData[2] = mode;
//      txData[3] = 0x00;
//      tx = true;
//  }
//  
  //=============================================================================
  // MODE $85 - Control DTC Sentings
  //=============================================================================
//  else if(mode == 0x85){
//      txData[0] = 0x03;
//      
//      txData[2] = mode;
//      txData[3] = 0x00;
//      tx = true;
//  }
  
  //=============================================================================
  // MODE $86 - Response On Event
  //=============================================================================
//  else if(mode == 0x86){
//      txData[0] = 0x03;
//      
//      txData[2] = mode;
//      txData[3] = 0x00;
//      tx = true;
//  }
  
  //=============================================================================
  // MODE $87 - Link Control
  //=============================================================================
//  else if(mode == 0x87){
//      txData[0] = 0x03;
//      
//      txData[2] = mode;
//      txData[3] = 0x00;
//      tx = true;
//  }
  
  // UDS Modes: Data Transmission ==============================================================
  //=============================================================================
  // MODE $22 - Read Data By Identifier
  //=============================================================================
//  else if(mode == 0x22){
//      txData[0] = 0x03;
//      
//      txData[2] = mode;
//      txData[3] = 0x00;
//      tx = true;
//  }
  
  //=============================================================================
  // MODE $23 - Read Memory By Address
  //=============================================================================
//  else if(mode == 0x23){
//      txData[0] = 0x02;
//      
//      txData[2] = mode;
//      tx = true;
//  }
  
  //=============================================================================
  // MODE $24 - Read Scaling Data By Identifier
  //=============================================================================
//  else if(mode == 0x24){
//      txData[0] = 0x02;
//      
//      txData[2] = mode;
//      txData[3] = 0x00;
//      tx = true;
//  }
  
  //=============================================================================
  // MODE $2A - Read Data By Periodic Identifier
  //=============================================================================
//  else if(mode == 0x2A){
//      txData[0] = 0x03;
//      
//      txData[2] = mode;
//      txData[3] = 0x00;
//      tx = true;
//  }
  
  //=============================================================================
  // MODE $2C - Dynamically Define Data Identifier
  //=============================================================================
//  else if(mode == 0x2C){
//      txData[0] = 0x03;
//      
//      txData[2] = mode;
//      txData[3] = 0x00;
//      tx = true;
//  }
  
  //=============================================================================
  // MODE $2E - Write Data By Identifier
  //=============================================================================
//  else if(mode == 0x2E){
//      txData[0] = 0x03;
//      
//      txData[2] = mode;
//      txData[3] = 0x00;
//      tx = true;
//  }
  
  //=============================================================================
  // MODE $3D - Write Memory By Address
  //=============================================================================
//  else if(mode == 0x3D){
//      txData[0] = 0x03;
//      
//      txData[2] = mode;
//      txData[3] = 0x00;
//      tx = true;
//  }
  
  // UDS Modes: Stored Data Transmission =======================================================
  //=============================================================================
  // MODE $14 - Clear Diagnostic Information
  //=============================================================================
//  else if(mode == 0x14){
//      txData[0] = 0x03;
//      
//      txData[2] = mode;
//      txData[3] = 0x00;
//      tx = true;
//  }
  
  //=============================================================================
  // MODE $19 - Read DTC Information
  //=============================================================================
//  else if(mode == 0x19){
//      txData[0] = 0x02;
//      
//      txData[2] = mode;
//      tx = true;
//  }
  
  // UDS Modes: Input Output Control ===========================================================
  //=============================================================================
  // MODE $2F - Input Output Control By Identifier
  //=============================================================================
//  else if(mode == 0x2F){
//      txData[0] = 0x03;
//      
//      txData[2] = mode;
//      txData[3] = 0x00;
//      tx = true;
//  }
  
  // UDS Modes: Remote Activation of Routine ===================================================
  //=============================================================================
  // MODE $31 - Routine Control
  //=============================================================================
//  else if(mode == 0x2F){
//      txData[0] = 0x03;
//      
//      txData[2] = mode;
//      txData[3] = 0x00;
//      tx = true;
//  }
  
  // UDS Modes: Upload / Download ==============================================================
  //=============================================================================
  // MODE $34 - Request Download
  //=============================================================================
//  else if(mode == 0x34){
//      txData[0] = 0x03;
//      
//      txData[2] = mode;
//      txData[3] = 0x00;
//      tx = true;
//  }
  
  //=============================================================================
  // MODE $35 - Request Upload
  //=============================================================================
//  else if(mode == 0x35){
//      txData[0] = 0x02;
//      
//      txData[2] = mode;
//      tx = true;
//  }
  
  //=============================================================================
  // MODE $36 - Transfer Data
  //=============================================================================
//  else if(mode == 0x36){
//      txData[0] = 0x02;
//      
//      txData[2] = mode;
//      txData[3] = 0x00;
//      tx = true;
//  }
  
  //=============================================================================
  // MODE $37 - Request Transfer Exit
  //=============================================================================
//  else if(mode == 0x37){
//      txData[0] = 0x03;
//      
//      txData[2] = mode;
//      txData[3] = 0x00;
//      tx = true;
//  }
  
  //=============================================================================
  // MODE $38 - Request File Transfer
  //=============================================================================
//  else if(mode == 0x38){
//      txData[0] = 0x03;
//      
//      txData[2] = mode;
//      txData[3] = 0x00;
//      tx = true;
//  }
  else { 
    unsupported(mode, pid);
  }
  
  if(tx)
    CAN0.sendMsgBuf(REPLY_ID, 8, txData);
}


// Generic debug serial output
void unsupported(byte mode, byte pid){
  negAck(mode, 0x12);
  unsupportedPrint(mode, pid);  
}


// Generic debug serial output
void negAck(byte mode, byte reason){
  byte txData[] = {0x03,0x7F,mode,reason,PAD,PAD,PAD,PAD};
  CAN0.sendMsgBuf(REPLY_ID, 8, txData);
}


// Generic debug serial output
void unsupportedPrint(byte mode, byte pid){
  char msgstring[64];
  snprintf(msgstring, sizeof(msgstring) - 1, "Mode $%02X: Unsupported PID $%02X requested!", mode, pid);
  deb(msgstring);
}


// Blocking example of ISO transport
void iso_tp(byte mode, byte pid, int len, byte *data){
  byte tpData[8];
  int offset = 0;
  byte index = 0;
//  byte packetcnt = ((len & 0x0FFF) - 6) / 7;
//  if((((len & 0x0FFF) - 6) % 7) > 0)
//    packetcnt++;

  // First frame
  tpData[0] = 0x10 | ((len >> 8) & 0x0F);
  tpData[1] = 0x00FF & len;
  for(byte i=2; i<8; i++){
    tpData[i] = data[offset++];
  }
  CAN0.sendMsgBuf(REPLY_ID, 8, tpData);
  index++; // We sent a packet so increase our index.
  
  bool not_done = true;
  unsigned long sepPrev = millis();
  byte sepInvl = 0;
  byte frames = 0;
  bool lockout = false;
  while(not_done){
    // Need to wait for flow frame
    if(!digitalRead(CAN1_INT)){
      CAN0.readMsgBuf(&rxId, &dlc, rxBuf);
    
      if((rxId == LISTEN_ID) && ((rxBuf[0] & 0xF0) == 0x30)){
        if((rxBuf[0] & 0x0F) == 0x00){
          // Continue
          frames = rxBuf[1];
          sepInvl = rxBuf[2];
          lockout = true;
        } else if((rxBuf[0] & 0x0F) == 0x01){
          // Wait
          lockout = false;
          delay(rxBuf[2]);
        } else if((rxBuf[0] & 0x0F) == 0x03){
          // Abort
          not_done = false;
          return;
        }
      }
    }

    if(((millis() - sepPrev) >= sepInvl) && lockout){
      sepPrev = millis();

      tpData[0] = 0x20 | index++;
      for(byte i=1; i<8; i++){
        if(offset != len)
          tpData[i] = data[offset++];
        else
          tpData[i] = 0x00;
      }
      
      // Do consecutive frames as instructed via flow frame
      CAN0.sendMsgBuf(REPLY_ID, 8, tpData);
      
      if(frames-- == 1)
        lockout = false;
    }

    if(offset == len)
      not_done = false;
    else{
      char msgstring[32];
      snprintf(msgstring, sizeof(msgstring) - 1, "Offset: 0x%04X\tLen: 0x%04X", offset, len);
      Serial.println(msgstring);
    }
    // Timeout
    if((millis() - sepPrev) >= 1000)
      not_done = false;
  }
}

