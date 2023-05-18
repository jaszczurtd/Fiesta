
#include "obd-2.h"

const char PID_NAME_0x00[] PROGMEM = "PIDs supported [01 - 20]";
const char PID_NAME_0x01[] PROGMEM = "Monitor status since DTCs cleared";
const char PID_NAME_0x02[] PROGMEM = "Freeze DTC";
const char PID_NAME_0x03[] PROGMEM = "Fuel system status";
const char PID_NAME_0x04[] PROGMEM = "Calculated engine load";
const char PID_NAME_0x05[] PROGMEM = "Engine coolant temperature";
const char PID_NAME_0x06[] PROGMEM = "Short term fuel trim — Bank 1";
const char PID_NAME_0x07[] PROGMEM = "Long term fuel trim — Bank 1";
const char PID_NAME_0x08[] PROGMEM = "Short term fuel trim — Bank 2";
const char PID_NAME_0x09[] PROGMEM = "Long term fuel trim — Bank 2";
const char PID_NAME_0x0a[] PROGMEM = "Fuel pressure";
const char PID_NAME_0x0b[] PROGMEM = "Intake manifold absolute pressure";
const char PID_NAME_0x0c[] PROGMEM = "Engine RPM";
const char PID_NAME_0x0d[] PROGMEM = "Vehicle speed";
const char PID_NAME_0x0e[] PROGMEM = "Timing advance";
const char PID_NAME_0x0f[] PROGMEM = "Intake air temperature";
const char PID_NAME_0x10[] PROGMEM = "MAF air flow rate";
const char PID_NAME_0x11[] PROGMEM = "Throttle position";
const char PID_NAME_0x12[] PROGMEM = "Commanded secondary air status";
const char PID_NAME_0x13[] PROGMEM = "Oxygen sensors present (in 2 banks)";
const char PID_NAME_0x14[] PROGMEM = "Oxygen Sensor 1 - Short term fuel trim";
const char PID_NAME_0x15[] PROGMEM = "Oxygen Sensor 2 - Short term fuel trim";
const char PID_NAME_0x16[] PROGMEM = "Oxygen Sensor 3 - Short term fuel trim";
const char PID_NAME_0x17[] PROGMEM = "Oxygen Sensor 4 - Short term fuel trim";
const char PID_NAME_0x18[] PROGMEM = "Oxygen Sensor 5 - Short term fuel trim";
const char PID_NAME_0x19[] PROGMEM = "Oxygen Sensor 6 - Short term fuel trim";
const char PID_NAME_0x1a[] PROGMEM = "Oxygen Sensor 7 - Short term fuel trim";
const char PID_NAME_0x1b[] PROGMEM = "Oxygen Sensor 8 - Short term fuel trim";
const char PID_NAME_0x1c[] PROGMEM = "OBD standards this vehicle conforms to";
const char PID_NAME_0x1d[] PROGMEM = "Oxygen sensors present (in 4 banks)";
const char PID_NAME_0x1e[] PROGMEM = "Auxiliary input status";
const char PID_NAME_0x1f[] PROGMEM = "Run time since engine start";
const char PID_NAME_0x20[] PROGMEM = "PIDs supported [21 - 40]";
const char PID_NAME_0x21[] PROGMEM = "Distance traveled with malfunction indicator lamp (MIL) on";
const char PID_NAME_0x22[] PROGMEM = "Fuel Rail Pressure (relative to manifold vacuum)";
const char PID_NAME_0x23[] PROGMEM = "Fuel Rail Gauge Pressure (diesel, or gasoline direct injection)";
const char PID_NAME_0x24[] PROGMEM = "Oxygen Sensor 1 - Fuel–Air Equivalence Ratio";
const char PID_NAME_0x25[] PROGMEM = "Oxygen Sensor 2 - Fuel–Air Equivalence Ratio";
const char PID_NAME_0x26[] PROGMEM = "Oxygen Sensor 3 - Fuel–Air Equivalence Ratio";
const char PID_NAME_0x27[] PROGMEM = "Oxygen Sensor 4 - Fuel–Air Equivalence Ratio";
const char PID_NAME_0x28[] PROGMEM = "Oxygen Sensor 5 - Fuel–Air Equivalence Ratio";
const char PID_NAME_0x29[] PROGMEM = "Oxygen Sensor 6 - Fuel–Air Equivalence Ratio";
const char PID_NAME_0x2a[] PROGMEM = "Oxygen Sensor 7 - Fuel–Air Equivalence Ratio";
const char PID_NAME_0x2b[] PROGMEM = "Oxygen Sensor 8 - Fuel–Air Equivalence Ratio";
const char PID_NAME_0x2c[] PROGMEM = "Commanded EGR";
const char PID_NAME_0x2d[] PROGMEM = "EGR Error";
const char PID_NAME_0x2e[] PROGMEM = "Commanded evaporative purge";
const char PID_NAME_0x2f[] PROGMEM = "Fuel Tank Level Input";
const char PID_NAME_0x30[] PROGMEM = "Warm-ups since codes cleared";
const char PID_NAME_0x31[] PROGMEM = "Distance traveled since codes cleared";
const char PID_NAME_0x32[] PROGMEM = "Evap. System Vapor Pressure";
const char PID_NAME_0x33[] PROGMEM = "Absolute Barometric Pressure";
const char PID_NAME_0x34[] PROGMEM = "Oxygen Sensor 1 - Fuel–Air Equivalence Ratio";
const char PID_NAME_0x35[] PROGMEM = "Oxygen Sensor 2 - Fuel–Air Equivalence Ratio";
const char PID_NAME_0x36[] PROGMEM = "Oxygen Sensor 3 - Fuel–Air Equivalence Ratio";
const char PID_NAME_0x37[] PROGMEM = "Oxygen Sensor 4 - Fuel–Air Equivalence Ratio";
const char PID_NAME_0x38[] PROGMEM = "Oxygen Sensor 5 - Fuel–Air Equivalence Ratio";
const char PID_NAME_0x39[] PROGMEM = "Oxygen Sensor 6 - Fuel–Air Equivalence Ratio";
const char PID_NAME_0x3a[] PROGMEM = "Oxygen Sensor 7 - Fuel–Air Equivalence Ratio";
const char PID_NAME_0x3b[] PROGMEM = "Oxygen Sensor 8 - Fuel–Air Equivalence Ratio";
const char PID_NAME_0x3c[] PROGMEM = "Catalyst Temperature: Bank 1, Sensor 1";
const char PID_NAME_0x3d[] PROGMEM = "Catalyst Temperature: Bank 2, Sensor 1";
const char PID_NAME_0x3e[] PROGMEM = "Catalyst Temperature: Bank 1, Sensor 2";
const char PID_NAME_0x3f[] PROGMEM = "Catalyst Temperature: Bank 2, Sensor 2";
const char PID_NAME_0x40[] PROGMEM = "PIDs supported [41 - 60]";
const char PID_NAME_0x41[] PROGMEM = "Monitor status this drive cycle";
const char PID_NAME_0x42[] PROGMEM = "Control module voltage";
const char PID_NAME_0x43[] PROGMEM = "Absolute load value";
const char PID_NAME_0x44[] PROGMEM = "Fuel–Air commanded equivalence ratio";
const char PID_NAME_0x45[] PROGMEM = "Relative throttle position";
const char PID_NAME_0x46[] PROGMEM = "Ambient air temperature";
const char PID_NAME_0x47[] PROGMEM = "Absolute throttle position B";
const char PID_NAME_0x48[] PROGMEM = "Absolute throttle position C";
const char PID_NAME_0x49[] PROGMEM = "Absolute throttle position D";
const char PID_NAME_0x4a[] PROGMEM = "Absolute throttle position E";
const char PID_NAME_0x4b[] PROGMEM = "Absolute throttle position F";
const char PID_NAME_0x4c[] PROGMEM = "Commanded throttle actuator";
const char PID_NAME_0x4d[] PROGMEM = "Time run with MIL on";
const char PID_NAME_0x4e[] PROGMEM = "Time since trouble codes cleared";
const char PID_NAME_0x4f[] PROGMEM = "Maximum value for Fuel–Air equivalence ratio, oxygen sensor voltage, oxygen sensor current, and intake manifold absolute pressure";
const char PID_NAME_0x50[] PROGMEM = "Maximum value for air flow rate from mass air flow sensor";
const char PID_NAME_0x51[] PROGMEM = "Fuel Type";
const char PID_NAME_0x52[] PROGMEM = "Ethanol fuel percentage";
const char PID_NAME_0x53[] PROGMEM = "Absolute Evap system Vapor Pressure";
const char PID_NAME_0x54[] PROGMEM = "Evap system vapor pressure";
const char PID_NAME_0x55[] PROGMEM = "Short term secondary oxygen sensor trim";
const char PID_NAME_0x56[] PROGMEM = "Long term secondary oxygen sensor trim";
const char PID_NAME_0x57[] PROGMEM = "Short term secondary oxygen sensor trim";
const char PID_NAME_0x58[] PROGMEM = "Long term secondary oxygen sensor trim";
const char PID_NAME_0x59[] PROGMEM = "Fuel rail absolute pressure";
const char PID_NAME_0x5a[] PROGMEM = "Relative accelerator pedal position";
const char PID_NAME_0x5b[] PROGMEM = "Hybrid battery pack remaining life";
const char PID_NAME_0x5c[] PROGMEM = "Engine oil temperature";
const char PID_NAME_0x5d[] PROGMEM = "Fuel injection timing";
const char PID_NAME_0x5e[] PROGMEM = "Engine fuel rate";
const char PID_NAME_0x5f[] PROGMEM = "Emission requirements to which vehicle is designed";

const char* const PID_NAME_MAPPER[] PROGMEM = {
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

const char *getPIDName(int pid) {
  if(pid > PID_LAST) {
    return "unknown";
  }
  return PID_NAME_MAPPER[pid];
}
