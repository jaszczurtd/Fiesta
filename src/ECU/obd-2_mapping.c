
#include "obd-2.h"

const char PID_NAME_0x00[]  = "PIDs supported [01 - 20]";
const char PID_NAME_0x01[]  = "Monitor status since DTCs cleared";
const char PID_NAME_0x02[]  = "Freeze DTC";
const char PID_NAME_0x03[]  = "Fuel system status";
const char PID_NAME_0x04[]  = "Calculated engine load";
const char PID_NAME_0x05[]  = "Engine coolant temperature";
const char PID_NAME_0x06[]  = "Short term fuel trim - Bank 1";
const char PID_NAME_0x07[]  = "Long term fuel trim - Bank 1";
const char PID_NAME_0x08[]  = "Short term fuel trim - Bank 2";
const char PID_NAME_0x09[]  = "Long term fuel trim - Bank 2";
const char PID_NAME_0x0a[]  = "Fuel pressure";
const char PID_NAME_0x0b[]  = "Intake manifold absolute pressure";
const char PID_NAME_0x0c[]  = "Engine RPM";
const char PID_NAME_0x0d[]  = "Vehicle speed";
const char PID_NAME_0x0e[]  = "Timing advance";
const char PID_NAME_0x0f[]  = "Intake air temperature";
const char PID_NAME_0x10[]  = "MAF air flow rate";
const char PID_NAME_0x11[]  = "Throttle position";
const char PID_NAME_0x12[]  = "Commanded secondary air status";
const char PID_NAME_0x13[]  = "Oxygen sensors present (in 2 banks)";
const char PID_NAME_0x14[]  = "Oxygen Sensor 1 - Short term fuel trim";
const char PID_NAME_0x15[]  = "Oxygen Sensor 2 - Short term fuel trim";
const char PID_NAME_0x16[]  = "Oxygen Sensor 3 - Short term fuel trim";
const char PID_NAME_0x17[]  = "Oxygen Sensor 4 - Short term fuel trim";
const char PID_NAME_0x18[]  = "Oxygen Sensor 5 - Short term fuel trim";
const char PID_NAME_0x19[]  = "Oxygen Sensor 6 - Short term fuel trim";
const char PID_NAME_0x1a[]  = "Oxygen Sensor 7 - Short term fuel trim";
const char PID_NAME_0x1b[]  = "Oxygen Sensor 8 - Short term fuel trim";
const char PID_NAME_0x1c[]  = "OBD standards this vehicle conforms to";
const char PID_NAME_0x1d[]  = "Oxygen sensors present (in 4 banks)";
const char PID_NAME_0x1e[]  = "Auxiliary input status";
const char PID_NAME_0x1f[]  = "Run time since engine start";
const char PID_NAME_0x20[]  = "PIDs supported [21 - 40]";
const char PID_NAME_0x21[]  = "Distance traveled with malfunction indicator lamp (MIL) on";
const char PID_NAME_0x22[]  = "Fuel Rail Pressure (relative to manifold vacuum)";
const char PID_NAME_0x23[]  = "Fuel Rail Gauge Pressure (diesel, or gasoline direct injection)";
const char PID_NAME_0x24[]  = "Oxygen Sensor 1 - Fuel–Air Equivalence Ratio";
const char PID_NAME_0x25[]  = "Oxygen Sensor 2 - Fuel–Air Equivalence Ratio";
const char PID_NAME_0x26[]  = "Oxygen Sensor 3 - Fuel–Air Equivalence Ratio";
const char PID_NAME_0x27[]  = "Oxygen Sensor 4 - Fuel–Air Equivalence Ratio";
const char PID_NAME_0x28[]  = "Oxygen Sensor 5 - Fuel–Air Equivalence Ratio";
const char PID_NAME_0x29[]  = "Oxygen Sensor 6 - Fuel–Air Equivalence Ratio";
const char PID_NAME_0x2a[]  = "Oxygen Sensor 7 - Fuel–Air Equivalence Ratio";
const char PID_NAME_0x2b[]  = "Oxygen Sensor 8 - Fuel–Air Equivalence Ratio";
const char PID_NAME_0x2c[]  = "Commanded EGR";
const char PID_NAME_0x2d[]  = "EGR Error";
const char PID_NAME_0x2e[]  = "Commanded evaporative purge";
const char PID_NAME_0x2f[]  = "Fuel Tank Level Input";
const char PID_NAME_0x30[]  = "Warm-ups since codes cleared";
const char PID_NAME_0x31[]  = "Distance traveled since codes cleared";
const char PID_NAME_0x32[]  = "Evap. System Vapor Pressure";
const char PID_NAME_0x33[]  = "Absolute Barometric Pressure";
const char PID_NAME_0x34[]  = "Oxygen Sensor 1 - Fuel–Air Equivalence Ratio";
const char PID_NAME_0x35[]  = "Oxygen Sensor 2 - Fuel–Air Equivalence Ratio";
const char PID_NAME_0x36[]  = "Oxygen Sensor 3 - Fuel–Air Equivalence Ratio";
const char PID_NAME_0x37[]  = "Oxygen Sensor 4 - Fuel–Air Equivalence Ratio";
const char PID_NAME_0x38[]  = "Oxygen Sensor 5 - Fuel–Air Equivalence Ratio";
const char PID_NAME_0x39[]  = "Oxygen Sensor 6 - Fuel–Air Equivalence Ratio";
const char PID_NAME_0x3a[]  = "Oxygen Sensor 7 - Fuel–Air Equivalence Ratio";
const char PID_NAME_0x3b[]  = "Oxygen Sensor 8 - Fuel–Air Equivalence Ratio";
const char PID_NAME_0x3c[]  = "Catalyst Temperature: Bank 1, Sensor 1";
const char PID_NAME_0x3d[]  = "Catalyst Temperature: Bank 2, Sensor 1";
const char PID_NAME_0x3e[]  = "Catalyst Temperature: Bank 1, Sensor 2";
const char PID_NAME_0x3f[]  = "Catalyst Temperature: Bank 2, Sensor 2";
const char PID_NAME_0x40[]  = "PIDs supported [41 - 60]";
const char PID_NAME_0x41[]  = "Monitor status this drive cycle";
const char PID_NAME_0x42[]  = "Control module voltage";
const char PID_NAME_0x43[]  = "Absolute load value";
const char PID_NAME_0x44[]  = "Fuel–Air commanded equivalence ratio";
const char PID_NAME_0x45[]  = "Relative throttle position";
const char PID_NAME_0x46[]  = "Ambient air temperature";
const char PID_NAME_0x47[]  = "Absolute throttle position B";
const char PID_NAME_0x48[]  = "Absolute throttle position C";
const char PID_NAME_0x49[]  = "Absolute throttle position D";
const char PID_NAME_0x4a[]  = "Absolute throttle position E";
const char PID_NAME_0x4b[]  = "Absolute throttle position F";
const char PID_NAME_0x4c[]  = "Commanded throttle actuator";
const char PID_NAME_0x4d[]  = "Time run with MIL on";
const char PID_NAME_0x4e[]  = "Time since trouble codes cleared";
const char PID_NAME_0x4f[]  = "Maximum value for Fuel–Air equivalence ratio, oxygen sensor voltage, oxygen sensor current, and intake manifold absolute pressure";
const char PID_NAME_0x50[]  = "Maximum value for air flow rate from mass air flow sensor";
const char PID_NAME_0x51[]  = "Fuel Type";
const char PID_NAME_0x52[]  = "Ethanol fuel percentage";
const char PID_NAME_0x53[]  = "Absolute Evap system Vapor Pressure";
const char PID_NAME_0x54[]  = "Evap system vapor pressure";
const char PID_NAME_0x55[]  = "Short term secondary oxygen sensor trim";
const char PID_NAME_0x56[]  = "Long term secondary oxygen sensor trim";
const char PID_NAME_0x57[]  = "Short term secondary oxygen sensor trim";
const char PID_NAME_0x58[]  = "Long term secondary oxygen sensor trim";
const char PID_NAME_0x59[]  = "Fuel rail absolute pressure";
const char PID_NAME_0x5a[]  = "Relative accelerator pedal position";
const char PID_NAME_0x5b[]  = "Hybrid battery pack remaining life";
const char PID_NAME_0x5c[]  = "Engine oil temperature";
const char PID_NAME_0x5d[]  = "Fuel injection timing";
const char PID_NAME_0x5e[]  = "Engine fuel rate";
const char PID_NAME_0x5f[]  = "Emission requirements to which vehicle is designed";

const char* const PID_NAME_MAPPER[]  = {
  PID_NAME_0x00,
  PID_NAME_0x01,
  PID_NAME_0x02,
  PID_NAME_0x03,
  PID_NAME_0x04,
  PID_NAME_0x05,
  PID_NAME_0x06,
  PID_NAME_0x07,
  PID_NAME_0x08,
  PID_NAME_0x09,
  PID_NAME_0x0a,
  PID_NAME_0x0b,
  PID_NAME_0x0c,
  PID_NAME_0x0d,
  PID_NAME_0x0e,
  PID_NAME_0x0f,
  PID_NAME_0x10,
  PID_NAME_0x11,
  PID_NAME_0x12,
  PID_NAME_0x13,
  PID_NAME_0x14,
  PID_NAME_0x15,
  PID_NAME_0x16,
  PID_NAME_0x17,
  PID_NAME_0x18,
  PID_NAME_0x19,
  PID_NAME_0x1a,
  PID_NAME_0x1b,
  PID_NAME_0x1c,
  PID_NAME_0x1d,
  PID_NAME_0x1e,
  PID_NAME_0x1f,
  PID_NAME_0x20,
  PID_NAME_0x21,
  PID_NAME_0x22,
  PID_NAME_0x23,
  PID_NAME_0x24,
  PID_NAME_0x25,
  PID_NAME_0x26,
  PID_NAME_0x27,
  PID_NAME_0x28,
  PID_NAME_0x29,
  PID_NAME_0x2a,
  PID_NAME_0x2b,
  PID_NAME_0x2c,
  PID_NAME_0x2d,
  PID_NAME_0x2e,
  PID_NAME_0x2f,
  PID_NAME_0x30,
  PID_NAME_0x31,
  PID_NAME_0x32,
  PID_NAME_0x33,
  PID_NAME_0x34,
  PID_NAME_0x35,
  PID_NAME_0x36,
  PID_NAME_0x37,
  PID_NAME_0x38,
  PID_NAME_0x39,
  PID_NAME_0x3a,
  PID_NAME_0x3b,
  PID_NAME_0x3c,
  PID_NAME_0x3d,
  PID_NAME_0x3e,
  PID_NAME_0x3f,
  PID_NAME_0x40,
  PID_NAME_0x41,
  PID_NAME_0x42,
  PID_NAME_0x43,
  PID_NAME_0x44,
  PID_NAME_0x45,
  PID_NAME_0x46,
  PID_NAME_0x47,
  PID_NAME_0x48,
  PID_NAME_0x49,
  PID_NAME_0x4a,
  PID_NAME_0x4b,
  PID_NAME_0x4c,
  PID_NAME_0x4d,
  PID_NAME_0x4e,
  PID_NAME_0x4f,
  PID_NAME_0x50,
  PID_NAME_0x51,
  PID_NAME_0x52,
  PID_NAME_0x53,
  PID_NAME_0x54,
  PID_NAME_0x55,
  PID_NAME_0x56,
  PID_NAME_0x57,
  PID_NAME_0x58,
  PID_NAME_0x59,
  PID_NAME_0x5a,
  PID_NAME_0x5b,
  PID_NAME_0x5c,
  PID_NAME_0x5d,
  PID_NAME_0x5e,
  PID_NAME_0x5f,
};

/**
 * @brief Return the static name assigned to a standard PID.
 * @param pid PID number to look up.
 * @return Static PID description, or "unknown" when out of range.
 */
const char *getPIDName(int pid) {
  if(pid > PID_LAST) {
    return "unknown";
  }
  return PID_NAME_MAPPER[pid];
}

/**
 * @brief Return the static name assigned to a project DTC mapping.
 * @param code DTC code to look up.
 * @return Static DTC description, or "Unknown DTC" when unmapped.
 */
const char *getDtcName(uint16_t code) {
  switch(code) {
    case DTC_OBD_CAN_INIT_FAIL:
      return "U1900 Network CAN communication fault";
    case DTC_PCF8574_COMM_FAIL:
      return "U0073 Control module communication bus off";
    case DTC_PWM_CHANNEL_NOT_INIT:
      return "P0657 Actuator supply voltage A circuit/open";
    case DTC_DPF_COMM_LOST:
      return "U0100 Lost communication with DPF module (project mapping)";
    case DTC_EGT_COMM_LOST:
      return "U1902 Lost communication with EGT module";
    case DTC_CAN0_INIT_FAIL:
      return "U1903 CAN0 bus init failure";
    case DTC_GPS_SIGNAL_LOST:
      return "U1904 GPS data unavailable/stale";
    case DTC_SD_LOGGER_NOT_READY:
      return "U1905 SD logger missing/not initialized";
    case DTC_ISOTP_FC_TIMEOUT:
      return "U1906 ISO-TP flow-control timeout";
    case DTC_ISOTP_FC_ABORT:
      return "U1907 ISO-TP flow-control abort from tester";
    case DTC_ENGINE_OVERSPEED:
      return "P0219 Engine overspeed condition";
    case DTC_ECM_EEPROM_FAULT:
      return "P062F Internal control module EEPROM error";
    case DTC_SYSTEM_VOLTAGE_LOW:
      return "P0562 System voltage low";
    case DTC_SYSTEM_VOLTAGE_HIGH:
      return "P0563 System voltage high";
    case DTC_THROTTLE_RANGE_PERF:
      return "P0121 Throttle/Pedal position range/performance";
    case DTC_COOLANT_TEMP_RANGE:
      return "P0116 Engine coolant temperature range/performance";
    case DTC_INTAKE_TEMP_RANGE:
      return "P0111 Intake air temperature range/performance";
    case DTC_MAP_BARO_RANGE:
      return "P0106 MAP/BARO pressure range/performance";
    case DTC_FUEL_LEVEL_RANGE:
      return "P0460 Fuel level sensor range/performance";
    case DTC_ADJ_COMM_LOST:
      return "U1908 Lost communication with Adjustometer module";
    case DTC_ADJ_SIGNAL_LOST:
      return "U1909 Adjustometer oscillator signal lost";
    case DTC_ADJ_FUEL_TEMP_BROKEN:
      return "U190A Adjustometer fuel temperature sensor fault";
    case DTC_ADJ_VOLTAGE_BAD:
      return "U190B Adjustometer supply voltage out of range";
    default:
      return "Unknown DTC";
  }
}
