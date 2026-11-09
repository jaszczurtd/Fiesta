#ifndef FIESTA_OBD_DTC_CATALOG_H
#define FIESTA_OBD_DTC_CATALOG_H

#include <hal/core/hal_status.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file obd_dtc_catalog.h
 * @brief Diagnostic trouble codes: the generic SAE J2012 list and the
 * project's own codes, with their descriptions.
 *
 * One X-macro shape serves both lists: `X(name, value, description)`.
 * `name` is the code as printed on a scan tool (`P0116`, `U190C`), `value`
 * its two-byte SAE J2012 wire representation (bits 15..14 select the letter
 * P/C/B/U, bits 13..12 the first digit, bits 11..0 the three hex digits) and
 * `description` the text a reader sees.
 *
 * - `OBD_DTC_SAE_CATALOG`: the generic powertrain codes P0xxx, P2xxx and
 *   P3xxx of SAE J2012 / ISO 15031-6 and the generic network codes the
 *   project reports. The code numbers and descriptions are the standard's.
 *   The list was compiled against the rusEFI catalogue
 *   (`firmware/controllers/algo/obd_error_codes.h`, GPL-3.0) and completed
 *   from the standard where that catalogue keeps a code without its text;
 *   no rusEFI code is part of this file.
 * - `OBD_DTC_FIESTA_CATALOG`: the project's manufacturer-specific network
 *   codes U19xx.
 *
 * Both lists ascend by value, so the lookup table built from them is sorted
 * at compile time; the ECU host test `test_obd_dtc_catalog` checks the
 * order and the uniqueness. A new code goes into the list that owns its
 * range, at its place in the order. The ECU names the codes it raises in
 * `dtc_codes.h`; those aliases take their values from `obd_dtc_code_t`.
 */

/** @brief Characters of a printed code, with the terminator: `P0116`. */
#define OBD_DTC_TEXT_SIZE 6U

/** @brief One catalogue row: the wire value and its description. */
typedef struct {
  uint16_t code;           /**< Two-byte SAE J2012 representation. */
  const char *description; /**< Description, never NULL. */
} obd_dtc_entry_t;

/* clang-format off */
/** @brief Generic codes of SAE J2012 / ISO 15031-6, ascending by value. */
#define OBD_DTC_SAE_CATALOG(X) \
  X(P0001, 0x0001U, "Fuel Volume Regulator Control Circuit/Open") \
  X(P0002, 0x0002U, "Fuel Volume Regulator Control Circuit Range/Performance") \
  X(P0003, 0x0003U, "Fuel Volume Regulator Control Circuit Low") \
  X(P0004, 0x0004U, "Fuel Volume Regulator Control Circuit High") \
  X(P0005, 0x0005U, "Fuel Shutoff Valve \"A\" Control Circuit/Open") \
  X(P0006, 0x0006U, "Fuel Shutoff Valve \"A\" Control Circuit Low") \
  X(P0007, 0x0007U, "Fuel Shutoff Valve \"A\" Control Circuit High") \
  X(P0008, 0x0008U, "Engine Positions System Performance Bank 1") \
  X(P0009, 0x0009U, "Engine Position System Performance Bank 2") \
  X(P0010, 0x0010U, "\"A\" Camshaft Position Actuator Circuit (Bank 1)") \
  X(P0011, 0x0011U, "\"A\" Camshaft Position - Timing Over-Advanced or System Performance (Bank 1)") \
  X(P0012, 0x0012U, "\"A\" Camshaft Position - Timing Over-Retarded (Bank 1)") \
  X(P0013, 0x0013U, "\"B\" Camshaft Position - Actuator Circuit (Bank 1)") \
  X(P0014, 0x0014U, "\"B\" Camshaft Position - Timing Over-Advanced or System Performance (Bank 1)") \
  X(P0015, 0x0015U, "\"B\" Camshaft Position -Timing Over-Retarded (Bank 1)") \
  X(P0016, 0x0016U, "Crankshaft Position - Camshaft Position Correlation (Bank 1 Sensor A)") \
  X(P0017, 0x0017U, "Crankshaft Position - Camshaft Position Correlation (Bank 1 Sensor B)") \
  X(P0018, 0x0018U, "Crankshaft Position - Camshaft Position Correlation (Bank 2 Sensor A)") \
  X(P0019, 0x0019U, "Crankshaft Position - Camshaft Position Correlation (Bank 2 Sensor B)") \
  X(P0020, 0x0020U, "\"A\" Camshaft Position Actuator Circuit (Bank 2)") \
  X(P0021, 0x0021U, "\"A\" Camshaft Position - Timing Over-Advanced or System Performance (Bank 2)") \
  X(P0022, 0x0022U, "\"A\" Camshaft Position - Timing Over-Retarded (Bank 2)") \
  X(P0023, 0x0023U, "\"B\" Camshaft Position - Actuator Circuit (Bank 2)") \
  X(P0024, 0x0024U, "\"B\" Camshaft Position - Timing Over-Advanced or System Performance (Bank 2)") \
  X(P0025, 0x0025U, "\"B\" Camshaft Position - Timing Over-Retarded (Bank 2)") \
  X(P0026, 0x0026U, "Intake Valve Control Solenoid Circuit Range/Performance Bank 1") \
  X(P0027, 0x0027U, "Exhaust Valve Control solenoid Circuit Range/Performance Bank 1") \
  X(P0028, 0x0028U, "Intake valve Control Solenoid Circuit Range/Performance Bank 2") \
  X(P0029, 0x0029U, "Exhaust Valve Control Solenoid Circuit Range/Performance Bank 2") \
  X(P0030, 0x0030U, "HO2S Heater Control Circuit (Bank 1 Sensor 1)") \
  X(P0031, 0x0031U, "HO2S Heater Control Circuit Low (Bank 1 Sensor 1)") \
  X(P0032, 0x0032U, "HO2S Heater Control Circuit High (Bank 1 Sensor 1)") \
  X(P0033, 0x0033U, "Turbo Charger Bypass Valve Control Circuit") \
  X(P0034, 0x0034U, "Turbo Charger Bypass Valve Control Circuit Low") \
  X(P0035, 0x0035U, "Turbo Charger Bypass Valve Control Circuit High") \
  X(P0036, 0x0036U, "HO2S Heater Control Circuit (Bank 1 Sensor 2)") \
  X(P0037, 0x0037U, "HO2S Heater Control Circuit Low (Bank 1 Sensor 2)") \
  X(P0038, 0x0038U, "HO2S Heater Control Circuit High (Bank 1 Sensor 2)") \
  X(P0039, 0x0039U, "Turbo/Super Charger Bypass Valve Control Circuit Range/Performance") \
  X(P0040, 0x0040U, "Upstream Oxygen Sensors Swapped From Bank To Bank") \
  X(P0041, 0x0041U, "Downstream Oxygen Sensors Swapped From Bank To Bank") \
  X(P0042, 0x0042U, "HO2S Heater Control Circuit (Bank 1 Sensor 3)") \
  X(P0043, 0x0043U, "HO2S Heater Control Circuit Low (Bank 1 Sensor 3)") \
  X(P0044, 0x0044U, "HO2S Heater Control Circuit High (Bank 1 Sensor 3)") \
  X(P0050, 0x0050U, "HO2S Heater Control Circuit (Bank 2 Sensor 1)") \
  X(P0051, 0x0051U, "HO2S Heater Control Circuit Low (Bank 2 Sensor 1)") \
  X(P0052, 0x0052U, "HO2S Heater Control Circuit High (Bank 2 Sensor 1)") \
  X(P0053, 0x0053U, "HO2S Heater Resistance (Bank 1, Sensor 1)") \
  X(P0054, 0x0054U, "HO2S Heater Resistance (Bank 1, Sensor 2)") \
  X(P0055, 0x0055U, "HO2S Heater Resistance (Bank 1, Sensor 3)") \
  X(P0056, 0x0056U, "HO2S Heater Control Circuit (Bank 2 Sensor 2)") \
  X(P0057, 0x0057U, "HO2S Heater Control Circuit Low (Bank 2 Sensor 2)") \
  X(P0058, 0x0058U, "HO2S Heater Control Circuit High (Bank 2 Sensor 2)") \
  X(P0059, 0x0059U, "HO2S Heater Resistance (Bank 2, Sensor 1)") \
  X(P0060, 0x0060U, "HO2S Heater Resistance (Bank 2, Sensor 2)") \
  X(P0061, 0x0061U, "HO2S Heater Resistance (Bank 2, Sensor 3)") \
  X(P0062, 0x0062U, "HO2S Heater Control Circuit (Bank 2 Sensor 3)") \
  X(P0063, 0x0063U, "HO2S Heater Control Circuit Low (Bank 2 Sensor 3)") \
  X(P0064, 0x0064U, "HO2S Heater Control Circuit High (Bank 2 Sensor 3)") \
  X(P0065, 0x0065U, "Air Assisted Injector Control Range/Performance") \
  X(P0066, 0x0066U, "Air Assisted Injector Control Circuit or Circuit Low") \
  X(P0067, 0x0067U, "Air Assisted Injector Control Circuit High") \
  X(P0068, 0x0068U, "MAP/MAF - Throttle Position Correlation") \
  X(P0069, 0x0069U, "Manifold Absolute Pressure - Barometric Pressure Correlation") \
  X(P0070, 0x0070U, "Ambient Air Temperature Sensor Circuit") \
  X(P0071, 0x0071U, "Ambient Air Temperature Sensor Range/Performance") \
  X(P0072, 0x0072U, "Ambient Air Temperature Sensor Circuit Low Input") \
  X(P0073, 0x0073U, "Ambient Air Temperature Sensor Circuit High Input") \
  X(P0074, 0x0074U, "Ambient Air Temperature Sensor Circuit Intermittent") \
  X(P0075, 0x0075U, "Intake Valve Control Solenoid Circuit (Bank 1)") \
  X(P0076, 0x0076U, "Intake Valve Control Solenoid Circuit Low (Bank 1)") \
  X(P0077, 0x0077U, "Intake Valve Control Solenoid Circuit High (Bank 1)") \
  X(P0078, 0x0078U, "Exhaust Valve Control Solenoid Circuit (Bank 1)") \
  X(P0079, 0x0079U, "Exhaust Valve Control Solenoid Circuit Low (Bank 1)") \
  X(P0080, 0x0080U, "Exhaust Valve Control Solenoid Circuit High (Bank 1)") \
  X(P0081, 0x0081U, "Intake valve Control Solenoid Circuit (Bank 2)") \
  X(P0082, 0x0082U, "Intake Valve Control Solenoid Circuit Low (Bank 2)") \
  X(P0083, 0x0083U, "Intake Valve Control Solenoid Circuit High (Bank 2)") \
  X(P0084, 0x0084U, "Exhaust Valve Control Solenoid Circuit (Bank 2)") \
  X(P0085, 0x0085U, "Exhaust Valve Control Solenoid Circuit Low (Bank 2)") \
  X(P0086, 0x0086U, "Exhaust Valve Control Solenoid Circuit High (Bank 2)") \
  X(P0087, 0x0087U, "Fuel Rail/System Pressure - Too Low") \
  X(P0088, 0x0088U, "Fuel Rail/System Pressure - Too High") \
  X(P0089, 0x0089U, "Fuel Pressure Regulator 1 Performance") \
  X(P0090, 0x0090U, "Fuel Pressure Regulator 1 Control Circuit") \
  X(P0091, 0x0091U, "Fuel Pressure Regulator 1 Control Circuit Low") \
  X(P0092, 0x0092U, "Fuel Pressure Regulator 1 Control Circuit High") \
  X(P0093, 0x0093U, "Fuel System Leak Detected - Large Leak") \
  X(P0094, 0x0094U, "Fuel System Leak Detected - Small Leak") \
  X(P0095, 0x0095U, "Intake Air Temperature Sensor 2 Circuit") \
  X(P0096, 0x0096U, "Intake Air Temperature Sensor 2 Circuit Range/Performance") \
  X(P0097, 0x0097U, "Intake Air Temperature Sensor 2 Circuit Low") \
  X(P0098, 0x0098U, "Intake Air Temperature Sensor 2 Circuit High") \
  X(P0099, 0x0099U, "Intake Air Temperature Sensor 2 Circuit Intermittent/Erratic") \
  X(P0100, 0x0100U, "Mass or Volume Air Flow Circuit Malfunction") \
  X(P0101, 0x0101U, "Mass or Volume Air Flow Circuit Range/Performance Problem") \
  X(P0102, 0x0102U, "Mass or Volume Air Flow Circuit Low Input") \
  X(P0103, 0x0103U, "Mass or Volume Air Flow Circuit High Input") \
  X(P0104, 0x0104U, "Mass or Volume Air Flow Circuit Intermittent") \
  X(P0105, 0x0105U, "Manifold Absolute Pressure/Barometric Pressure Circuit Malfunction") \
  X(P0106, 0x0106U, "Manifold Absolute Pressure/Barometric Pressure Circuit Range/Performance Problem") \
  X(P0107, 0x0107U, "Manifold Absolute Pressure/Barometric Pressure Circuit Low Input") \
  X(P0108, 0x0108U, "Manifold Absolute Pressure/Barometric Pressure Circuit High Input") \
  X(P0109, 0x0109U, "Manifold Absolute Pressure/Barometric Pressure Circuit Intermittent") \
  X(P0110, 0x0110U, "Intake Air Temperature Circuit Malfunction") \
  X(P0111, 0x0111U, "Intake Air Temperature Circuit Range/Performance Problem") \
  X(P0112, 0x0112U, "Intake Air Temperature Circuit Low Input") \
  X(P0113, 0x0113U, "Intake Air Temperature Circuit High Input") \
  X(P0114, 0x0114U, "Intake Air Temperature Circuit Intermittent") \
  X(P0115, 0x0115U, "Engine Coolant Temperature Circuit Malfunction") \
  X(P0116, 0x0116U, "Engine Coolant Temperature Circuit Range/Performance Problem") \
  X(P0117, 0x0117U, "Engine Coolant Temperature Circuit Low Input") \
  X(P0118, 0x0118U, "Engine Coolant Temperature Circuit High Input") \
  X(P0119, 0x0119U, "Engine Coolant Temperature Circuit Intermittent") \
  X(P0120, 0x0120U, "Throttle Position Sensor/Switch A Circuit Malfunction") \
  X(P0121, 0x0121U, "Throttle Position Sensor/Switch A Circuit Range/Performance Problem") \
  X(P0122, 0x0122U, "Throttle Position Sensor/Switch A Circuit Low Input") \
  X(P0123, 0x0123U, "Throttle Position Sensor/Switch A Circuit High Input") \
  X(P0124, 0x0124U, "Throttle Position Sensor/Switch A Circuit Intermittent") \
  X(P0125, 0x0125U, "Insufficient Coolant Temperature for Closed Loop Fuel Control") \
  X(P0126, 0x0126U, "Insufficient Coolant Temperature for Stable Operation") \
  X(P0128, 0x0128U, "Coolant Thermostat (Coolant Temperature Below Thermostat Regulating Temperature)") \
  X(P0130, 0x0130U, "02 Sensor Circuit Malfunction (Bank I Sensor 1)") \
  X(P0131, 0x0131U, "02 Sensor Circuit Low Voltage (Bank I Sensor I)") \
  X(P0132, 0x0132U, "02 Sensor Circuit High Voltage (Bank I Sensor 1)") \
  X(P0133, 0x0133U, "02 Sensor Circuit Slow Response (Bank 1 Sensor 1)") \
  X(P0134, 0x0134U, "02 Sensor Circuit No Activity Detected (Bank I Sensor 1)") \
  X(P0135, 0x0135U, "02 Sensor Heater Circuit Malfunction (Bank 1 Sensor 1)") \
  X(P0136, 0x0136U, "02 Sensor Circuit Malfunction (Bank I Sensor 2)") \
  X(P0137, 0x0137U, "02 Sensor Circuit Low Voltage (Bank I Sensor 2)") \
  X(P0138, 0x0138U, "02 Sensor Circuit High Voltage (Bank I Sensor 2)") \
  X(P0139, 0x0139U, "02 Sensor Circuit Slow Response (Bank 1 Sensor 2)") \
  X(P0140, 0x0140U, "02 Sensor Circuit No Activity Detected (Bank 1 Sensor 2)") \
  X(P0141, 0x0141U, "02 Sensor Heater Circuit Malfunction (Bank 1 Sensor 2)") \
  X(P0142, 0x0142U, "02 Sensor Circuit Malfunction (Bank I Sensor 3)") \
  X(P0143, 0x0143U, "02 Sensor Circuit Low Voltage (Bank I Sensor 3)") \
  X(P0144, 0x0144U, "02 Sensor Circuit High Voltage (Bank I Sensor 3)") \
  X(P0145, 0x0145U, "02 Sensor Circuit Slow Response (Bank 1 Sensor 3)") \
  X(P0146, 0x0146U, "02 Sensor Circuit No Activity Detected (Bank I Sensor 3)") \
  X(P0147, 0x0147U, "02 Sensor Heater Circuit Malfunction (Bank I Sensor 3)") \
  X(P0150, 0x0150U, "02 Sensor Circuit Malfunction (Bank 2 Sensor I)") \
  X(P0151, 0x0151U, "02 Sensor Circuit Low Voltage (Bank 2 Sensor I)") \
  X(P0152, 0x0152U, "02 Sensor Circuit High Voltage (Bank 2 Sensor 1)") \
  X(P0153, 0x0153U, "02 Sensor Circuit Slow Response (Bank 2 Sensor 1)") \
  X(P0154, 0x0154U, "02 Sensor Circuit No Activity Detected (Bank 2 Sensor 1)") \
  X(P0155, 0x0155U, "02 Sensor Heater Circuit Malfunction (Bank 2 Sensor 1)") \
  X(P0156, 0x0156U, "02 Sensor Circuit Malfunction (Bank 2 Sensor 2)") \
  X(P0157, 0x0157U, "02 Sensor Circuit Low Voltage (Bank 2 Sensor 2)") \
  X(P0158, 0x0158U, "02 Sensor Circuit High Voltage (Bank 2 Sensor 2)") \
  X(P0159, 0x0159U, "02 Sensor Circuit Slow Response (Bank 2 Sensor 2)") \
  X(P0160, 0x0160U, "02 Sensor Circuit No Activity Detected (Bank 2 Sensor 2)") \
  X(P0161, 0x0161U, "02 Sensor Heater Circuit Malfunction (Bank 2 Sensor 2)") \
  X(P0162, 0x0162U, "02 Sensor Circuit Malfunction (Bank 2 Sensor 3)") \
  X(P0163, 0x0163U, "02 Sensor Circuit Low Voltage (Bank 2 Sensor 3)") \
  X(P0164, 0x0164U, "02 Sensor Circuit High Voltage (Bank 2 Sensor 3)") \
  X(P0165, 0x0165U, "02 Sensor Circuit Slow Response (Bank 2 Sensor 3)") \
  X(P0166, 0x0166U, "02 Sensor Circuit No Activity Detected (Bank 2 Sensor 3)") \
  X(P0167, 0x0167U, "02 Sensor Heater Circuit Malfunction (Bank 2 Sensor 3)") \
  X(P0170, 0x0170U, "Fuel Trim Malfunction (Bank 1)") \
  X(P0171, 0x0171U, "System too Lean (Bank 1)") \
  X(P0172, 0x0172U, "System too Rich (Bank 1)") \
  X(P0173, 0x0173U, "Fuel Trim Malfunction (Bank 2)") \
  X(P0174, 0x0174U, "System too Lean (Bank 2)") \
  X(P0175, 0x0175U, "System too Rich (Bank 2)") \
  X(P0176, 0x0176U, "Fuel Composition Sensor Circuit Malfunction") \
  X(P0177, 0x0177U, "Fuel Composition Sensor Circuit Range/Performance") \
  X(P0178, 0x0178U, "Fuel Composition Sensor Circuit Low Input") \
  X(P0179, 0x0179U, "Fuel Composition Sensor Circuit High Input") \
  X(P0180, 0x0180U, "Fuel Temperature Sensor A Circuit Malfunction") \
  X(P0181, 0x0181U, "Fuel Temperature Sensor A Circuit Range/Performance") \
  X(P0182, 0x0182U, "Fuel Temperature Sensor A Circuit Low Input") \
  X(P0183, 0x0183U, "Fuel Temperature Sensor A Circuit High Input") \
  X(P0184, 0x0184U, "Fuel Temperature Sensor A Circuit Intermittent") \
  X(P0185, 0x0185U, "Fuel Temperature Sensor B Circuit Malfunction") \
  X(P0186, 0x0186U, "Fuel Temperature Sensor B Circuit Range/Performance") \
  X(P0187, 0x0187U, "Fuel Temperature Sensor B Circuit Low Input") \
  X(P0188, 0x0188U, "Fuel Temperature Sensor B Circuit High Input") \
  X(P0189, 0x0189U, "Fuel Temperature Sensor B Circuit Intermittent") \
  X(P0190, 0x0190U, "Fuel Rail Pressure Sensor Circuit Malfunction") \
  X(P0191, 0x0191U, "Fuel Rail Pressure Sensor Circuit Range/Performance") \
  X(P0192, 0x0192U, "Fuel Rail Pressure Sensor Circuit Low Input") \
  X(P0193, 0x0193U, "Fuel Rail Pressure Sensor Circuit High Input") \
  X(P0194, 0x0194U, "Fuel Rail Pressure Sensor Circuit Intermittent") \
  X(P0195, 0x0195U, "Engine Oil Temperature Sensor Malfunction") \
  X(P0196, 0x0196U, "Engine Oil Temperature Sensor Range/Performance") \
  X(P0197, 0x0197U, "Engine Oil Temperature Sensor Low") \
  X(P0198, 0x0198U, "Engine Oil Temperature Sensor High") \
  X(P0199, 0x0199U, "Engine Oil Temperature Sensor Intermittent") \
  X(P0200, 0x0200U, "Injector Circuit Malfunction") \
  X(P0201, 0x0201U, "Injector Circuit Malfunction - Cylinder 1") \
  X(P0202, 0x0202U, "Injector Circuit Malfunction - Cylinder 2") \
  X(P0203, 0x0203U, "Injector Circuit Malfunction - Cylinder 3") \
  X(P0204, 0x0204U, "Injector Circuit Malfunction - Cylinder 4") \
  X(P0205, 0x0205U, "Injector Circuit Malfunction - Cylinder 5") \
  X(P0206, 0x0206U, "Injector Circuit Malfunction - Cylinder 6") \
  X(P0207, 0x0207U, "Injector Circuit Malfunction - Cylinder 7") \
  X(P0208, 0x0208U, "Injector Circuit Malfunction - Cylinder 8") \
  X(P0209, 0x0209U, "Injector Circuit Malfunction - Cylinder 9") \
  X(P0210, 0x0210U, "Injector Circuit Malfunction - Cylinder 10") \
  X(P0211, 0x0211U, "Injector Circuit Malfunction - Cylinder 11") \
  X(P0212, 0x0212U, "Injector Circuit Malfunction - Cylinder 12") \
  X(P0213, 0x0213U, "Cold Start Injector 1 Malfunction") \
  X(P0214, 0x0214U, "Cold Start Injector 2 Malfunction") \
  X(P0215, 0x0215U, "Engine Shutoff Solenoid Malfunction") \
  X(P0216, 0x0216U, "Injection Timing Control Circuit Malfunction") \
  X(P0217, 0x0217U, "Engine Overtemp Condition") \
  X(P0218, 0x0218U, "Transmission Over Temperature Condition") \
  X(P0219, 0x0219U, "Engine Overspeed Condition") \
  X(P0220, 0x0220U, "Throttle/Petal Position Sensor/Switch B Circuit Malfunction") \
  X(P0221, 0x0221U, "Throttle/Petal Position Sensor/Switch B Circuit Range/Performance Problem") \
  X(P0222, 0x0222U, "Throttle/Petal Position Sensor/Switch B Circuit Low Input") \
  X(P0223, 0x0223U, "Throttle/Petal Position Sensor/Switch B Circuit High Input") \
  X(P0224, 0x0224U, "Throttle/Petal Position Sensor/Switch B Circuit Intermittent") \
  X(P0225, 0x0225U, "Throttle/Petal Position Sensor/Switch C Circuit Malfunction") \
  X(P0226, 0x0226U, "Throttle/Petal Position Sensor/Switch C Circuit Range/Performance Problem") \
  X(P0227, 0x0227U, "Throttle/Petal Position Sensor/Switch C Circuit Low Input") \
  X(P0228, 0x0228U, "Throttle/Petal Position Sensor/Switch C Circuit High Input") \
  X(P0229, 0x0229U, "Throttle/Petal Position Sensor/Switch C Circuit Intermittent") \
  X(P0230, 0x0230U, "Fuel Pump Primary Circuit Malfunction") \
  X(P0231, 0x0231U, "Fuel Pump Secondary Circuit Low") \
  X(P0232, 0x0232U, "Fuel Pump Secondary Circuit High") \
  X(P0233, 0x0233U, "Fuel Pump Secondary Circuit Intermittent") \
  X(P0234, 0x0234U, "Turbocharger/Supercharger \"A\" Overboost Condition") \
  X(P0235, 0x0235U, "Turbocharger Boost Sensor A Circuit Malfunction") \
  X(P0236, 0x0236U, "Turbocharger Boost Sensor A Circuit Range/Performance") \
  X(P0237, 0x0237U, "Turbocharger Boost Sensor A Circuit Low") \
  X(P0238, 0x0238U, "Turbocharger Boost Sensor A Circuit High") \
  X(P0239, 0x0239U, "Turbocharger Boost Sensor B Malfunction") \
  X(P0240, 0x0240U, "Turbocharger Boost Sensor B Circuit Range/Performance") \
  X(P0241, 0x0241U, "Turbocharger Boost Sensor B Circuit Low") \
  X(P0242, 0x0242U, "Turbocharger Boost Sensor B Circuit High") \
  X(P0243, 0x0243U, "Turbocharger Wastegate Solenoid A Malfunction") \
  X(P0244, 0x0244U, "Turbocharger Wastegate Solenoid A Range/Performance") \
  X(P0245, 0x0245U, "Turbocharger Wastegate Solenoid A Low") \
  X(P0246, 0x0246U, "Turbocharger Wastegate Solenoid A High") \
  X(P0247, 0x0247U, "Turbocharger Wastegate Solenoid B Malfunction") \
  X(P0248, 0x0248U, "Turbocharger Wastegate Solenoid B Range/Performance") \
  X(P0249, 0x0249U, "Turbocharger Wastegate Solenoid B Low") \
  X(P0250, 0x0250U, "Turbocharger Wastegate Solenoid B High") \
  X(P0251, 0x0251U, "Injection Pump Fuel Metering Control \"A\" Malfunction (Cam/Rotor/Injector)") \
  X(P0252, 0x0252U, "Injection Pump Fuel Metering Control \"A\" Range/Performance (Cam/Rotor/Injector)") \
  X(P0253, 0x0253U, "Injection Pump Fuel Metering Control \"A\" Low (Cam/Rotor/Injector)") \
  X(P0254, 0x0254U, "Injection Pump Fuel Metering Control \"A\" High (Cam/Rotor/Injector)") \
  X(P0255, 0x0255U, "Injection Pump Fuel Metering Control \"A\" Intermittent (Cam/Rotor/Injector)") \
  X(P0256, 0x0256U, "Injection Pump Fuel Metering Control \"B\" Malfunction (Cam/Rotor/Injector)") \
  X(P0257, 0x0257U, "Injection Pump Fuel Metering Control \"B\" Range/Performance Injector)") \
  X(P0258, 0x0258U, "Injection Pump Fuel Metering Control \"B\" Low (Cam/R") \
  X(P0259, 0x0259U, "Injection Pump Fuel Metering Control \"B\" High (Cam/R") \
  X(P0260, 0x0260U, "Injection Pump Fuel Metering Control \"B\" Intermittent Injector)") \
  X(P0261, 0x0261U, "Cylinder I Injector Circuit Low") \
  X(P0262, 0x0262U, "Cylinder I Injector Circuit High") \
  X(P0263, 0x0263U, "Cylinder I Contribution/Balance Fault") \
  X(P0264, 0x0264U, "Cylinder 2 Injector Circuit Low") \
  X(P0265, 0x0265U, "Cylinder 2 Injector Circuit High") \
  X(P0266, 0x0266U, "Cylinder 2 Contribution/Balance Fault") \
  X(P0267, 0x0267U, "Cylinder 3 Injector Circuit Low") \
  X(P0268, 0x0268U, "Cylinder 3 Injector Circuit High") \
  X(P0269, 0x0269U, "Cylinder 3 Contribution/Balance Fault") \
  X(P0270, 0x0270U, "Cylinder 4 Injector Circuit Low") \
  X(P0271, 0x0271U, "Cylinder 4 Injector Circuit High") \
  X(P0272, 0x0272U, "Cylinder 4 Contribution/Balance Fault") \
  X(P0273, 0x0273U, "Cylinder 5 Injector Circuit Low") \
  X(P0274, 0x0274U, "Cylinder 5 Injector Circuit High") \
  X(P0275, 0x0275U, "Cylinder S Contribution/Balance Fault") \
  X(P0276, 0x0276U, "Cylinder 6 Injector Circuit Low") \
  X(P0277, 0x0277U, "Cylinder 6 Injector Circuit High") \
  X(P0278, 0x0278U, "Cylinder 6 Contribution/Balance Fault") \
  X(P0279, 0x0279U, "Cylinder 7 Injector Circuit Low") \
  X(P0280, 0x0280U, "Cylinder 7 Injector Circuit High") \
  X(P0281, 0x0281U, "Cylinder 7 Contribution/Balance Fault") \
  X(P0282, 0x0282U, "Cylinder 8 Injector Circuit Low") \
  X(P0283, 0x0283U, "Cylinder 8 Injector Circuit High") \
  X(P0284, 0x0284U, "Cylinder 8 Contribution/Balance Fault") \
  X(P0285, 0x0285U, "Cylinder 9 Injector Circuit Low") \
  X(P0286, 0x0286U, "Cylinder 9 Injector Circuit High") \
  X(P0287, 0x0287U, "Cylinder 9 Contribution/Balance Fault") \
  X(P0288, 0x0288U, "Cylinder 10 Injector Circuit Low") \
  X(P0289, 0x0289U, "Cylinder 10 Injector Circuit High") \
  X(P0290, 0x0290U, "Cylinder 10 Contribution/Balance Fault") \
  X(P0291, 0x0291U, "Cylinder 11 Injector Circuit Low") \
  X(P0292, 0x0292U, "Cylinder 11 Injector Circuit High") \
  X(P0293, 0x0293U, "Cylinder 11 Contribution/Balance Fault") \
  X(P0294, 0x0294U, "Cylinder 12 Injector Circuit Low") \
  X(P0295, 0x0295U, "Cylinder 12 Injector Circuit High") \
  X(P0296, 0x0296U, "Cylinder 12 Contribution/Range Fault") \
  X(P0297, 0x0297U, "Vehicle Overspeed Condition") \
  X(P0298, 0x0298U, "Engine Oil Over Temperature Condition") \
  X(P0299, 0x0299U, "Turbocharger/Supercharger \"A\" Underboost Condition") \
  X(P0300, 0x0300U, "Random/Multiple Cylinder Misfire Detected") \
  X(P0301, 0x0301U, "Cylinder 1 Misfire Detected") \
  X(P0302, 0x0302U, "Cylinder 2 Misfire Detected") \
  X(P0303, 0x0303U, "Cylinder 3 Misfire Detected") \
  X(P0304, 0x0304U, "Cylinder 4 Misfire Detected") \
  X(P0305, 0x0305U, "Cylinder 5 Misfire Detected") \
  X(P0306, 0x0306U, "Cylinder 6 Misfire Detected") \
  X(P0307, 0x0307U, "Cylinder 7 Misfire Detected") \
  X(P0308, 0x0308U, "Cylinder 8 Misfire Detected") \
  X(P0309, 0x0309U, "Cylinder 9 Misfire Detected") \
  X(P0310, 0x0310U, "Cylinder 10 Misfire Detected") \
  X(P0311, 0x0311U, "Cylinder 11 Misfire Detected") \
  X(P0312, 0x0312U, "Cylinder 12 Misfire Detected") \
  X(P0313, 0x0313U, "Misfire Detected with Low Fuel") \
  X(P0314, 0x0314U, "Single Cylinder Misfire (Cylinder not Specified)") \
  X(P0315, 0x0315U, "Crankshaft Position System Variation Not Learned") \
  X(P0316, 0x0316U, "Misfire Detected On Startup (First 1000 Revolutions)") \
  X(P0317, 0x0317U, "Rough Road Hardware Not Present") \
  X(P0318, 0x0318U, "Rough Road Sensor A Signal Circuit") \
  X(P0319, 0x0319U, "Rough Road Sensor B Signal Circuit") \
  X(P0320, 0x0320U, "Ignition/Distributor Engine Speed Input Circuit Malfunction") \
  X(P0321, 0x0321U, "Ignition/Distributor Engine Speed Input Circuit Range/Performance") \
  X(P0322, 0x0322U, "Ignition/Distributor Engine Speed Input Circuit No Signal") \
  X(P0323, 0x0323U, "Ignition/Distributor Engine Speed Input Circuit Intermittent") \
  X(P0324, 0x0324U, "Knock Control System Error") \
  X(P0325, 0x0325U, "Knock Sensor 1 Circuit Malfunction (Bank I or Single Sensor)") \
  X(P0326, 0x0326U, "Knock Sensor 1 Circuit Range/Performance (Bank 1 or Single Sensor)") \
  X(P0327, 0x0327U, "Knock Sensor 1 Circuit Low Input (Bank I or Single Sensor)") \
  X(P0328, 0x0328U, "Knock Sensor 1 Circuit High Input (Bank I or Single Sensor)") \
  X(P0329, 0x0329U, "Knock Sensor 1 Circuit Intermittent (Bank 1 or Single Sensor)") \
  X(P0330, 0x0330U, "Knock Sensor 2 Circuit Malfunction (Bank 2)") \
  X(P0331, 0x0331U, "Knock Sensor 2 Circuit Range/Performance (Bank 2)") \
  X(P0332, 0x0332U, "Knock Sensor 2 Circuit Low Input (Bank 2)") \
  X(P0333, 0x0333U, "Knock Sensor 2 Circuit High Input (Bank 2)") \
  X(P0334, 0x0334U, "Knock Sensor 2 Circuit Intermittent (Bank 2)") \
  X(P0335, 0x0335U, "Crankshaft Position Sensor A Circuit Malfunction") \
  X(P0336, 0x0336U, "Crankshaft Position Sensor A Circuit Range/Performance") \
  X(P0337, 0x0337U, "Crankshaft Position Sensor A Circuit Low Input") \
  X(P0338, 0x0338U, "Crankshaft Position Sensor A Circuit High Input") \
  X(P0339, 0x0339U, "Crankshaft Position Sensor A Circuit Intermittent") \
  X(P0340, 0x0340U, "Camshaft Position Sensor Circuit Malfunction") \
  X(P0341, 0x0341U, "Camshaft Position Sensor Circuit Range/Performance") \
  X(P0342, 0x0342U, "Camshaft Position Sensor Circuit Low Input") \
  X(P0343, 0x0343U, "Camshaft Position Sensor Circuit High Input") \
  X(P0344, 0x0344U, "Camshaft Position Sensor Circuit Intermittent") \
  X(P0345, 0x0345U, "Camshaft Position Sensor A Circuit Malfunction (Bank 2)") \
  X(P0346, 0x0346U, "Camshaft Position Sensor A Circuit Range/Performance (Bank 2)") \
  X(P0347, 0x0347U, "Camshaft Position Sensor A Circuit Low Input (Bank 2)") \
  X(P0348, 0x0348U, "Camshaft Position Sensor A Circuit High Input (Bank 2)") \
  X(P0349, 0x0349U, "Camshaft Position Sensor A Circuit Intermittent (Bank 2)") \
  X(P0350, 0x0350U, "Ignition Coil Primary/Secondary Circuit Malfunction") \
  X(P0351, 0x0351U, "Ignition Coil A Primary/Secondary Circuit Malfunction") \
  X(P0352, 0x0352U, "Ignition Coil B Primary/Secondary Circuit Malfunction") \
  X(P0353, 0x0353U, "Ignition Coil C Primary/Secondary Circuit Malfunction") \
  X(P0354, 0x0354U, "Ignition Coil D Primary/Secondary Circuit Malfunction") \
  X(P0355, 0x0355U, "Ignition Coil E Primary/Secondary Circuit Malfunction") \
  X(P0356, 0x0356U, "Ignition Coil F Primary/Secondary Circuit Malfunction") \
  X(P0357, 0x0357U, "Ignition Coil G Primary/Secondary Circuit Malfunction") \
  X(P0358, 0x0358U, "Ignition Coil H Primary/Secondary Circuit Malfunction") \
  X(P0359, 0x0359U, "Ignition Coil I Primary/Secondary Circuit Malfunction") \
  X(P0360, 0x0360U, "Ignition Coil J Primary/Secondary Circuit Malfunction") \
  X(P0361, 0x0361U, "Ignition Coil K Primary/Secondary Circuit Malfunction") \
  X(P0362, 0x0362U, "Ignition Coil L Primary/Secondary Circuit Malfunction") \
  X(P0363, 0x0363U, "Misfire Detected - Fueling Disabled") \
  X(P0364, 0x0364U, "Reserved") \
  X(P0365, 0x0365U, "Camshaft Position Sensor \"B\" Circuit (Bank 1)") \
  X(P0366, 0x0366U, "Camshaft Position Sensor \"B\" Circuit Range/Performance (Bank 1)") \
  X(P0367, 0x0367U, "Camshaft Position Sensor \"B\" Circuit Low (Bank 1)") \
  X(P0368, 0x0368U, "Camshaft Position Sensor \"B\" Circuit High (Bank 1)") \
  X(P0369, 0x0369U, "Camshaft Position Sensor \"B\" Circuit Intermittent (Bank 1)") \
  X(P0370, 0x0370U, "Timing Reference High Resolution Signal A Malfunction") \
  X(P0371, 0x0371U, "Timing Reference High Resolution Signal A Too Many Pulses") \
  X(P0372, 0x0372U, "Timing Reference High Resolution Signal A Too Few Pulses") \
  X(P0373, 0x0373U, "Timing Reference High Resolution Signal A Intermittent/Erratic Pulses") \
  X(P0374, 0x0374U, "Timing Reference High Resolution Signal A No Pulses") \
  X(P0375, 0x0375U, "Timing Reference High Resolution Signal B Malfunction") \
  X(P0376, 0x0376U, "Timing Reference High Resolution Signal B Too Many Pulses") \
  X(P0377, 0x0377U, "Timing Reference High Resolution Signal B Too Few Pulses") \
  X(P0378, 0x0378U, "Timing Reference High Resolution Signal B Intermittent/Erratic Pulses") \
  X(P0379, 0x0379U, "Timing Reference High Resolution Signal B No Pulses") \
  X(P0380, 0x0380U, "Glow Plug/Heater Circuit \"A\" Malfunction") \
  X(P0381, 0x0381U, "Glow Plug/Heater Indicator Circuit Malfunction") \
  X(P0382, 0x0382U, "Exhaust Gas Recirculation Flow Malfunction") \
  X(P0383, 0x0383U, "Glow Plug Control Module Control Circuit Low") \
  X(P0384, 0x0384U, "Glow Plug Control Module Control Circuit High") \
  X(P0385, 0x0385U, "Crankshaft Position Sensor B Circuit Malfunction") \
  X(P0386, 0x0386U, "Crankshaft Position Sensor B Circuit Range/Performance") \
  X(P0387, 0x0387U, "Crankshaft Position Sensor B Circuit Low Input") \
  X(P0388, 0x0388U, "Crankshaft Position Sensor B Circuit High Input") \
  X(P0389, 0x0389U, "Crankshaft Position Sensor B Circuit Intermittent") \
  X(P0390, 0x0390U, "Camshaft Position Sensor \"B\" Circuit (Bank 2)") \
  X(P0391, 0x0391U, "Camshaft Position Sensor \"B\" Circuit Range/Performance (Bank 2)") \
  X(P0392, 0x0392U, "Camshaft Position Sensor \"B\" Circuit Low (Bank 2)") \
  X(P0393, 0x0393U, "Camshaft Position Sensor \"B\" Circuit High (Bank 2)") \
  X(P0394, 0x0394U, "Camshaft Position Sensor \"B\" Circuit Intermittent (Bank 2)") \
  X(P0400, 0x0400U, "Exhaust Gas Recirculation Flow Malfunction") \
  X(P0401, 0x0401U, "Exhaust Gas Recirculation Flow Insufficient Detected") \
  X(P0402, 0x0402U, "Exhaust Gas Recirculation Flow Excessive Detected") \
  X(P0403, 0x0403U, "Exhaust Gas Recirculation Circuit Malfunction") \
  X(P0404, 0x0404U, "Exhaust Gas Recirculation Circuit Range/Performance") \
  X(P0405, 0x0405U, "Exhaust Gas Recirculation Sensor A Circuit Low") \
  X(P0406, 0x0406U, "Exhaust Gas Recirculation Sensor A Circuit High") \
  X(P0407, 0x0407U, "Exhaust Gas Recirculation Sensor B Circuit Low") \
  X(P0408, 0x0408U, "Exhaust Gas Recirculation Sensor B Circuit High") \
  X(P0409, 0x0409U, "Exhaust Gas Recirculation Sensor \"A\" Circuit") \
  X(P0410, 0x0410U, "Secondary Air Injection System Malfunction") \
  X(P0411, 0x0411U, "Secondary Air Injection System Incorrect Flow Detected") \
  X(P0412, 0x0412U, "Secondary Air Injection System Switching Valve A Circuit Malfunction") \
  X(P0413, 0x0413U, "Secondary Air Injection System Switching Valve A Circuit Open") \
  X(P0414, 0x0414U, "Secondary Air Injection System Switching Valve A Circuit Shorted") \
  X(P0415, 0x0415U, "Secondary Air Injection System Switching Valve B Circuit Malfunction") \
  X(P0416, 0x0416U, "Secondary Air Injection System Switching Valve B Circuit Open") \
  X(P0417, 0x0417U, "Secondary Air Injection System Switching Valve B Circuit Shorted") \
  X(P0418, 0x0418U, "Secondary Air Injection System Relay \"A\" Circuit Malfunction") \
  X(P0419, 0x0419U, "Secondary Air Injection System Relay \"B\" Circuit Malfunction") \
  X(P0420, 0x0420U, "Catalyst System Efficiency Below Threshold (Bank 1)") \
  X(P0421, 0x0421U, "Warm Up Catalyst Efficiency Below Threshold (Bank 1)") \
  X(P0422, 0x0422U, "Main Catalyst Efficiency Below Threshold (Bank 1)") \
  X(P0423, 0x0423U, "Heated Catalyst Efficiency Below Threshold (Bank 1)") \
  X(P0424, 0x0424U, "Heated Catalyst Temperature Below Threshold (Bank 1)") \
  X(P0425, 0x0425U, "Catalyst Temperature Sensor (Bank 1)") \
  X(P0426, 0x0426U, "Catalyst Temperature Sensor Range/Performance (Bank 1)") \
  X(P0427, 0x0427U, "Catalyst Temperature Sensor Low (Bank 1)") \
  X(P0428, 0x0428U, "Catalyst Temperature Sensor High (Bank 1)") \
  X(P0429, 0x0429U, "Catalyst Heater Control Circuit") \
  X(P0430, 0x0430U, "Catalyst System Efficiency Below Threshold (Bank 2)") \
  X(P0431, 0x0431U, "Warm Up Catalyst Efficiency Below Threshold (Bank 2)") \
  X(P0432, 0x0432U, "Main Catalyst Efficiency Below Threshold (Bank 2)") \
  X(P0433, 0x0433U, "Heated Catalyst Efficiency Below Threshold (Bank 2)") \
  X(P0434, 0x0434U, "Heated Catalyst Temperature Below Threshold (Bank 2)") \
  X(P0435, 0x0435U, "Catalyst Temperature Sensor (Bank 2)") \
  X(P0436, 0x0436U, "Catalyst Temperature Sensor Range/Performance (Bank 2)") \
  X(P0437, 0x0437U, "Catalyst Temperature Sensor Low (Bank 2)") \
  X(P0438, 0x0438U, "Catalyst Temperature Sensor High (Bank 2)") \
  X(P0439, 0x0439U, "Catalyst Heater Control Circuit") \
  X(P0440, 0x0440U, "Evaporative Emission Control System Malfunction") \
  X(P0441, 0x0441U, "Evaporative Emission Control System Incorrect Purge Flow") \
  X(P0442, 0x0442U, "Evaporative Emission Control System Leak Detected (small leak)") \
  X(P0443, 0x0443U, "Evaporative Emission Control System Purge Control Valve Circuit") \
  X(P0444, 0x0444U, "Evaporative Emission Control System Purge Control Valve Circuit Open") \
  X(P0445, 0x0445U, "Evaporative Emission Control System Purge Control Valve Circuit Shorted") \
  X(P0446, 0x0446U, "Evaporative Emission Control System Vent Control Circuit Malfunction") \
  X(P0447, 0x0447U, "Evaporative Emission Control System Vent Control Circuit Open") \
  X(P0448, 0x0448U, "Evaporative Emission Control System Vent Control Circuit Shorted") \
  X(P0449, 0x0449U, "Evaporative Emission Control System Vent Valve/Solenoid Circuit Malfunction") \
  X(P0450, 0x0450U, "Evaporative Emission Control System Pressure Sensor Malfunction") \
  X(P0451, 0x0451U, "Evaporative Emission Control System Pressure Sensor Range/Performance") \
  X(P0452, 0x0452U, "Evaporative Emission Control System Pressure Sensor Low Input") \
  X(P0453, 0x0453U, "Evaporative Emission Control System Pressure Sensor High Input") \
  X(P0454, 0x0454U, "Evaporative Emission Control System Pressure Sensor Intermittent") \
  X(P0455, 0x0455U, "Evaporative Emission Control System Leak Detected (gross leak)") \
  X(P0456, 0x0456U, "Evaporative Emissions System Small Leak Detected") \
  X(P0457, 0x0457U, "Evaporative Emission Control System Leak Detected") \
  X(P0458, 0x0458U, "Evaporative Emission System Purge Control Valve Circuit Low") \
  X(P0459, 0x0459U, "Evaporative Emission System Purge Control Valve Circuit High") \
  X(P0460, 0x0460U, "Fuel Level Sensor Circuit Malfunction") \
  X(P0461, 0x0461U, "Fuel Level Sensor Circuit Range/Performance") \
  X(P0462, 0x0462U, "Fuel Level Sensor Circuit Low Input") \
  X(P0463, 0x0463U, "Fuel Level Sensor Circuit High Input") \
  X(P0464, 0x0464U, "Fuel Level Sensor Circuit Intermittent") \
  X(P0465, 0x0465U, "Purge Flow Sensor Circuit Malfunction") \
  X(P0466, 0x0466U, "Purge Flow Sensor Circuit Range/Performance") \
  X(P0467, 0x0467U, "Purge Flow Sensor Circuit Low Input") \
  X(P0468, 0x0468U, "Purge Flow Sensor Circuit High Input") \
  X(P0469, 0x0469U, "Purge Flow Sensor Circuit Intermittent") \
  X(P0470, 0x0470U, "Exhaust Pressure Sensor Malfunction") \
  X(P0471, 0x0471U, "Exhaust Pressure Sensor Range/Performance") \
  X(P0472, 0x0472U, "Exhaust Pressure Sensor Low") \
  X(P0473, 0x0473U, "Exhaust Pressure Sensor High") \
  X(P0474, 0x0474U, "Exhaust Pressure Sensor Intermittent") \
  X(P0475, 0x0475U, "Exhaust Pressure Control Valve Malfunction") \
  X(P0476, 0x0476U, "Exhaust Pressure Control Valve Range/Performance") \
  X(P0477, 0x0477U, "Exhaust Pressure Control Valve Low") \
  X(P0478, 0x0478U, "Exhaust Pressure Control Valve High") \
  X(P0479, 0x0479U, "Exhaust Pressure Control Valve Intermittent") \
  X(P0480, 0x0480U, "Cooling Fan I Control Circuit Malfunction") \
  X(P0481, 0x0481U, "Cooling Fan 2 Control Circuit Malfunction") \
  X(P0482, 0x0482U, "Cooling Fan 3 Control Circuit Malfunction") \
  X(P0483, 0x0483U, "Cooling Fan Rationality Check Malfunction") \
  X(P0484, 0x0484U, "Cooling Fan Circuit Over Current") \
  X(P0485, 0x0485U, "Cooling Fan Power/Ground Circuit Malfunction") \
  X(P0486, 0x0486U, "Exhaust Gas Recirculation Sensor \"B\" Circuit") \
  X(P0487, 0x0487U, "Exhaust Gas Recirculation Throttle Control Circuit \"A\" Open") \
  X(P0488, 0x0488U, "Exhaust Gas Recirculation Throttle Control Circuit \"A\" Range/Performance") \
  X(P0489, 0x0489U, "Exhaust Gas Recirculation \"A\" Control Circuit Low") \
  X(P0490, 0x0490U, "Exhaust Gas Recirculation \"A\" Control Circuit High") \
  X(P0491, 0x0491U, "Secondary Air Injection System Insufficient Flow Bank 1") \
  X(P0492, 0x0492U, "Secondary Air Injection System Insufficient Flow Bank 2") \
  X(P0493, 0x0493U, "Fan Overspeed") \
  X(P0494, 0x0494U, "Fan Speed Low") \
  X(P0495, 0x0495U, "Fan Speed High") \
  X(P0496, 0x0496U, "EVAP Flow During A Non-Purge Condition") \
  X(P0497, 0x0497U, "Evaporative Emission System Low Purge Flow") \
  X(P0498, 0x0498U, "Evaporative Emission System Vent Valve Control Circuit Low") \
  X(P0499, 0x0499U, "Evaporative Emission System Vent Valve Control Circuit High") \
  X(P0500, 0x0500U, "Vehicle Speed Sensor Malfunction") \
  X(P0501, 0x0501U, "Vehicle Speed Sensor Range/Performance") \
  X(P0502, 0x0502U, "Vehicle Speed Sensor Low Input") \
  X(P0503, 0x0503U, "Vehicle Speed Sensor Intermittent/Erratic/High") \
  X(P0504, 0x0504U, "Brake Switch \"A\"/\"B\" Correlation") \
  X(P0505, 0x0505U, "Idle Control System Malfunction") \
  X(P0506, 0x0506U, "Idle Control System RPM Lower Than Expected") \
  X(P0507, 0x0507U, "Idle Control System RPM Higher Than Expected") \
  X(P0508, 0x0508U, "Idle Air Control System Circuit Low") \
  X(P0509, 0x0509U, "Idle Air Control System Circuit High") \
  X(P0510, 0x0510U, "Closed Throttle Position Switch Malfunction") \
  X(P0511, 0x0511U, "Idle Air Control Circuit") \
  X(P0512, 0x0512U, "Starter Request Circuit") \
  X(P0513, 0x0513U, "Incorrect Immobilizer Key") \
  X(P0514, 0x0514U, "Battery Temperature Sensor Circuit Range/Performance") \
  X(P0515, 0x0515U, "Battery Temperature Sensor Circuit") \
  X(P0516, 0x0516U, "Battery Temperature Sensor Circuit Low") \
  X(P0517, 0x0517U, "Battery Temperature Sensor Circuit High") \
  X(P0518, 0x0518U, "Idle Air Control Circuit Intermittent") \
  X(P0519, 0x0519U, "Idle Air Control System Performance") \
  X(P0520, 0x0520U, "Engine Oil Pressure Sensor/Switch Circuit Malfunction") \
  X(P0521, 0x0521U, "Engine Oil Pressure Sensor/Switch Circuit Range/Performance") \
  X(P0522, 0x0522U, "Engine Oil Pressure Sensor/Switch Circuit Low Voltage") \
  X(P0523, 0x0523U, "Engine Oil Pressure Sensor/Switch Circuit High Voltage") \
  X(P0524, 0x0524U, "Engine Oil Pressure Too Low") \
  X(P0525, 0x0525U, "Cruise Control Servo Control Circuit Range/Performance") \
  X(P0526, 0x0526U, "Fan Speed Sensor Circuit") \
  X(P0527, 0x0527U, "Fan Speed Sensor Circuit Range/Performance") \
  X(P0528, 0x0528U, "Fan Speed Sensor Circuit No Signal") \
  X(P0529, 0x0529U, "Fan Speed Sensor Circuit Intermittent") \
  X(P0530, 0x0530U, "A/C Refrigerant Pressure Sensor Circuit Malfunction") \
  X(P0531, 0x0531U, "A/C Refrigerant Pressure Sensor Circuit Range/Performance") \
  X(P0532, 0x0532U, "A/C Refrigerant Pressure Sensor Circuit Low Input") \
  X(P0533, 0x0533U, "A/C Refrigerant Pressure Sensor Circuit High Input") \
  X(P0534, 0x0534U, "Air Conditioner Refrigerant Charge Loss") \
  X(P0535, 0x0535U, "A/C Evaporator Temperature Sensor Circuit") \
  X(P0536, 0x0536U, "A/C Evaporator Temperature Sensor Circuit Range/Performance") \
  X(P0537, 0x0537U, "A/C Evaporator Temperature Sensor Circuit Low") \
  X(P0538, 0x0538U, "A/C Evaporator Temperature Sensor Circuit High") \
  X(P0539, 0x0539U, "A/C Evaporator Temperature Sensor Circuit Intermittent") \
  X(P0540, 0x0540U, "Intake Air Heater \"A\" Circuit") \
  X(P0541, 0x0541U, "Intake Air Heater \"A\" Circuit Low") \
  X(P0542, 0x0542U, "Intake Air Heater \"A\" Circuit High") \
  X(P0543, 0x0543U, "Intake Air Heater \"A\" Circuit Open") \
  X(P0544, 0x0544U, "Exhaust Gas Temperature Sensor Circuit") \
  X(P0545, 0x0545U, "Exhaust Gas Temperature Sensor Circuit Low") \
  X(P0546, 0x0546U, "Exhaust Gas Temperature Sensor Circuit High") \
  X(P0547, 0x0547U, "Exhaust Gas Temperature Sensor Circuit") \
  X(P0548, 0x0548U, "Exhaust Gas Temperature Sensor Circuit Low") \
  X(P0549, 0x0549U, "Exhaust Gas Temperature Sensor Circuit High") \
  X(P0550, 0x0550U, "Power Steering Pressure Sensor Circuit Malfunction") \
  X(P0551, 0x0551U, "Power Steering Pressure Sensor Circuit Range/Performance") \
  X(P0552, 0x0552U, "Power Steering Pressure Sensor Circuit Low Input") \
  X(P0553, 0x0553U, "Power Steering Pressure Sensor Circuit High Input") \
  X(P0554, 0x0554U, "Power Steering Pressure Sensor Circuit Intermittent") \
  X(P0555, 0x0555U, "Brake Booster Pressure Sensor Circuit") \
  X(P0556, 0x0556U, "Brake Booster Pressure Sensor Circuit Range/Performance") \
  X(P0557, 0x0557U, "Brake Booster Pressure Sensor Circuit Low") \
  X(P0558, 0x0558U, "Brake Booster Pressure Sensor Circuit High") \
  X(P0559, 0x0559U, "Brake Booster Pressure Sensor Circuit Intermittent") \
  X(P0560, 0x0560U, "System Voltage Malfunction") \
  X(P0561, 0x0561U, "System Voltage Unstable") \
  X(P0562, 0x0562U, "System Voltage Low") \
  X(P0563, 0x0563U, "System Voltage High") \
  X(P0564, 0x0564U, "Cruise Control Multi-Function Input \"A\" Circuit") \
  X(P0565, 0x0565U, "Cruise Control On Signal Malfunction") \
  X(P0566, 0x0566U, "Cruise Control Off Signal Malfunction") \
  X(P0567, 0x0567U, "Cruise Control Resume Signal Malfunction") \
  X(P0568, 0x0568U, "Cruise Control Set Signal Malfunction") \
  X(P0569, 0x0569U, "Cruise Control Coast Signal Malfunction") \
  X(P0570, 0x0570U, "Cruise Control Accel Signal Malfunction") \
  X(P0571, 0x0571U, "Cruise Control/Brake Switch A Circuit Malfunction") \
  X(P0572, 0x0572U, "Cruise Control/Brake Switch A Circuit Low") \
  X(P0573, 0x0573U, "Cruise Control/Brake Switch A Circuit High") \
  X(P0574, 0x0574U, "Cruise Control System - Vehicle Speed Too High") \
  X(P0575, 0x0575U, "Cruise Control Input Circuit") \
  X(P0576, 0x0576U, "Cruise Control Input Circuit Low") \
  X(P0577, 0x0577U, "Cruise Control Input Circuit High") \
  X(P0578, 0x0578U, "Cruise Control Multi-Function Input \"A\" Circuit Stuck") \
  X(P0579, 0x0579U, "Cruise Control Multi-Function Input \"A\" Circuit Range/Performance") \
  X(P0580, 0x0580U, "Cruise Control Multi-Function Input \"A\" Circuit Low") \
  X(P0581, 0x0581U, "Cruise Control Multi-Function Input \"A\" Circuit High") \
  X(P0582, 0x0582U, "Cruise Control Vacuum Control Circuit/Open") \
  X(P0583, 0x0583U, "Cruise Control Vacuum Control Circuit Low") \
  X(P0584, 0x0584U, "Cruise Control Vacuum Control Circuit High") \
  X(P0585, 0x0585U, "Cruise Control Multi-Function Input \"A\"/\"B\" Correlation") \
  X(P0586, 0x0586U, "Cruise Control Vent Control Circuit/Open") \
  X(P0587, 0x0587U, "Cruise Control Vent Control Circuit Low") \
  X(P0588, 0x0588U, "Cruise Control Vent Control Circuit High") \
  X(P0589, 0x0589U, "Cruise Control Multi-Function Input \"B\" Circuit") \
  X(P0590, 0x0590U, "Cruise Control Multi-Function Input \"B\" Circuit Stuck") \
  X(P0591, 0x0591U, "Cruise Control Multi-Function Input \"B\" Circuit Range/Performance") \
  X(P0592, 0x0592U, "Cruise Control Multi-Function Input \"B\" Circuit Low") \
  X(P0593, 0x0593U, "Cruise Control Multi-Function Input \"B\" Circuit High") \
  X(P0594, 0x0594U, "Cruise Control Servo Control Circuit/Open") \
  X(P0595, 0x0595U, "Cruise Control Servo Control Circuit Low") \
  X(P0596, 0x0596U, "Cruise Control Servo Control Circuit High") \
  X(P0597, 0x0597U, "Thermostat Heater Control Circuit/Open") \
  X(P0598, 0x0598U, "Thermostat Heater Control Circuit Low") \
  X(P0599, 0x0599U, "Thermostat Heater Control Circuit High") \
  X(P0600, 0x0600U, "Serial Communication Link Malfunction") \
  X(P0601, 0x0601U, "Internal Control Module Memory Check Sum Error") \
  X(P0602, 0x0602U, "Control Module Programming Error") \
  X(P0603, 0x0603U, "Internal Control Module Keep Alive Memory (KAM) Error") \
  X(P0604, 0x0604U, "Internal Control Module Random Access Memory (RAM) Error") \
  X(P0605, 0x0605U, "Internal Control Module Read Only Memory (ROM) Error") \
  X(P0606, 0x0606U, "ECM/PCM Processor Fault") \
  X(P0607, 0x0607U, "Control Module Performance") \
  X(P0608, 0x0608U, "Control Module VSS Output \"A\" Malfunction") \
  X(P0609, 0x0609U, "Control Module VSS Output \"B\" Malfunction") \
  X(P0610, 0x0610U, "Control Module Vehicle Options Error") \
  X(P0611, 0x0611U, "Fuel Injector Control Module Performance") \
  X(P0612, 0x0612U, "Fuel Injector Control Module Relay Control") \
  X(P0613, 0x0613U, "TCM Processor") \
  X(P0614, 0x0614U, "ECM / TCM Incompatible") \
  X(P0615, 0x0615U, "Starter Relay Circuit") \
  X(P0616, 0x0616U, "Starter Relay Circuit Low") \
  X(P0617, 0x0617U, "Starter Relay Circuit High") \
  X(P0618, 0x0618U, "Alternative Fuel Control Module KAM Error") \
  X(P0619, 0x0619U, "Alternative Fuel Control Module RAM/ROM Error") \
  X(P0620, 0x0620U, "Generator Control Circuit Malfunction") \
  X(P0621, 0x0621U, "Generator Lamp \"L\" Control Circuit Malfunction") \
  X(P0622, 0x0622U, "Generator Field \"F\" Control Circuit Malfunction") \
  X(P0623, 0x0623U, "Generator Lamp Control Circuit") \
  X(P0624, 0x0624U, "Fuel Cap Lamp Control Circuit") \
  X(P0625, 0x0625U, "Generator Field/F Terminal Circuit Low") \
  X(P0626, 0x0626U, "Generator Field/F Terminal Circuit High") \
  X(P0627, 0x0627U, "Fuel Pump \"A\" Control Circuit /Open") \
  X(P0628, 0x0628U, "Fuel Pump \"A\" Control Circuit Low") \
  X(P0629, 0x0629U, "Fuel Pump \"A\" Control Circuit High") \
  X(P062F, 0x062FU, "Internal Control Module EEPROM Error") \
  X(P0630, 0x0630U, "VIN Not Programmed or Incompatible - ECM/PCM") \
  X(P0631, 0x0631U, "VIN Not Programmed or Incompatible - TCM") \
  X(P0632, 0x0632U, "Odometer Not Programmed - ECM/PCM") \
  X(P0633, 0x0633U, "Immobilizer Key Not Programmed - ECM/PCM") \
  X(P0634, 0x0634U, "PCM/ECM/TCM Internal Temperature Too High") \
  X(P0635, 0x0635U, "Power Steering Control Circuit") \
  X(P0636, 0x0636U, "Power Steering Control Circuit Low") \
  X(P0637, 0x0637U, "Power Steering Control Circuit High") \
  X(P0638, 0x0638U, "Throttle Actuator Control Range/Performance (Bank 1)") \
  X(P0639, 0x0639U, "Throttle Actuator Control Range/Performance (Bank 2)") \
  X(P0640, 0x0640U, "Intake Air Heater Control Circuit") \
  X(P0641, 0x0641U, "Sensor Reference Voltage \"A\" Circuit/Open") \
  X(P0642, 0x0642U, "Sensor Reference Voltage \"A\" Circuit Low") \
  X(P0643, 0x0643U, "Sensor Reference Voltage \"A\" Circuit High") \
  X(P0644, 0x0644U, "Driver Display Serial Communication Circuit") \
  X(P0645, 0x0645U, "A/C Clutch Relay Control Circuit") \
  X(P0646, 0x0646U, "A/C Clutch Relay Control Circuit Low") \
  X(P0647, 0x0647U, "A/C Clutch Relay Control Circuit High") \
  X(P0648, 0x0648U, "Immobilizer Lamp Control Circuit") \
  X(P0649, 0x0649U, "Speed Control Lamp Control Circuit") \
  X(P0650, 0x0650U, "Malfunction Indicator Lamp (MIL) Control Circuit Malfunction") \
  X(P0651, 0x0651U, "Sensor Reference Voltage \"B\" Circuit/Open") \
  X(P0652, 0x0652U, "Sensor Reference Voltage \"B\" Circuit Low") \
  X(P0653, 0x0653U, "Sensor Reference Voltage \"B\" Circuit High") \
  X(P0654, 0x0654U, "Engine RPM Output Circuit Malfunction") \
  X(P0655, 0x0655U, "Engine Hot Lamp Output Control Circuit Malfunction") \
  X(P0656, 0x0656U, "Fuel Level Output Circuit Malfunction") \
  X(P0657, 0x0657U, "Actuator Supply Voltage \"A\" Circuit/Open") \
  X(P0658, 0x0658U, "Actuator Supply Voltage \"A\" Circuit Low") \
  X(P0659, 0x0659U, "Actuator Supply Voltage \"A\" Circuit High") \
  X(P0660, 0x0660U, "Intake Manifold Tuning Valve Control Circuit/Open Bank 1") \
  X(P0661, 0x0661U, "Intake Manifold Tuning Valve Control Circuit Low Bank 1") \
  X(P0662, 0x0662U, "Intake Manifold Tuning Valve Control Circuit High Bank 1") \
  X(P0663, 0x0663U, "Intake Manifold Tuning Valve Control Circuit/Open Bank 2") \
  X(P0664, 0x0664U, "Intake Manifold Tuning Valve Control Circuit Low Bank 2") \
  X(P0665, 0x0665U, "Intake Manifold Tuning Valve Control Circuit High Bank 2") \
  X(P0666, 0x0666U, "PCM/ECM/TCM Internal Temperature Sensor Circuit") \
  X(P0667, 0x0667U, "PCM/ECM/TCM Internal Temperature Sensor Range/Performance") \
  X(P0668, 0x0668U, "PCM/ECM/TCM Internal Temperature Sensor Circuit Low") \
  X(P0669, 0x0669U, "PCM/ECM/TCM Internal Temperature Sensor Circuit High") \
  X(P0670, 0x0670U, "Glow Plug Module Control Circuit") \
  X(P0671, 0x0671U, "Cylinder 1 Glow Plug Circuit") \
  X(P0672, 0x0672U, "Cylinder 2 Glow Plug Circuit") \
  X(P0673, 0x0673U, "Cylinder 3 Glow Plug Circuit") \
  X(P0674, 0x0674U, "Cylinder 4 Glow Plug Circuit") \
  X(P0675, 0x0675U, "Cylinder 5 Glow Plug Circuit") \
  X(P0676, 0x0676U, "Cylinder 6 Glow Plug Circuit") \
  X(P0677, 0x0677U, "Cylinder 7 Glow Plug Circuit") \
  X(P0678, 0x0678U, "Cylinder 8 Glow Plug Circuit") \
  X(P0679, 0x0679U, "Cylinder 9 Glow Plug Circuit") \
  X(P0680, 0x0680U, "Cylinder 10 Glow Plug Circuit") \
  X(P0681, 0x0681U, "Cylinder 11 Glow Plug Circuit") \
  X(P0682, 0x0682U, "Cylinder 12 Glow Plug Circuit") \
  X(P0683, 0x0683U, "Glow Plug Control Module to PCM Communication Circuit") \
  X(P0684, 0x0684U, "Glow Plug Control Module to PCM Communication Circuit Range/Performance") \
  X(P0685, 0x0685U, "ECM/PCM Power Relay Control Circuit /Open") \
  X(P0686, 0x0686U, "ECM/PCM Power Relay Control Circuit Low") \
  X(P0687, 0x0687U, "ECM/PCM Power Relay Control Circuit High") \
  X(P0688, 0x0688U, "ECM/PCM Power Relay Sense Circuit /Open") \
  X(P0689, 0x0689U, "ECM/PCM Power Relay Sense Circuit Low") \
  X(P0690, 0x0690U, "ECM/PCM Power Relay Sense Circuit High") \
  X(P0691, 0x0691U, "Fan 1 Control Circuit Low P0692 Fan 1 Control Circuit High") \
  X(P0693, 0x0693U, "Fan 2 Control Circuit Low") \
  X(P0694, 0x0694U, "Fan 2 Control Circuit High") \
  X(P0695, 0x0695U, "Fan 3 Control Circuit Low") \
  X(P0696, 0x0696U, "Fan 3 Control Circuit High") \
  X(P0697, 0x0697U, "Sensor Reference Voltage \"C\" Circuit/Open") \
  X(P0698, 0x0698U, "Sensor Reference Voltage \"C\" Circuit Low") \
  X(P0699, 0x0699U, "Sensor Reference Voltage \"C\" Circuit High") \
  X(P0700, 0x0700U, "Transmission Control System Malfunction") \
  X(P0701, 0x0701U, "Transmission Control System Range/Performance") \
  X(P0702, 0x0702U, "Transmission Control System Electrical") \
  X(P0703, 0x0703U, "Torque Converter/Brake Switch B Circuit Malfunction") \
  X(P0704, 0x0704U, "Clutch Switch Input Circuit Malfunction") \
  X(P0705, 0x0705U, "Transmission Range Sensor Circuit malfunction (PRNDL Input)") \
  X(P0706, 0x0706U, "Transmission Range Sensor Circuit Range/Performance") \
  X(P0707, 0x0707U, "Transmission Range Sensor Circuit Low Input") \
  X(P0708, 0x0708U, "Transmission Range Sensor Circuit High Input") \
  X(P0709, 0x0709U, "Transmission Range Sensor Circuit Intermittent") \
  X(P0710, 0x0710U, "Transmission Fluid Temperature Sensor Circuit Malfunction") \
  X(P0711, 0x0711U, "Transmission Fluid Temperature Sensor Circuit Range/Performance") \
  X(P0712, 0x0712U, "Transmission Fluid Temperature Sensor Circuit Low Input") \
  X(P0713, 0x0713U, "Transmission Fluid Temperature Sensor Circuit High Input") \
  X(P0714, 0x0714U, "Transmission Fluid Temperature Sensor Circuit Intermittent") \
  X(P0715, 0x0715U, "Input/Turbine Speed Sensor Circuit Malfunction") \
  X(P0716, 0x0716U, "Input/Turbine Speed Sensor Circuit Range/Performance") \
  X(P0717, 0x0717U, "Input/Turbine Speed Sensor Circuit No Signal") \
  X(P0718, 0x0718U, "Input/Turbine Speed Sensor Circuit Intermittent") \
  X(P0719, 0x0719U, "Torque Converter/Brake Switch B Circuit Low") \
  X(P0720, 0x0720U, "Output Speed Sensor Circuit Malfunction") \
  X(P0721, 0x0721U, "Output Speed Sensor Range/Performance") \
  X(P0722, 0x0722U, "Output Speed Sensor No Signal") \
  X(P0723, 0x0723U, "Output Speed Sensor Intermittent") \
  X(P0724, 0x0724U, "Torque Converter/Brake Switch B Circuit High") \
  X(P0725, 0x0725U, "Engine Speed input Circuit Malfunction") \
  X(P0726, 0x0726U, "Engine Speed Input Circuit Range/Performance") \
  X(P0727, 0x0727U, "Engine Speed Input Circuit No Signal") \
  X(P0728, 0x0728U, "Engine Speed Input Circuit Intermittent") \
  X(P0729, 0x0729U, "Gear 6 Incorrect Ratio") \
  X(P0730, 0x0730U, "Incorrect Gear Ratio") \
  X(P0731, 0x0731U, "Gear I Incorrect ratio") \
  X(P0732, 0x0732U, "Gear 2 Incorrect ratio") \
  X(P0733, 0x0733U, "Gear 3 Incorrect ratio") \
  X(P0734, 0x0734U, "Gear 4 Incorrect ratio") \
  X(P0735, 0x0735U, "Gear 5 Incorrect ratio") \
  X(P0736, 0x0736U, "Reverse incorrect gear ratio") \
  X(P0737, 0x0737U, "TCM Engine Speed Output Circuit") \
  X(P0738, 0x0738U, "TCM Engine Speed Output Circuit Low") \
  X(P0739, 0x0739U, "TCM Engine Speed Output Circuit High") \
  X(P0740, 0x0740U, "Torque Converter Clutch Circuit Malfunction") \
  X(P0741, 0x0741U, "Torque Converter Clutch Circuit Performance or Stuck Off") \
  X(P0742, 0x0742U, "Torque Converter Clutch Circuit Stock On") \
  X(P0743, 0x0743U, "Torque Converter Clutch Circuit Electrical") \
  X(P0744, 0x0744U, "Torque Converter Clutch Circuit Intermittent") \
  X(P0745, 0x0745U, "Pressure Control Solenoid Malfunction") \
  X(P0746, 0x0746U, "Pressure Control Solenoid Performance or Stuck Off") \
  X(P0747, 0x0747U, "Pressure Control Solenoid Stuck On") \
  X(P0748, 0x0748U, "Pressure Control Solenoid Electrical") \
  X(P0749, 0x0749U, "Pressure Control Solenoid Intermittent") \
  X(P0750, 0x0750U, "Shift Solenoid A Malfunction") \
  X(P0751, 0x0751U, "Shift Solenoid A Performance or Stuck Off") \
  X(P0752, 0x0752U, "Shift Solenoid A Stuck On") \
  X(P0753, 0x0753U, "Shift Solenoid A Electrical") \
  X(P0754, 0x0754U, "Shift Solenoid A Intermittent") \
  X(P0755, 0x0755U, "Shift Solenoid B Malfunction") \
  X(P0756, 0x0756U, "Shift Solenoid B Performance or Stock Off") \
  X(P0757, 0x0757U, "Shift Solenoid B Stuck On") \
  X(P0758, 0x0758U, "Shift Solenoid B Electrical") \
  X(P0759, 0x0759U, "Shift Solenoid B Intermittent") \
  X(P0760, 0x0760U, "Shift Solenoid C Malfunction") \
  X(P0761, 0x0761U, "Shift Solenoid C Performance or Stuck Off") \
  X(P0762, 0x0762U, "Shift Solenoid C Stuck On") \
  X(P0763, 0x0763U, "Shift Solenoid C Electrical") \
  X(P0764, 0x0764U, "Shift Solenoid C Intermittent") \
  X(P0765, 0x0765U, "Shift Solenoid D Malfunction") \
  X(P0766, 0x0766U, "Shift Solenoid D Performance or Stuck Off") \
  X(P0767, 0x0767U, "Shift Solenoid D Stuck On") \
  X(P0768, 0x0768U, "Shift Solenoid D Electrical") \
  X(P0769, 0x0769U, "Shift Solenoid D Intermittent") \
  X(P0770, 0x0770U, "Shift Solenoid E Malfunction") \
  X(P0771, 0x0771U, "Shift Solenoid E Performance or Stuck Off") \
  X(P0772, 0x0772U, "Shift Solenoid E Stuck On") \
  X(P0773, 0x0773U, "Shift Solenoid E Electrical") \
  X(P0774, 0x0774U, "Shift Solenoid E Intermittent") \
  X(P0775, 0x0775U, "Pressure Control Solenoid \"B\"") \
  X(P0776, 0x0776U, "Pressure Control Solenoid \"B\" Performance or Stuck off") \
  X(P0777, 0x0777U, "Pressure Control Solenoid \"B\" Stuck On") \
  X(P0778, 0x0778U, "Pressure Control Solenoid \"B\" Electrical") \
  X(P0779, 0x0779U, "Pressure Control Solenoid \"B\" Intermittent") \
  X(P0780, 0x0780U, "Shift Malfunction") \
  X(P0781, 0x0781U, "1-2 Shift Malfunction") \
  X(P0782, 0x0782U, "2-3 Shift Malfunction") \
  X(P0783, 0x0783U, "3-4 Shift Malfunction") \
  X(P0784, 0x0784U, "4-5 Shift Malfunction") \
  X(P0785, 0x0785U, "Shift/Timing Solenoid Malfunction") \
  X(P0786, 0x0786U, "Shift/Timing Solenoid Range/Performance") \
  X(P0787, 0x0787U, "Shift/Timing Solenoid Low") \
  X(P0788, 0x0788U, "Shift/Timing Solenoid High") \
  X(P0789, 0x0789U, "Shift/Timing Solenoid Intermittent") \
  X(P0790, 0x0790U, "Normal/Performance Switch Circuit Malfunction") \
  X(P0791, 0x0791U, "Intermediate Shaft Speed Sensor \"A\" Circuit") \
  X(P0792, 0x0792U, "Intermediate Shaft Speed Sensor \"A\" Circuit Range/Performance") \
  X(P0793, 0x0793U, "Intermediate Shaft Speed Sensor \"A\" Circuit No Signal") \
  X(P0794, 0x0794U, "Intermediate Shaft Speed Sensor \"A\" Circuit Intermittent") \
  X(P0795, 0x0795U, "Pressure Control Solenoid \"C\"") \
  X(P0796, 0x0796U, "Pressure Control Solenoid \"C\" Performance or Stuck off") \
  X(P0797, 0x0797U, "Pressure Control Solenoid \"C\" Stuck On") \
  X(P0798, 0x0798U, "Pressure Control Solenoid \"C\" Electrical") \
  X(P0799, 0x0799U, "Pressure Control Solenoid \"C\" Intermittent") \
  X(P0800, 0x0800U, "Transfer Case Control System (MIL Request)") \
  X(P0801, 0x0801U, "Reverse Inhibit Control Circuit Malfunction") \
  X(P0802, 0x0802U, "Transmission Control System MIL Request Circuit/Open") \
  X(P0803, 0x0803U, "1-4 Upshift (Skip Shift) Solenoid Control Circuit Malfunction") \
  X(P0804, 0x0804U, "1-4 Upshift (Skip Shift) Lamp Control Circuit Malfunction") \
  X(P0805, 0x0805U, "Clutch Position Sensor Circuit") \
  X(P0806, 0x0806U, "Clutch Position Sensor Circuit Range/Performance") \
  X(P0807, 0x0807U, "Clutch Position Sensor Circuit Low") \
  X(P0808, 0x0808U, "Clutch Position Sensor Circuit High") \
  X(P0809, 0x0809U, "Clutch Position Sensor Circuit Intermittent") \
  X(P0810, 0x0810U, "Clutch Position Control Error") \
  X(P0811, 0x0811U, "Excessive Clutch Slippage") \
  X(P0812, 0x0812U, "Reverse Input Circuit") \
  X(P0813, 0x0813U, "Reverse Output Circuit") \
  X(P0814, 0x0814U, "Transmission Range Display Circuit") \
  X(P0815, 0x0815U, "Upshift Switch Circuit") \
  X(P0816, 0x0816U, "Downshift Switch Circuit") \
  X(P0817, 0x0817U, "Starter Disable Circuit") \
  X(P0818, 0x0818U, "Driveline Disconnect Switch Input Circuit") \
  X(P0819, 0x0819U, "Up and Down Shift Switch to Transmission Range Correlation") \
  X(P0820, 0x0820U, "Gear Lever X-Y Position Sensor Circuit") \
  X(P0821, 0x0821U, "Gear Lever X Position Circuit") \
  X(P0822, 0x0822U, "Gear Lever Y Position Circuit") \
  X(P0823, 0x0823U, "Gear Lever X Position Circuit Intermittent") \
  X(P0824, 0x0824U, "Gear Lever Y Position Circuit Intermittent") \
  X(P0825, 0x0825U, "Gear Lever Push-Pull Switch (Shift Anticipate") \
  X(P0826, 0x0826U, "Up and Down Shift Switch Circuit") \
  X(P0827, 0x0827U, "Up and Down Shift Switch Circuit Low") \
  X(P0828, 0x0828U, "Up and Down Shift Switch Circuit High") \
  X(P0829, 0x0829U, "5-6 Shift") \
  X(P0830, 0x0830U, "Clutch Pedal Switch \"A\" Circuit") \
  X(P0831, 0x0831U, "Clutch Pedal Switch \"A\" Circuit Low") \
  X(P0832, 0x0832U, "Clutch Pedal Switch \"A\" Circuit High") \
  X(P0833, 0x0833U, "Clutch Pedal Switch \"B\" Circuit") \
  X(P0834, 0x0834U, "Clutch Pedal Switch \"B\" Circuit Low") \
  X(P0835, 0x0835U, "Clutch Pedal Switch \"B\" Circuit High") \
  X(P0836, 0x0836U, "Four Wheel Drive (4WD) Switch Circuit") \
  X(P0837, 0x0837U, "Four Wheel Drive (4WD) Switch Circuit Range/Performance") \
  X(P0838, 0x0838U, "Four Wheel Drive (4WD) Switch Circuit Low") \
  X(P0839, 0x0839U, "Four Wheel Drive (4WD) Switch Circuit High P0840 Transmission Fluid Pressure Sensor/Switch \"A\" Circuit") \
  X(P0840, 0x0840U, "Transmission Fluid Pressure Sensor/Switch \"A\" Circuit") \
  X(P0841, 0x0841U, "Transmission Fluid Pressure Sensor/Switch \"A\" Circuit Range/Performance") \
  X(P0842, 0x0842U, "Transmission Fluid Pressure Sensor/Switch \"A\" Circuit Low") \
  X(P0843, 0x0843U, "Transmission Fluid Pressure Sensor/Switch \"A\" Circuit High") \
  X(P0844, 0x0844U, "Transmission Fluid Pressure Sensor/Switch \"A\" Circuit Intermittent") \
  X(P0845, 0x0845U, "Transmission Fluid Pressure Sensor/Switch \"B\" Circuit") \
  X(P0846, 0x0846U, "Transmission Fluid Pressure Sensor/Switch \"B\" Circuit Range/Performance") \
  X(P0847, 0x0847U, "Transmission Fluid Pressure Sensor/Switch \"B\" Circuit Low") \
  X(P0848, 0x0848U, "Transmission Fluid Pressure Sensor/Switch \"B\" Circuit High") \
  X(P0849, 0x0849U, "Transmission Fluid Pressure Sensor/Switch \"B\" Circuit Intermittent") \
  X(P0850, 0x0850U, "Park/Neutral Switch Input Circuit") \
  X(P0851, 0x0851U, "Park/Neutral Switch Input Circuit Low") \
  X(P0852, 0x0852U, "Park/Neutral Switch Input Circuit High") \
  X(P0853, 0x0853U, "Drive Switch Input Circuit") \
  X(P0854, 0x0854U, "Drive Switch Input Circuit Low") \
  X(P0855, 0x0855U, "Drive Switch Input Circuit High") \
  X(P0856, 0x0856U, "Traction Control Input Signal") \
  X(P0857, 0x0857U, "Traction Control Input Signal Range/Performance") \
  X(P0858, 0x0858U, "Traction Control Input Signal Low") \
  X(P0859, 0x0859U, "Traction Control Input Signal High") \
  X(P0860, 0x0860U, "Gear Shift Module Communication Circuit") \
  X(P0861, 0x0861U, "Gear Shift Module Communication Circuit Low") \
  X(P0862, 0x0862U, "Gear Shift Module Communication Circuit High") \
  X(P0863, 0x0863U, "TCM Communication Circuit") \
  X(P0864, 0x0864U, "TCM Communication Circuit Range/Performance") \
  X(P0865, 0x0865U, "TCM Communication Circuit Low") \
  X(P0866, 0x0866U, "TCM Communication Circuit High") \
  X(P0867, 0x0867U, "Transmission Fluid Pressure") \
  X(P0868, 0x0868U, "Transmission Fluid Pressure Low P0869 Transmission Fluid Pressure High") \
  X(P0869, 0x0869U, "Transmission Fluid Pressure High") \
  X(P0870, 0x0870U, "Transmission Fluid Pressure Sensor/Switch \"C\" Circuit") \
  X(P0871, 0x0871U, "Transmission Fluid Pressure Sensor/Switch \"C\" Circuit Range/Performance") \
  X(P0872, 0x0872U, "Transmission Fluid Pressure Sensor/Switch \"C\" Circuit Low") \
  X(P0873, 0x0873U, "Transmission Fluid Pressure Sensor/Switch \"C\" Circuit High") \
  X(P0874, 0x0874U, "Transmission Fluid Pressure Sensor/Switch \"C\" Circuit Intermittent") \
  X(P0875, 0x0875U, "Transmission Fluid Pressure Sensor/Switch \"D\" Circuit") \
  X(P0876, 0x0876U, "Transmission Fluid Pressure Sensor/Switch \"D\" Circuit Range/Performance") \
  X(P0877, 0x0877U, "Transmission Fluid Pressure Sensor/Switch \"D\" Circuit Low") \
  X(P0878, 0x0878U, "Transmission Fluid Pressure Sensor/Switch \"D\" Circuit High") \
  X(P0879, 0x0879U, "Transmission Fluid Pressure Sensor/Switch \"D\" Circuit Intermittent") \
  X(P0880, 0x0880U, "TCM Power Input Signal") \
  X(P0881, 0x0881U, "TCM Power Input Signal Range/Performance") \
  X(P0882, 0x0882U, "TCM Power Input Signal Low") \
  X(P0883, 0x0883U, "TCM Power Input Signal High") \
  X(P0884, 0x0884U, "TCM Power Input Signal Intermittent") \
  X(P0885, 0x0885U, "TCM Power Relay Control Circuit/Open") \
  X(P0886, 0x0886U, "TCM Power Relay Control Circuit Low") \
  X(P0887, 0x0887U, "TCM Power Relay Control Circuit High") \
  X(P0888, 0x0888U, "TCM Power Relay Sense Circuit") \
  X(P0889, 0x0889U, "TCM Power Relay Sense Circuit Range/Performance") \
  X(P0890, 0x0890U, "TCM Power Relay Sense Circuit Low") \
  X(P0891, 0x0891U, "TCM Power Relay Sense Circuit High") \
  X(P0892, 0x0892U, "TCM Power Relay Sense Circuit Intermittent") \
  X(P0893, 0x0893U, "Multiple Gears Engaged") \
  X(P0894, 0x0894U, "Transmission Component Slipping") \
  X(P0895, 0x0895U, "Shift Time Too Short") \
  X(P0896, 0x0896U, "Shift Time Too Long") \
  X(P0897, 0x0897U, "Transmission Fluid Deteriorated") \
  X(P0898, 0x0898U, "Transmission Control System MIL Request Circuit Low") \
  X(P0899, 0x0899U, "Transmission Control System MIL Request Circuit High") \
  X(P2000, 0x2000U, "NOx Trap Efficiency Below Threshold Bank1") \
  X(P2001, 0x2001U, "NOx Trap Efficiency Below Threshold Bank2") \
  X(P2002, 0x2002U, "Particulate Trap Efficiency Below Threshold Bank1") \
  X(P2003, 0x2003U, "Particulate Trap Efficiency Below Threshold Bank2") \
  X(P2004, 0x2004U, "Intake Manifold Runner Ctrl Stuck Open Bank1") \
  X(P2005, 0x2005U, "Intake Manifold Runner Ctrl Stuck Open Bank2") \
  X(P2006, 0x2006U, "Intake Manifold Runner Ctrl Stuck Closed Bank1") \
  X(P2007, 0x2007U, "Intake Manifold Runner Ctrl Stuck Closed Bank2") \
  X(P2008, 0x2008U, "Intake Manifold Runner Ctrl Circ/Open Bank1") \
  X(P2009, 0x2009U, "Intake Manifold Runner Ctrl Circ Low Bank1") \
  X(P2010, 0x2010U, "Intake Manifold Runner Ctrl Circ High Bank1") \
  X(P2011, 0x2011U, "Intake Manifold Runner Ctrl Circ/Open Bank2") \
  X(P2012, 0x2012U, "Intake Manifold Runner Ctrl Circ Low Bank2") \
  X(P2013, 0x2013U, "Intake Manifold Runner Ctrl Circ High Bank2") \
  X(P2014, 0x2014U, "Intake Manifold Runner Pos Sensor/Switch Circ Bank1") \
  X(P2015, 0x2015U, "Intake Manifold Runner Pos Sensor/Switch Circ Range/Perf Bank1") \
  X(P2016, 0x2016U, "Intake Manifold Runner Pos Sensor/Switch Circ Low Bank1") \
  X(P2017, 0x2017U, "Intake Manifold Runner Pos Sensor/Switch Circ High Bank1") \
  X(P2018, 0x2018U, "Intake Manifold Runner Pos Sensor/Switch Circ Interm Bank1") \
  X(P2019, 0x2019U, "Intake Manifold Runner Pos Sensor/Switch Circ Bank2") \
  X(P2020, 0x2020U, "Intake Manifold Runner Pos Sensor/Switch Circ Range/Perf Bank2") \
  X(P2021, 0x2021U, "Intake Manifold Runner Pos Sensor/Switch Circ Low Bank2") \
  X(P2022, 0x2022U, "Intake Manifold Runner Pos Sensor/Switch Circ High Bank2") \
  X(P2023, 0x2023U, "Intake Manifold Runner Pos Sensor/Switch Circ Interm Bank2") \
  X(P2024, 0x2024U, "EVAP Fuel Vapor Temp Sensor Circ") \
  X(P2025, 0x2025U, "EVAP Fuel Vapor Temp Sensor Perf") \
  X(P2026, 0x2026U, "EVAP Fuel Vapor Temp Sensor Circ Low Voltage") \
  X(P2027, 0x2027U, "EVAP Fuel Vapor Temp Sensor Circ High Voltage") \
  X(P2028, 0x2028U, "EVAP Fuel Vapor Temp Sensor Circ Interm") \
  X(P2029, 0x2029U, "Fuel Fired Heater Disabled") \
  X(P2030, 0x2030U, "Fuel Fired Heater Perf") \
  X(P2031, 0x2031U, "Exhaust Gas Temp Sensor Circ Bank1 Sensor 2") \
  X(P2032, 0x2032U, "Exhaust Gas Temp Sensor Circ Low Bank1 Sensor 2") \
  X(P2033, 0x2033U, "Exhaust Gas Temp Sensor Circ High Bank1 Sensor 2") \
  X(P2034, 0x2034U, "Exhaust Gas Temp Sensor Circ Bank2 Sensor 2") \
  X(P2035, 0x2035U, "Exhaust Gas Temp Sensor Circ Low Bank2 Sensor 2") \
  X(P2036, 0x2036U, "Exhaust Gas Temp Sensor Circ High Bank2 Sensor 2") \
  X(P2037, 0x2037U, "Reductant Inj Air Press Sensor Circ") \
  X(P2038, 0x2038U, "Reductant Inj Air Press Sensor Circ Range/Perf") \
  X(P2039, 0x2039U, "Reductant Inj Air Press Sensor Circ Low Input") \
  X(P2040, 0x2040U, "Reductant Inj Air Press Sensor Circ High Input") \
  X(P2041, 0x2041U, "Reductant Inj Air Press Sensor Circ Interm") \
  X(P2042, 0x2042U, "Reductant Temp Sensor Circ") \
  X(P2043, 0x2043U, "Reductant Temp Sensor Circ Range/Perf") \
  X(P2044, 0x2044U, "Reductant Temp Sensor Circ Low Input") \
  X(P2045, 0x2045U, "Reductant Temp Sensor Circ High Input") \
  X(P2046, 0x2046U, "Reductant Temp Sensor Circ Interm") \
  X(P2047, 0x2047U, "Reductant Injector Circ/Open Bank1 Unit 1") \
  X(P2048, 0x2048U, "Reductant Injector Circ Low Bank1 Unit 1") \
  X(P2049, 0x2049U, "Reductant Injector Circ High Bank1 Unit 1") \
  X(P2050, 0x2050U, "Reductant Injector Circ/Open Bank2 Unit 1") \
  X(P2051, 0x2051U, "Reductant Injector Circ Low Bank2 Unit 1") \
  X(P2052, 0x2052U, "Reductant Injector Circ High Bank2 Unit 1") \
  X(P2053, 0x2053U, "Reductant Injector Circ/Open Bank1 Unit 2") \
  X(P2054, 0x2054U, "Reductant Injector Circ Low Bank1 Unit 2") \
  X(P2055, 0x2055U, "Reductant Injector Circ High Bank1 Unit 2") \
  X(P2056, 0x2056U, "Reductant Injector Circ/Open Bank2 Unit 2") \
  X(P2057, 0x2057U, "Reductant Injector Circ Low Bank2 Unit 2") \
  X(P2058, 0x2058U, "Reductant Injector Circ High Bank2 Unit 2") \
  X(P2059, 0x2059U, "Reductant Inj Air Pump Ctrl Circ/Open") \
  X(P2060, 0x2060U, "Reductant Inj Air Pump Ctrl Circ Low") \
  X(P2061, 0x2061U, "Reductant Inj Air Pump Ctrl Circ High") \
  X(P2062, 0x2062U, "Reductant Supply Ctrl Circ/Open") \
  X(P2063, 0x2063U, "Reductant Supply Ctrl Circ Low") \
  X(P2064, 0x2064U, "Reductant Supply Ctrl Circ High") \
  X(P2065, 0x2065U, "Fuel Level SensorB Circ") \
  X(P2066, 0x2066U, "Fuel Level SensorB Perf") \
  X(P2067, 0x2067U, "Fuel Level SensorB Circ Low") \
  X(P2068, 0x2068U, "Fuel Level SensorB Circ High") \
  X(P2069, 0x2069U, "Fuel Level SensorB Circ Interm") \
  X(P2070, 0x2070U, "Intake Manifold Tuning (IMT) Valve Stuck Open") \
  X(P2071, 0x2071U, "IMT Valve Stuck Closed") \
  X(P2072, 0x2072U, "Throttle Actuator Control System - Ice Blockage") \
  X(P2073, 0x2073U, "Manifold Absolute Pressure/Mass Air Flow - Throttle Position Correlation at Idle") \
  X(P2074, 0x2074U, "Manifold Absolute Pressure/Mass Air Flow - Throttle Position Correlation at Higher Load") \
  X(P2075, 0x2075U, "IMT Valve Pos Sensor/Switch Circ") \
  X(P2076, 0x2076U, "IMT Valve Pos Sensor/Switch Circ Range/Perf") \
  X(P2077, 0x2077U, "IMT Valve Pos Sensor/Switch Circ Low") \
  X(P2078, 0x2078U, "IMT Valve Pos Sensor/Switch Circ High") \
  X(P2079, 0x2079U, "IMT Valve Pos Sensor/Switch Circ Interm") \
  X(P2080, 0x2080U, "Exhaust Gas Temp Sensor Circ Range/Perf Bank1 Sensor 1") \
  X(P2081, 0x2081U, "Exhaust Gas Temp Sensor Circ Interm Bank1 Sensor 1") \
  X(P2082, 0x2082U, "Exhaust Gas Temp Sensor Circ Range/Perf Bank2 Sensor 1") \
  X(P2083, 0x2083U, "Exhaust Gas Temp Sensor Circ Interm Bank2 Sensor 1") \
  X(P2084, 0x2084U, "Exhaust Gas Temp Sensor Circ Range/Perf Bank1 Sensor 2") \
  X(P2085, 0x2085U, "Exhaust Gas Temp Sensor Circ Interm Bank1 Sensor 2") \
  X(P2086, 0x2086U, "Exhaust Gas Temp Sensor Circ Range/Perf Bank2 Sensor 2") \
  X(P2087, 0x2087U, "Exhaust Gas Temp Sensor Circ Interm Bank2 Sensor 2") \
  X(P2088, 0x2088U, "A Camshaft Pos Actuator Ctrl Circ Low Bank1") \
  X(P2089, 0x2089U, "A Camshaft Pos Actuator Ctrl Circ High Bank1") \
  X(P2090, 0x2090U, "B Camshaft Pos Actuator Ctrl Circ Low Bank1") \
  X(P2091, 0x2091U, "B Camshaft Pos Actuator Ctrl Circ High Bank1") \
  X(P2092, 0x2092U, "A Camshaft Pos Actuator Ctrl Circ Low Bank2") \
  X(P2093, 0x2093U, "A Camshaft Pos Actuator Ctrl Circ High Bank2") \
  X(P2094, 0x2094U, "B Camshaft Pos Actuator Ctrl Circ Low Bank2") \
  X(P2095, 0x2095U, "B Camshaft Pos Actuator Ctrl Circ High Bank2") \
  X(P2096, 0x2096U, "Post Catalyst Fuel Trim Sys Too Lean Bank1") \
  X(P2097, 0x2097U, "Post Catalyst Fuel Trim Sys Too Rich Bank1") \
  X(P2098, 0x2098U, "Post Catalyst Fuel Trim Sys Too Lean Bank2") \
  X(P2099, 0x2099U, "Post Catalyst Fuel Trim Sys Too Rich Bank2") \
  X(P2100, 0x2100U, "Throttle Actuator Ctrl Motor Circ/Open") \
  X(P2101, 0x2101U, "Throttle Actuator Ctrl Motor Circ Range/Perf") \
  X(P2102, 0x2102U, "Throttle Actuator Ctrl Motor Circ Low") \
  X(P2103, 0x2103U, "Throttle Actuator Ctrl Motor Circ High") \
  X(P2104, 0x2104U, "Throttle Actuator Ctrl Sys-Forced Idle") \
  X(P2105, 0x2105U, "Throttle Actuator Ctrl Sys-Forced Engine Shutdown") \
  X(P2106, 0x2106U, "Throttle Actuator Ctrl Sys-Forced Limited Power") \
  X(P2107, 0x2107U, "Throttle Actuator Ctrl Mod Processor") \
  X(P2108, 0x2108U, "Throttle Actuator Ctrl Mod Perf") \
  X(P2109, 0x2109U, "Throttle/Pedal Pos SensorA Minimum Stop Perf") \
  X(P2110, 0x2110U, "Throttle Actuator Ctrl Sys-Forced Limited RPM") \
  X(P2111, 0x2111U, "Throttle Actuator Ctrl Sys-Stuck Open") \
  X(P2112, 0x2112U, "Throttle Actuator Ctrl Sys-Stuck Closed") \
  X(P2113, 0x2113U, "Throttle/Pedal Pos SensorB Minimum Stop Perf") \
  X(P2114, 0x2114U, "Throttle/Pedal Pos Sensor C Minimum Stop Perf") \
  X(P2115, 0x2115U, "Throttle/Pedal Pos Sensor D Minimum Stop Perf") \
  X(P2116, 0x2116U, "Throttle/Pedal Pos Sensor E Minimum Stop Perf") \
  X(P2117, 0x2117U, "Throttle/Pedal Pos Sensor F Minimum Stop Perf") \
  X(P2118, 0x2118U, "Throttle Actuator Ctrl Motor Current Range/Perf") \
  X(P2119, 0x2119U, "Throttle Actuator Ctrl Throttle Body Range/Perf") \
  X(P2120, 0x2120U, "Throttle/Pedal Pos Sensor/Switch D Circ") \
  X(P2121, 0x2121U, "Throttle/Pedal Pos Sensor/Switch D Circ Range/Perf") \
  X(P2122, 0x2122U, "Throttle/Pedal Pos Sensor/Switch D Circ Low Input") \
  X(P2123, 0x2123U, "Throttle/Pedal Pos Sensor/Switch D Circ High Input") \
  X(P2124, 0x2124U, "Throttle/Pedal Pos Sensor/Switch D Circ Interm") \
  X(P2125, 0x2125U, "Throttle/Pedal Pos Sensor/Switch E Circ") \
  X(P2126, 0x2126U, "Throttle/Pedal Pos Sensor/Switch E Circ Range/Perf") \
  X(P2127, 0x2127U, "Throttle/Pedal Pos Sensor/Switch E Circ Low Input") \
  X(P2128, 0x2128U, "Throttle/Pedal Pos Sensor/Switch E Circ High Input") \
  X(P2129, 0x2129U, "Throttle/Pedal Pos Sensor/Switch E Circ Interm") \
  X(P2130, 0x2130U, "Throttle/Pedal Pos Sensor/Switch F Circ") \
  X(P2131, 0x2131U, "Throttle/Pedal Pos Sensor/Switch F Circ Range Perf") \
  X(P2132, 0x2132U, "Throttle/Pedal Pos Sensor/Switch F Circ Low Input") \
  X(P2133, 0x2133U, "Throttle/Pedal Pos Sensor/Switch F Circ High Input") \
  X(P2134, 0x2134U, "Throttle/Pedal Pos Sensor/Switch F Circ Interm") \
  X(P2135, 0x2135U, "Throttle/Pedal Pos Sensor/Switch A / B Voltage Correlation") \
  X(P2136, 0x2136U, "Throttle/Pedal Pos Sensor/Switch A / C Voltage Correlation") \
  X(P2137, 0x2137U, "Throttle/Pedal Pos Sensor/Switch B / C Voltage Correlation") \
  X(P2138, 0x2138U, "Throttle/Pedal Pos Sensor/Switch D / E Voltage Correlation") \
  X(P2139, 0x2139U, "Throttle/Pedal Pos Sensor/Switch D / F Voltage Correlation") \
  X(P2140, 0x2140U, "Throttle/Pedal Pos Sensor/Switch E / F Voltage Correlation") \
  X(P2141, 0x2141U, "Exhaust Gas Recirculation Throttle Ctrl Circ Low") \
  X(P2142, 0x2142U, "Exhaust Gas Recirculation Throttle Ctrl Circ High") \
  X(P2143, 0x2143U, "Exhaust Gas Recirculation Vent Ctrl Circ/Open") \
  X(P2144, 0x2144U, "Exhaust Gas Recirculation Vent Ctrl Circ Low") \
  X(P2145, 0x2145U, "Exhaust Gas Recirculation Vent Ctrl Circ High") \
  X(P2146, 0x2146U, "Fuel Injector Group A Supply Voltage Circ/Open") \
  X(P2147, 0x2147U, "Fuel Injector Group A Supply Voltage Circ Low") \
  X(P2148, 0x2148U, "Fuel Injector Group A Supply Voltage Circ High") \
  X(P2149, 0x2149U, "Fuel Injector Group B Supply Voltage Circ/Open") \
  X(P2150, 0x2150U, "Fuel Injector Group B Supply Voltage Circ Low") \
  X(P2151, 0x2151U, "Fuel Injector Group B Supply Voltage Circ High") \
  X(P2152, 0x2152U, "Fuel Injector Group C Supply Voltage Circ/Open") \
  X(P2153, 0x2153U, "Fuel Injector Group C Supply Voltage Circ Low") \
  X(P2154, 0x2154U, "Fuel Injector Group C Supply Voltage Circ High") \
  X(P2155, 0x2155U, "Fuel Injector Group D Supply Voltage Circ/Open") \
  X(P2156, 0x2156U, "Fuel Injector Group D Supply Voltage Circ Low") \
  X(P2157, 0x2157U, "Fuel Injector Group D Supply Voltage Circ High") \
  X(P2158, 0x2158U, "Vehicle Speed SensorB") \
  X(P2159, 0x2159U, "Vehicle Speed SensorB Range/Perf") \
  X(P2160, 0x2160U, "Vehicle Speed SensorB Circ Low") \
  X(P2161, 0x2161U, "Vehicle Speed SensorB Interm/Erratic") \
  X(P2162, 0x2162U, "Vehicle Speed SensorA / B Correlation") \
  X(P2163, 0x2163U, "Throttle/Pedal Pos SensorA Maximum Stop Perf") \
  X(P2164, 0x2164U, "Throttle/Pedal Pos SensorB Maximum Stop Perf") \
  X(P2165, 0x2165U, "Throttle/Pedal Pos Sensor C Maximum Stop Perf") \
  X(P2166, 0x2166U, "Throttle/Pedal Pos Sensor D Maximum Stop Perf") \
  X(P2167, 0x2167U, "Throttle/Pedal Pos Sensor E Maximum Stop Perf") \
  X(P2168, 0x2168U, "Throttle/Pedal Pos Sensor F Maximum Stop Perf") \
  X(P2169, 0x2169U, "Exhaust Press Reg Vent Solenoid Ctrl Circ/Open") \
  X(P2170, 0x2170U, "Exhaust Press Reg Vent Solenoid Ctrl Circ Low") \
  X(P2171, 0x2171U, "Exhaust Press Reg Vent Solenoid Ctrl Circ High") \
  X(P2172, 0x2172U, "Throttle Actuator Ctrl Sys-Sudden High Airflow Detected") \
  X(P2173, 0x2173U, "Throttle Actuator Ctrl Sys-High Airflow Detected") \
  X(P2174, 0x2174U, "Throttle Actuator Ctrl Sys-Sudden Low Airflow Detected") \
  X(P2175, 0x2175U, "Throttle Actuator Ctrl Sys-Low Airflow Detected") \
  X(P2176, 0x2176U, "Throttle Actuator Ctrl Sys-Idle Pos Not Learned") \
  X(P2177, 0x2177U, "Sys Too Lean Off Idle Bank1") \
  X(P2178, 0x2178U, "Sys Too Rich Off Idle Bank1") \
  X(P2179, 0x2179U, "Sys Too Lean Off Idle Bank2") \
  X(P2180, 0x2180U, "Sys Too Rich Off Idle Bank2") \
  X(P2181, 0x2181U, "Cooling System Performance") \
  X(P2182, 0x2182U, "Engine Coolant Temp Sensor 2 Circ") \
  X(P2183, 0x2183U, "Engine Coolant Temp Sensor 2 Circ Range/Perf") \
  X(P2184, 0x2184U, "Engine Coolant Temp Sensor 2 Circ Low") \
  X(P2185, 0x2185U, "Engine Coolant Temp Sensor 2 Circ High") \
  X(P2186, 0x2186U, "Engine Coolant Temp Sensor 2 Circ Interm/Erratic") \
  X(P2187, 0x2187U, "Sys Too Lean at Idle Bank1") \
  X(P2188, 0x2188U, "Sys Too Rich at Idle Bank1") \
  X(P2189, 0x2189U, "Sys Too Lean at Idle Bank2") \
  X(P2190, 0x2190U, "Sys Too Rich at Idle Bank2") \
  X(P2191, 0x2191U, "Sys Too Lean at Higher Load Bank1") \
  X(P2192, 0x2192U, "Sys Too Rich at Higher Load Bank1") \
  X(P2193, 0x2193U, "Sys Too Lean at Higher Load Bank2") \
  X(P2194, 0x2194U, "Sys Too Rich at Higher Load Bank2") \
  X(P2195, 0x2195U, "O2 Sensor Signal Stuck Lean Bank1 Sensor 1") \
  X(P2196, 0x2196U, "O2 Sensor Signal Stuck Rich Bank1 Sensor 1") \
  X(P2197, 0x2197U, "O2 Sensor Signal Stuck Lean Bank2 Sensor 1") \
  X(P2198, 0x2198U, "O2 Sensor Signal Stuck Rich Bank2 Sensor 1") \
  X(P2199, 0x2199U, "Intake Air Temp Sensor 1 / 2 Correlation") \
  X(P2200, 0x2200U, "NOx Sensor Circ Bank1") \
  X(P2201, 0x2201U, "NOx Sensor Circ Range/Perf Bank1") \
  X(P2202, 0x2202U, "NOx Sensor Circ Low Input Bank1") \
  X(P2203, 0x2203U, "NOx Sensor Circ High Input Bank1") \
  X(P2204, 0x2204U, "NOx Sensor Circ Interm Input Bank1") \
  X(P2205, 0x2205U, "NOx Sensor Heater Ctrl Circ/Open Bank1") \
  X(P2206, 0x2206U, "NOx Sensor Heater Ctrl Circ Low Bank1") \
  X(P2207, 0x2207U, "NOx Sensor Heater Ctrl Circ High Bank1") \
  X(P2208, 0x2208U, "NOx Sensor Heater Sense Circ Bank1") \
  X(P2209, 0x2209U, "NOx Sensor Heater Sense Circ Range/Perf Bank1") \
  X(P2210, 0x2210U, "NOx Sensor Heater Sense Circ Low Input Bank1") \
  X(P2211, 0x2211U, "NOx Sensor Heater Sense Circ High Input Bank1") \
  X(P2212, 0x2212U, "NOx Sensor Heater Sense Circ Interm Bank1") \
  X(P2213, 0x2213U, "NOx Sensor Circ Bank2") \
  X(P2214, 0x2214U, "NOx Sensor Circ Range/Perf Bank2") \
  X(P2215, 0x2215U, "NOx Sensor Circ Low Input Bank2") \
  X(P2216, 0x2216U, "NOx Sensor Circ High Input Bank2") \
  X(P2217, 0x2217U, "NOx Sensor Circ Interm Input Bank2") \
  X(P2218, 0x2218U, "NOx Sensor Heater Ctrl Circ/Open Bank2") \
  X(P2219, 0x2219U, "NOx Sensor Heater Ctrl Circ Low Bank2") \
  X(P2220, 0x2220U, "NOx Sensor Heater Ctrl Circ High Bank2") \
  X(P2221, 0x2221U, "NOx Sensor Heater Sense Circ Bank2") \
  X(P2222, 0x2222U, "NOx Sensor Heater Sense Circ Range/Perf Bank2") \
  X(P2223, 0x2223U, "NOx Sensor Heater Sense Circ Low Bank2") \
  X(P2224, 0x2224U, "NOx Sensor Heater Sense Circ High Bank2") \
  X(P2225, 0x2225U, "NOx Sensor Heater Sense Circ Interm Bank2") \
  X(P2226, 0x2226U, "Barometric Pressure Circuit") \
  X(P2227, 0x2227U, "Barometric Pressure Circuit Range/Performance") \
  X(P2228, 0x2228U, "Barometric Press Circ Low") \
  X(P2229, 0x2229U, "Barometric Press Circ High") \
  X(P2230, 0x2230U, "Barometric Press Circ Interm") \
  X(P2232, 0x2232U, "O2 Sensor Signal Circ Shorted to Heater Circ Bank1 Sensor 2") \
  X(P2233, 0x2233U, "O2 Sensor Signal Circ Shorted to Heater Circ Bank1 Sensor 3") \
  X(P2234, 0x2234U, "O2 Sensor Signal Circ Shorted to Heater Circ Bank2 Sensor 1") \
  X(P2235, 0x2235U, "O2 Sensor Signal Circ Shorted to Heater Circ Bank2 Sensor 2") \
  X(P2236, 0x2236U, "O2 Sensor Signal Circ Shorted to Heater Circ Bank2 Sensor 3") \
  X(P2237, 0x2237U, "O2 Sensor Positive Current Ctrl Circ/Open Bank1 Sensor 1") \
  X(P2238, 0x2238U, "O2 Sensor Positive Current Ctrl Circ Low Bank1 Sensor 1") \
  X(P2239, 0x2239U, "O2 Sensor Positive Current Ctrl Circ High Bank1 Sensor 1") \
  X(P2240, 0x2240U, "O2 Sensor Positive Current Ctrl Circ/Open Bank2 Sensor 1") \
  X(P2241, 0x2241U, "O2 Sensor Positive Current Ctrl Circ Low Bank2 Sensor 1") \
  X(P2242, 0x2242U, "O2 Sensor Positive Current Ctrl Circ High Bank2 Sensor 1") \
  X(P2243, 0x2243U, "O2 Sensor Ref Voltage Circ/Open Bank1 Sensor 1") \
  X(P2244, 0x2244U, "O2 Sensor Ref Voltage Perf Bank1 Sensor 1") \
  X(P2245, 0x2245U, "O2 Sensor Ref Voltage Circ Low Bank1 Sensor 1") \
  X(P2246, 0x2246U, "O2 Sensor Ref Voltage Circ High Bank1 Sensor 1") \
  X(P2247, 0x2247U, "O2 Sensor Ref Voltage Circ/Open Bank2 Sensor 1") \
  X(P2248, 0x2248U, "O2 Sensor Ref Voltage Perf Bank2 Sensor 1") \
  X(P2249, 0x2249U, "O2 Sensor Ref Voltage Circ Low Bank2 Sensor 1") \
  X(P2250, 0x2250U, "O2 Sensor Ref Voltage Circ High Bank2 Sensor 1") \
  X(P2251, 0x2251U, "O2 Sensor Negative Current Ctrl Circ/Open Bank1 Sensor 1") \
  X(P2252, 0x2252U, "O2 Sensor Negative Current Ctrl Circ Low Bank1 Sensor 1") \
  X(P2253, 0x2253U, "O2 Sensor Negative Current Ctrl Circ High Bank1 Sensor 1") \
  X(P2254, 0x2254U, "O2 Sensor Negative Current Ctrl Circ/Open Bank2 Sensor 1") \
  X(P2255, 0x2255U, "O2 Sensor Negative Current Ctrl Circ Low Bank2 Sensor 1") \
  X(P2256, 0x2256U, "O2 Sensor Negative Current Ctrl Circ High Bank2 Sensor 1") \
  X(P2257, 0x2257U, "Sec Air Inj Sys Ctrl A Circ Low") \
  X(P2258, 0x2258U, "Sec Air Inj Sys Ctrl A Circ High") \
  X(P2259, 0x2259U, "Sec Air Inj Sys Ctrl B Circ Low") \
  X(P2260, 0x2260U, "Sec Air Inj Sys Ctrl B Circ High") \
  X(P2261, 0x2261U, "T/S Charger Bypass Valve-Mechanical") \
  X(P2262, 0x2262U, "Turbo Boost Press Not Detected-Mechanical") \
  X(P2263, 0x2263U, "T/S Charger Boost Sys Perf") \
  X(P2264, 0x2264U, "Water in Fuel Sensor Circ") \
  X(P2265, 0x2265U, "Water in Fuel Sensor Circ Range/Perf") \
  X(P2266, 0x2266U, "Water in Fuel Sensor Circ Low") \
  X(P2267, 0x2267U, "Water in Fuel Sensor Circ High") \
  X(P2268, 0x2268U, "Water in Fuel Sensor Circ Interm") \
  X(P2269, 0x2269U, "Water in Fuel Condition") \
  X(P2270, 0x2270U, "O2 Sensor Signal Stuck Lean Bank1 Sensor 2") \
  X(P2271, 0x2271U, "O2 Sensor Signal Stuck Rich Bank1 Sensor 2") \
  X(P2272, 0x2272U, "O2 Sensor Signal Stuck Lean Bank2 Sensor 2") \
  X(P2273, 0x2273U, "O2 Sensor Signal Stuck Rich Bank2 Sensor 2") \
  X(P2274, 0x2274U, "O2 Sensor Signal Stuck Lean Bank1 Sensor 3") \
  X(P2275, 0x2275U, "O2 Sensor Signal Stuck Rich Bank1 Sensor 3") \
  X(P2276, 0x2276U, "O2 Sensor Signal Stuck Lean Bank2 Sensor 3") \
  X(P2277, 0x2277U, "O2 Sensor Signal Stuck Rich Bank2 Sensor 3") \
  X(P2278, 0x2278U, "O2 Sensor Signals Swapped Bank1 Sensor 3 / Bank2 Sensor 3") \
  X(P2279, 0x2279U, "Intake Air Sys Leak") \
  X(P2280, 0x2280U, "Air Flow Restriction / Air Leak Between Air Filter and MAF") \
  X(P2281, 0x2281U, "Air Leak Between MAF and Throttle Body") \
  X(P2282, 0x2282U, "Air Leak Between Throttle Body and Intake Valves") \
  X(P2283, 0x2283U, "Injector Ctrl Press Sensor Circ") \
  X(P2284, 0x2284U, "Injector Ctrl Press Sensor Circ Range/Perf") \
  X(P2285, 0x2285U, "Injector Ctrl Press Sensor Circ Low") \
  X(P2286, 0x2286U, "Injector Ctrl Press Sensor Circ High") \
  X(P2287, 0x2287U, "Injector Ctrl Press Sensor Circ Interm") \
  X(P2288, 0x2288U, "Injector Ctrl Press Too High") \
  X(P2289, 0x2289U, "Injector Ctrl Press Too High-Engine Off") \
  X(P2290, 0x2290U, "Injector Ctrl Press Too Low") \
  X(P2291, 0x2291U, "Injector Ctrl Press Too Low-Engine Cranking") \
  X(P2292, 0x2292U, "Injector Ctrl Press Erratic") \
  X(P2293, 0x2293U, "Fuel Press Reg 2 Perf") \
  X(P2294, 0x2294U, "Fuel Press Reg 2 Ctrl Circ") \
  X(P2295, 0x2295U, "Fuel Press Reg 2 Ctrl Circ Low") \
  X(P2296, 0x2296U, "Fuel Press Reg 2 Ctrl Circ High") \
  X(P2297, 0x2297U, "O2 Sensor Out of Range During Deceleration Bank1 Sensor 1") \
  X(P2298, 0x2298U, "O2 Sensor Out of Range During Deceleration Bank2 Sensor 1") \
  X(P2299, 0x2299U, "Brake Pedal Pos / Accelerator Pedal Pos Incompatible") \
  X(P2300, 0x2300U, "Ignition Coil A Pri Ctrl Circ Low") \
  X(P2301, 0x2301U, "Ignition Coil A Pri Ctrl Circ High") \
  X(P2302, 0x2302U, "Ignition Coil A Sec Circ") \
  X(P2303, 0x2303U, "Ignition Coil B Pri Ctrl Circ Low") \
  X(P2304, 0x2304U, "Ignition Coil B Pri Ctrl Circ High") \
  X(P2305, 0x2305U, "Ignition Coil B Sec Circ") \
  X(P2306, 0x2306U, "Ignition Coil C Pri Ctrl Circ Low") \
  X(P2307, 0x2307U, "Ignition Coil C Pri Ctrl Circ High") \
  X(P2308, 0x2308U, "Ignition Coil C Sec Circ") \
  X(P2309, 0x2309U, "Ignition Coil D Pri Ctrl Circ Low") \
  X(P2310, 0x2310U, "Ignition Coil D Pri Ctrl Circ High") \
  X(P2311, 0x2311U, "Ignition Coil D Sec Circ") \
  X(P2312, 0x2312U, "Ignition Coil E Pri Ctrl Circ Low") \
  X(P2313, 0x2313U, "Ignition Coil E Pri Ctrl Circ High") \
  X(P2314, 0x2314U, "Ignition Coil E Sec Circ") \
  X(P2315, 0x2315U, "Ignition Coil F Pri Ctrl Circ Low") \
  X(P2316, 0x2316U, "Ignition Coil F Pri Ctrl Circ High") \
  X(P2317, 0x2317U, "Ignition Coil F Sec Circ") \
  X(P2318, 0x2318U, "Ignition Coil G Pri Ctrl Circ Low") \
  X(P2319, 0x2319U, "Ignition Coil G Pri Ctrl Circ High") \
  X(P2320, 0x2320U, "Ignition Coil G Sec Circ") \
  X(P2321, 0x2321U, "Ignition Coil H Pri Ctrl Circ Low") \
  X(P2322, 0x2322U, "Ignition Coil H Pri Ctrl Circ High") \
  X(P2323, 0x2323U, "Ignition Coil H Sec Circ") \
  X(P2324, 0x2324U, "Ignition Coil I Pri Ctrl Circ Low") \
  X(P2325, 0x2325U, "Ignition Coil I Pri Ctrl Circ High") \
  X(P2326, 0x2326U, "Ignition Coil I Sec Circ") \
  X(P2327, 0x2327U, "Ignition Coil J Pri Ctrl Circ Low") \
  X(P2328, 0x2328U, "Ignition Coil J Pri Ctrl Circ High") \
  X(P2329, 0x2329U, "Ignition Coil J Sec Circ") \
  X(P2330, 0x2330U, "Ignition Coil K Pri Ctrl Circ Low") \
  X(P2331, 0x2331U, "Ignition Coil K Pri Ctrl Circ High") \
  X(P2332, 0x2332U, "Ignition Coil K Sec Circ") \
  X(P2333, 0x2333U, "Ignition Coil L Pri Ctrl Circ Low") \
  X(P2334, 0x2334U, "Ignition Coil L Pri Ctrl Circ High") \
  X(P2335, 0x2335U, "Ignition Coil L Sec Circ") \
  X(P2336, 0x2336U, "Cylinder #1 Above Knock Threshold") \
  X(P2337, 0x2337U, "Cylinder #2 Above Knock Threshold") \
  X(P2338, 0x2338U, "Cylinder #3 Above Knock Threshold") \
  X(P2339, 0x2339U, "Cylinder #4 Above Knock Threshold") \
  X(P2340, 0x2340U, "Cylinder #5 Above Knock Threshold") \
  X(P2341, 0x2341U, "Cylinder #6 Above Knock Threshold") \
  X(P2342, 0x2342U, "Cylinder #7 Above Knock Threshold") \
  X(P2343, 0x2343U, "Cylinder #8 Above Knock Threshold") \
  X(P2344, 0x2344U, "Cylinder #9 Above Knock Threshold") \
  X(P2345, 0x2345U, "Cylinder #10 Above Knock Threshold") \
  X(P2346, 0x2346U, "Cylinder #11 Above Knock Threshold") \
  X(P2347, 0x2347U, "Cylinder #12 Above Knock Threshold") \
  X(P2400, 0x2400U, "EVAP Leak Detection Pump Ctrl Circ/Open") \
  X(P2401, 0x2401U, "EVAP Leak Detection Pump Ctrl Circ Low") \
  X(P2402, 0x2402U, "EVAP Leak Detection Pump Ctrl Circ High") \
  X(P2403, 0x2403U, "EVAP Leak Detection Pump Sense Circ/Open") \
  X(P2404, 0x2404U, "EVAP Leak Detection Pump Sense Circ Range/Perf") \
  X(P2405, 0x2405U, "EVAP Leak Detection Pump Sense Circ Low") \
  X(P2406, 0x2406U, "EVAP Leak Detection Pump Sense Circ High") \
  X(P2407, 0x2407U, "EVAP Leak Detection Pump Sense Circ Interm/Erratic") \
  X(P2408, 0x2408U, "Fuel Cap Sensor/Switch Circ") \
  X(P2409, 0x2409U, "Fuel Cap Sensor/Switch Circ Range/Perf") \
  X(P2410, 0x2410U, "Fuel Cap Sensor/Switch Circ Low") \
  X(P2411, 0x2411U, "Fuel Cap Sensor/Switch Circ High") \
  X(P2412, 0x2412U, "Fuel Cap Sensor/Switch Circ Interm/Erratic") \
  X(P2413, 0x2413U, "Exhaust Gas Recirculation Sys Perf") \
  X(P2414, 0x2414U, "O2 Sensor Exhaust Sample Error Bank1 Sensor 1") \
  X(P2415, 0x2415U, "O2 Sensor Exhaust Sample Error Bank2 Sensor 1") \
  X(P2416, 0x2416U, "O2 Sensor Signals Swapped Bank1 Sensor 2 / Bank1 Sensor 3") \
  X(P2417, 0x2417U, "O2 Sensor Signals Swapped Bank2 Sensor 2 / Bank2 Sensor 3") \
  X(P2418, 0x2418U, "EVAP Switching Valve Ctrl Circ /Open") \
  X(P2419, 0x2419U, "EVAP Switching Valve Ctrl Circ Low") \
  X(P2420, 0x2420U, "EVAP Switching Valve Ctrl Circ High") \
  X(P2421, 0x2421U, "EVAP Vent Valve Stuck Open") \
  X(P2422, 0x2422U, "EVAP Vent Valve Stuck Closed") \
  X(P2423, 0x2423U, "HC Adsorption Catalyst Efficiency Below Threshold Bank1") \
  X(P2424, 0x2424U, "HC Adsorption Catalyst Efficiency Below Threshold Bank2") \
  X(P2425, 0x2425U, "Exhaust Gas Recirculation Cooling Valve Ctrl Circ/Open") \
  X(P2426, 0x2426U, "Exhaust Gas Recirculation Cooling Valve Ctrl Circ Low") \
  X(P2427, 0x2427U, "Exhaust Gas Recirculation Cooling Valve Ctrl Circ High") \
  X(P2428, 0x2428U, "Exhaust Gas Temp Too High Bank1") \
  X(P2429, 0x2429U, "Exhaust Gas Temp Too High Bank2") \
  X(P2430, 0x2430U, "Sec Air Inj Sys Air Flow/Press Sensor Circ Bank1") \
  X(P2431, 0x2431U, "Sec Air Inj Sys Air Flow/Press Sensor Circ Range/Perf Bank1") \
  X(P2432, 0x2432U, "Sec Air Inj Sys Air Flow/Press Sensor Circ Low Bank1") \
  X(P2433, 0x2433U, "Sec Air Inj Sys Air Flow/Press Sensor Circ High Bank1") \
  X(P2434, 0x2434U, "Sec Air Inj Sys Air Flow/Press Sensor Circ Interm/Erratic Bank1") \
  X(P2435, 0x2435U, "Sec Air Inj Sys Air Flow/Press Sensor Circ Bank2") \
  X(P2436, 0x2436U, "Sec Air Inj Sys Air Flow/Press Sensor Circ Range/Perf Bank2") \
  X(P2437, 0x2437U, "Sec Air Inj Sys Air Flow/Press Sensor Circ Low Bank2") \
  X(P2438, 0x2438U, "Sec Air Inj Sys Air Flow/Press Sensor Circ High Bank2") \
  X(P2439, 0x2439U, "Sec Air Inj Sys Air Flow/Press Sensor Circ Interm/Eratic Bank2") \
  X(P2440, 0x2440U, "Sec Air Inj Sys Switching Valve Stuck Open Bank1") \
  X(P2441, 0x2441U, "Sec Air Inj Sys Switching Valve Stuck Closed Bank1") \
  X(P2442, 0x2442U, "Sec Air Inj Sys Switching Valve Stuck Open Bank2") \
  X(P2443, 0x2443U, "Sec Air Inj Sys Switching Valve Stuck Closed Bank2") \
  X(P2444, 0x2444U, "Sec Air Inj Sys Pump Stuck On Bank1") \
  X(P2445, 0x2445U, "Sec Air Inj Sys Pump Stuck Off Bank1") \
  X(P2446, 0x2446U, "Sec Air Inj Sys Pump Stuck On Bank2") \
  X(P2447, 0x2447U, "Sec Air Inj Sys Pump Stuck Off Bank2") \
  X(P2453, 0x2453U, "Particulate Matter Trap Differential Pressure Sensor Signal Performance") \
  X(P2455, 0x2455U, "DPF Differential Pressure Sensor Short to Voltage") \
  X(P2500, 0x2500U, "Generator Lamp/L-Terminal Circ Low") \
  X(P2501, 0x2501U, "Generator Lamp/L-Terminal Circ High") \
  X(P2502, 0x2502U, "Charging Sys Voltage") \
  X(P2503, 0x2503U, "Charging Sys Voltage Low") \
  X(P2504, 0x2504U, "Charging Sys Voltage High") \
  X(P2505, 0x2505U, "ECM/PCM Power Input Signal") \
  X(P2506, 0x2506U, "ECM/PCM Power Input Signal Range/Perf") \
  X(P2507, 0x2507U, "ECM/PCM Power Input Signal Low") \
  X(P2508, 0x2508U, "ECM/PCM Power Input Signal High") \
  X(P2509, 0x2509U, "ECM/PCM Power Input Signal Interm") \
  X(P2510, 0x2510U, "ECM/PCM Power Relay Sense Circ Range/Perf") \
  X(P2511, 0x2511U, "ECM/PCM Power Relay Sense Circ Interm") \
  X(P2512, 0x2512U, "Event Data Recorder Request Circ/ Open") \
  X(P2513, 0x2513U, "Event Data Recorder Request Circ Low") \
  X(P2514, 0x2514U, "Event Data Recorder Request Circ High") \
  X(P2515, 0x2515U, "A/C Refrigerant Press SensorB Circ") \
  X(P2516, 0x2516U, "A/C Refrigerant Press SensorB Circ Range/Perf") \
  X(P2517, 0x2517U, "A/C Refrigerant Press SensorB Circ Low") \
  X(P2518, 0x2518U, "A/C Refrigerant Press SensorB Circ High") \
  X(P2519, 0x2519U, "A/C Request A Circ") \
  X(P2520, 0x2520U, "A/C Request A Circ Low") \
  X(P2521, 0x2521U, "A/C Request A Circ High") \
  X(P2522, 0x2522U, "A/C Request B Circ") \
  X(P2523, 0x2523U, "A/C Request B Circ Low") \
  X(P2524, 0x2524U, "A/C Request B Circ High") \
  X(P2525, 0x2525U, "Vacuum Reservoir Press Sensor Circ") \
  X(P2526, 0x2526U, "Vacuum Reservoir Press Sensor Circ Range/Perf") \
  X(P2527, 0x2527U, "Vacuum Reservoir Press Sensor Circ Low") \
  X(P2528, 0x2528U, "Vacuum Reservoir Press Sensor Circ High") \
  X(P2529, 0x2529U, "Vacuum Reservoir Press Sensor Circ Interm") \
  X(P2530, 0x2530U, "Ignition Switch Run Pos Circ") \
  X(P2531, 0x2531U, "Ignition Switch Run Pos Circ Low") \
  X(P2532, 0x2532U, "Ignition Switch Run Pos Circ High") \
  X(P2533, 0x2533U, "Ignition Switch Run/Start Pos Circ") \
  X(P2534, 0x2534U, "Ignition Switch Run/Start Pos Circ Low") \
  X(P2535, 0x2535U, "Ignition Switch Run/Start Pos Circ High") \
  X(P2536, 0x2536U, "Ignition Switch Accessory Pos Circ") \
  X(P2537, 0x2537U, "Ignition Switch Accessory Pos Circ Low") \
  X(P2538, 0x2538U, "Ignition Switch Accessory Pos Circ High") \
  X(P2539, 0x2539U, "Low Press Fuel Sys Sensor Circ") \
  X(P2540, 0x2540U, "Low Press Fuel Sys Sensor Circ Range/Perf") \
  X(P2541, 0x2541U, "Low Press Fuel Sys Sensor Circ Low") \
  X(P2542, 0x2542U, "Low Press Fuel Sys Sensor Circ High") \
  X(P2543, 0x2543U, "Low Press Fuel Sys Sensor Circ Interm") \
  X(P2544, 0x2544U, "Torque Mgmt Request Input Signal A") \
  X(P2545, 0x2545U, "Torque Mgmt Request Input Signal A Range/Perf") \
  X(P2546, 0x2546U, "Torque Mgmt Request Input Signal A Low") \
  X(P2547, 0x2547U, "Torque Mgmt Request Input Signal A High") \
  X(P2548, 0x2548U, "Torque Mgmt Request Input Signal B") \
  X(P2549, 0x2549U, "Torque Mgmt Request Input Signal B Range/Perf") \
  X(P2550, 0x2550U, "Torque Mgmt Request Input Signal B Low") \
  X(P2551, 0x2551U, "Torque Mgmt Request Input Signal B High") \
  X(P2552, 0x2552U, "Throttle/Fuel Inhibit Circ") \
  X(P2553, 0x2553U, "Throttle/Fuel Inhibit Circ Range/Perf") \
  X(P2554, 0x2554U, "Throttle/Fuel Inhibit Circ Low") \
  X(P2555, 0x2555U, "Throttle/Fuel Inhibit Circ High") \
  X(P2556, 0x2556U, "Engine Coolant Level Sensor/Switch Circ") \
  X(P2557, 0x2557U, "Engine Coolant Level Sensor/Switch Circ Range/Perf") \
  X(P2558, 0x2558U, "Engine Coolant Level Sensor/Switch Circ Low") \
  X(P2559, 0x2559U, "Engine Coolant Level Sensor/Switch Circ High") \
  X(P2560, 0x2560U, "Engine Coolant Level Low") \
  X(P2561, 0x2561U, "A/C Ctrl Mod Requested MIL Illumination") \
  X(P2562, 0x2562U, "Turbocharger Boost Ctrl Pos Sensor Circ") \
  X(P2563, 0x2563U, "Turbocharger Boost Ctrl Pos Sensor Circ Range/Perf") \
  X(P2564, 0x2564U, "Turbocharger Boost Ctrl Pos Sensor Circ Low") \
  X(P2565, 0x2565U, "Turbocharger Boost Ctrl Pos Sensor Circ High") \
  X(P2566, 0x2566U, "Turbocharger Boost Ctrl Pos Sensor Circ Interm") \
  X(P2567, 0x2567U, "Direct Ozone Reduction Catalyst Temp Sensor Circ") \
  X(P2568, 0x2568U, "Direct Ozone Reduction Catalyst Temp Sensor Circ Range/Perf") \
  X(P2569, 0x2569U, "Direct Ozone Reduction Catalyst Temp Sensor Circ Low") \
  X(P2570, 0x2570U, "Direct Ozone Reduction Catalyst Temp Sensor Circ High") \
  X(P2571, 0x2571U, "Direct Ozone Reduction Catalyst Temp Sensor Circ Interm/Erratic4") \
  X(P2572, 0x2572U, "Direct Ozone Reduction Catalyst Deterioration Sensor Circ4") \
  X(P2573, 0x2573U, "Direct Ozone Reduction Catalyst Deterioration Sensor Circ Range/Perf4") \
  X(P2574, 0x2574U, "Direct Ozone Reduction Catalyst Deterioration Sensor Circ Low4") \
  X(P2575, 0x2575U, "Direct Ozone Reduction Catalyst Deterioration Sensor Circ High4") \
  X(P2576, 0x2576U, "Direct Ozone Reduction Catalyst Deterioration Sensor Circ Interm/Erratic") \
  X(P2577, 0x2577U, "Direct Ozone Reduction Catalyst Efficiency Below Threshold") \
  X(P2600, 0x2600U, "Coolant Pump Ctrl Circ/Open") \
  X(P2601, 0x2601U, "Coolant Pump Ctrl Circ Range/Perf") \
  X(P2602, 0x2602U, "Coolant Pump Ctrl Circ Low") \
  X(P2603, 0x2603U, "Coolant Pump Ctrl Circ High") \
  X(P2604, 0x2604U, "Intake Air Heater A Circ Range/Perf") \
  X(P2605, 0x2605U, "Intake Air Heater A Circ/Open") \
  X(P2606, 0x2606U, "Intake Air Heater B Circ Range/Perf") \
  X(P2607, 0x2607U, "Intake Air Heater B Circ Low") \
  X(P2608, 0x2608U, "Intake Air Heater B Circ High") \
  X(P2609, 0x2609U, "Intake Air Heater Sys Perf") \
  X(P2610, 0x2610U, "ECM/PCM Internal Engine Off Timer Perf") \
  X(P2611, 0x2611U, "A/C Refrigerant Distribution Valve Ctrl Circ/Open") \
  X(P2612, 0x2612U, "A/C Refrigerant Distribution Valve Ctrl Circ Low") \
  X(P2613, 0x2613U, "A/C Refrigerant Distribution Valve Ctrl Circ High") \
  X(P2614, 0x2614U, "Camshaft Pos Signal Output Circ/Open") \
  X(P2615, 0x2615U, "Camshaft Pos Signal Output Circ Low") \
  X(P2616, 0x2616U, "Camshaft Pos Signal Output Circ High") \
  X(P2617, 0x2617U, "Crank Pos Signal Output Circ/Open") \
  X(P2618, 0x2618U, "Crank Pos Signal Output Circ Low") \
  X(P2619, 0x2619U, "Crank Pos Signal Output Circ High") \
  X(P2620, 0x2620U, "Throttle Pos Output Circ/Open") \
  X(P2621, 0x2621U, "Throttle Pos Output Circ Low") \
  X(P2622, 0x2622U, "Throttle Pos Output Circ High") \
  X(P2623, 0x2623U, "Injector Ctrl Press Reg Circ/Open") \
  X(P2624, 0x2624U, "Injector Ctrl Press Reg Circ Low") \
  X(P2625, 0x2625U, "Injector Ctrl Press Reg Circ High") \
  X(P2626, 0x2626U, "O2 Sensor Pumping Current Trim Circ/Open Bank1 Sensor 1") \
  X(P2627, 0x2627U, "O2 Sensor Pumping Current Trim Circ Low Bank1 Sensor 1") \
  X(P2628, 0x2628U, "O2 Sensor Pumping Current Trim Circ High Bank1 Sensor 1") \
  X(P2629, 0x2629U, "O2 Sensor Pumping Current Trim Circ/Open Bank2 Sensor 1") \
  X(P2630, 0x2630U, "O2 Sensor Pumping Current Trim Circ Low Bank2 Sensor 1") \
  X(P2631, 0x2631U, "O2 Sensor Pumping Current Trim Circ High Bank2 Sensor 1") \
  X(P2632, 0x2632U, "Fuel Pump B Ctrl Circ /Open") \
  X(P2633, 0x2633U, "Fuel Pump B Ctrl Circ Low") \
  X(P2634, 0x2634U, "Fuel Pump B Ctrl Circ High") \
  X(P2635, 0x2635U, "Fuel Pump A Low Flow / Perf") \
  X(P2636, 0x2636U, "Fuel Pump B Low Flow / Perf") \
  X(P2637, 0x2637U, "Torque Mgmt Feedback Signal A") \
  X(P2638, 0x2638U, "Torque Mgmt Feedback Signal A Range/Perf") \
  X(P2639, 0x2639U, "Torque Mgmt Feedback Signal A Low") \
  X(P2640, 0x2640U, "Torque Mgmt Feedback Signal A High") \
  X(P2641, 0x2641U, "Torque Mgmt Feedback Signal B") \
  X(P2642, 0x2642U, "Torque Mgmt Feedback Signal B Range/Perf") \
  X(P2643, 0x2643U, "Torque Mgmt Feedback Signal B Low") \
  X(P2644, 0x2644U, "Torque Mgmt Feedback Signal B High") \
  X(P2645, 0x2645U, "A Rocker Arm Actuator Ctrl Circ/Open Bank1") \
  X(P2646, 0x2646U, "A Rocker Arm Actuator Sys Perf or Stuck Off Bank1") \
  X(P2647, 0x2647U, "A Rocker Arm Actuator Sys Stuck On Bank1") \
  X(P2648, 0x2648U, "A Rocker Arm Actuator Ctrl Circ Low Bank1") \
  X(P2649, 0x2649U, "A Rocker Arm Actuator Ctrl Circ High Bank1") \
  X(P2650, 0x2650U, "B Rocker Arm Actuator Ctrl Circ/Open Bank1") \
  X(P2651, 0x2651U, "B Rocker Arm Actuator Sys Perf or Stuck Off Bank1") \
  X(P2652, 0x2652U, "B Rocker Arm Actuator Sys Stuck On Bank1") \
  X(P2653, 0x2653U, "B Rocker Arm Actuator Ctrl Circ Low Bank1") \
  X(P2654, 0x2654U, "B Rocker Arm Actuator Ctrl Circ High Bank1") \
  X(P2655, 0x2655U, "A Rocker Arm Actuator Ctrl Circ/Open Bank2") \
  X(P2656, 0x2656U, "A Rocker Arm Actuator Sys Perf or Stuck Off Bank2") \
  X(P2657, 0x2657U, "A Rocker Arm Actuator Sys Stuck On Bank2") \
  X(P2658, 0x2658U, "A Rocker Arm Actuator Ctrl Circ Low Bank2") \
  X(P2659, 0x2659U, "A Rocker Arm Actuator Ctrl Circ High Bank2") \
  X(P2660, 0x2660U, "B Rocker Arm Actuator Ctrl Circ/Open Bank2") \
  X(P2661, 0x2661U, "B Rocker Arm Actuator Sys Perf or Stuck Off Bank2") \
  X(P2662, 0x2662U, "B Rocker Arm Actuator Sys Stuck On Bank2") \
  X(P2663, 0x2663U, "B Rocker Arm Actuator Ctrl Circ Low Bank2") \
  X(P2664, 0x2664U, "B Rocker Arm Actuator Ctrl Circ High Bank2") \
  X(P2665, 0x2665U, "Fuel Shutoff Valve B Ctrl Circ/Open") \
  X(P2666, 0x2666U, "Fuel Shutoff Valve B Ctrl Circ Low") \
  X(P2667, 0x2667U, "Fuel Shutoff Valve B Ctrl Circ High") \
  X(P2668, 0x2668U, "Fuel Mode Indicator Lamp Ctrl Circ") \
  X(P2669, 0x2669U, "Actuator Supply Voltage B Circ /Open") \
  X(P2670, 0x2670U, "Actuator Supply Voltage B Circ Low") \
  X(P2671, 0x2671U, "Actuator Supply Voltage B Circ High") \
  X(P2700, 0x2700U, "Trans Friction Element A Apply Time Range/Perf") \
  X(P2701, 0x2701U, "Trans Friction Element B Apply Time Range/Perf") \
  X(P2702, 0x2702U, "Trans Friction Element C Apply Time Range/Perf") \
  X(P2703, 0x2703U, "Trans Friction Element D Apply Time Range/Perf") \
  X(P2704, 0x2704U, "Trans Friction Element E Apply Time Range/Perf") \
  X(P2705, 0x2705U, "Trans Friction Element F Apply Time Range/Perf") \
  X(P2706, 0x2706U, "Shift Solenoid F") \
  X(P2707, 0x2707U, "Shift Solenoid F Perf or Stuck Off") \
  X(P2708, 0x2708U, "Shift Solenoid F Stuck On") \
  X(P2709, 0x2709U, "Shift Solenoid F Electrical") \
  X(P2710, 0x2710U, "Shift Solenoid F Interm") \
  X(P2711, 0x2711U, "Unexpected Mechanical Gear Disengagement") \
  X(P2712, 0x2712U, "Hydraulic Power Unit Leakage") \
  X(P2713, 0x2713U, "Press Ctrl Solenoid D") \
  X(P2714, 0x2714U, "Press Ctrl Solenoid D Perf or Stuck Off") \
  X(P2715, 0x2715U, "Press Ctrl Solenoid D Stuck On") \
  X(P2716, 0x2716U, "Press Ctrl Solenoid D Electrical") \
  X(P2717, 0x2717U, "Press Ctrl Solenoid D Interm") \
  X(P2718, 0x2718U, "Press Ctrl Solenoid D Ctrl Circ / Open") \
  X(P2719, 0x2719U, "Press Ctrl Solenoid D Ctrl Circ Range/Perf") \
  X(P2720, 0x2720U, "Press Ctrl Solenoid D Ctrl Circ Low") \
  X(P2721, 0x2721U, "Press Ctrl Solenoid D Ctrl Circ High") \
  X(P2722, 0x2722U, "Press Ctrl Solenoid E") \
  X(P2723, 0x2723U, "Press Ctrl Solenoid E Perf or Stuck Off") \
  X(P2724, 0x2724U, "Press Ctrl Solenoid E Stuck On") \
  X(P2725, 0x2725U, "Press Ctrl Solenoid E Electrical") \
  X(P2726, 0x2726U, "Press Ctrl Solenoid E Interm") \
  X(P2727, 0x2727U, "Press Ctrl Solenoid E Ctrl Circ / Open") \
  X(P2728, 0x2728U, "Press Ctrl Solenoid E Ctrl Circ Range/Perf") \
  X(P2729, 0x2729U, "Press Ctrl Solenoid E Ctrl Circ Low") \
  X(P2730, 0x2730U, "Press Ctrl Solenoid E Ctrl Circ High") \
  X(P2731, 0x2731U, "Press Ctrl Solenoid F") \
  X(P2732, 0x2732U, "Press Ctrl Solenoid F Perf or Stuck Off") \
  X(P2733, 0x2733U, "Press Ctrl Solenoid F Stuck On") \
  X(P2734, 0x2734U, "Press Ctrl Solenoid F Electrical") \
  X(P2735, 0x2735U, "Press Ctrl Solenoid F Interm") \
  X(P2736, 0x2736U, "Press Ctrl Solenoid F Ctrl Circ/Open") \
  X(P2737, 0x2737U, "Press Ctrl Solenoid F Ctrl Circ Range/Perf") \
  X(P2738, 0x2738U, "Press Ctrl Solenoid F Ctrl Circ Low") \
  X(P2739, 0x2739U, "Press Ctrl Solenoid F Ctrl Circ High") \
  X(P2740, 0x2740U, "Trans Fluid Temp SensorB Circ") \
  X(P2741, 0x2741U, "Trans Fluid Temp SensorB Circ Range Perf") \
  X(P2742, 0x2742U, "Trans Fluid Temp SensorB Circ Low") \
  X(P2743, 0x2743U, "Trans Fluid Temp SensorB Circ High") \
  X(P2744, 0x2744U, "Trans Fluid Temp SensorB Circ Interm") \
  X(P2745, 0x2745U, "Intermediate Shaft Speed SensorB Circ") \
  X(P2746, 0x2746U, "Intermediate Shaft Speed SensorB Circ Range/Perf") \
  X(P2747, 0x2747U, "Intermediate Shaft Speed SensorB Circ No Signal") \
  X(P2748, 0x2748U, "Intermediate Shaft Speed SensorB Circ Interm") \
  X(P2749, 0x2749U, "Intermediate Shaft Speed Sensor C Circ") \
  X(P2750, 0x2750U, "Intermediate Shaft Speed Sensor C Circ Range/Perf") \
  X(P2751, 0x2751U, "Intermediate Shaft Speed Sensor C Circ No Signal") \
  X(P2752, 0x2752U, "Intermediate Shaft Speed Sensor C Circ Interm") \
  X(P2753, 0x2753U, "Trans Fluid Cooler Ctrl Circ/Open") \
  X(P2754, 0x2754U, "Trans Fluid Cooler Ctrl Circ Low") \
  X(P2755, 0x2755U, "Trans Fluid Cooler Ctrl Circ High") \
  X(P2756, 0x2756U, "Torq Conv Clutch Press Ctrl Solenoid") \
  X(P2757, 0x2757U, "Torq Conv Clutch Press Ctrl Solenoid Ctrl Circ Perf or Stuck Off") \
  X(P2758, 0x2758U, "Torq Conv Clutch Press Ctrl Solenoid Ctrl Circ Stuck On") \
  X(P2759, 0x2759U, "Torq Conv Clutch Press Ctrl Solenoid Ctrl Circ Electrical") \
  X(P2760, 0x2760U, "Torq Conv Clutch Press Ctrl Solenoid Ctrl Circ Interm") \
  X(P2761, 0x2761U, "Torq Conv Clutch Press Ctrl Solenoid Ctrl Circ/Open") \
  X(P2762, 0x2762U, "Torq Conv Clutch Press Ctrl Solenoid Ctrl Circ Range/Perf") \
  X(P2763, 0x2763U, "Torq Conv Clutch Press Ctrl Solenoid Ctrl Circ High") \
  X(P2764, 0x2764U, "Torq Conv Clutch Press Ctrl Solenoid Ctrl Circ Low") \
  X(P2765, 0x2765U, "Input/Turbine Speed SensorB Circ") \
  X(P2766, 0x2766U, "Input/Turbine Speed SensorB Circ Range/Perf") \
  X(P2767, 0x2767U, "Input/Turbine Speed SensorB Circ No Signal") \
  X(P2768, 0x2768U, "Input/Turbine Speed SensorB Circ Interm") \
  X(P2769, 0x2769U, "Torq Conv Clutch Circ Low") \
  X(P2770, 0x2770U, "Torq Conv Clutch Circ High") \
  X(P2771, 0x2771U, "4WD Low Switch Circ") \
  X(P2772, 0x2772U, "4WD Low Switch Circ Range/Perf") \
  X(P2773, 0x2773U, "4WD Low Switch Circ Low") \
  X(P2774, 0x2774U, "4WD Low Switch Circ High") \
  X(P2775, 0x2775U, "Upshift Switch Circ Range/Perf") \
  X(P2776, 0x2776U, "Upshift Switch Circ Low") \
  X(P2777, 0x2777U, "Upshift Switch Circ High") \
  X(P2778, 0x2778U, "Upshift Switch Circ Interm/Erratic") \
  X(P2779, 0x2779U, "Downshift Switch Circ Range/Perf") \
  X(P2780, 0x2780U, "Downshift Switch Circ Low") \
  X(P2781, 0x2781U, "Downshift Switch Circ High") \
  X(P2782, 0x2782U, "Downshift Switch Circ Interm/Erratic") \
  X(P2783, 0x2783U, "Torq Conv Temp Too High") \
  X(P2784, 0x2784U, "Input/Turbine Speed SensorA/B Correlation") \
  X(P2785, 0x2785U, "Clutch Actuator Temp Too High") \
  X(P2786, 0x2786U, "Gear Shift Actuator Temp Too High") \
  X(P2787, 0x2787U, "Clutch Temp Too High") \
  X(P2788, 0x2788U, "Auto Shift Manual Adaptive Learning at Limit") \
  X(P2789, 0x2789U, "Clutch Adaptive Learning at Limit") \
  X(P2790, 0x2790U, "Gate Select Direction Circ") \
  X(P2791, 0x2791U, "Gate Select Direction Circ Low") \
  X(P2792, 0x2792U, "Gate Select Direction Circ High") \
  X(P2793, 0x2793U, "Gear Shift Direction Circ") \
  X(P2794, 0x2794U, "Gear Shift Direction Circ Low") \
  X(P2795, 0x2795U, "Gear Shift Direction Circ High") \
  X(P2A00, 0x2A00U, "O2 Sensor Circ Range/Perf Bank1 Sensor 1") \
  X(P2A01, 0x2A01U, "O2 Sensor Circ Range/Perf Bank1 Sensor 2") \
  X(P2A02, 0x2A02U, "O2 Sensor Circ Range/Perf Bank1 Sensor 3") \
  X(P2A03, 0x2A03U, "O2 Sensor Circ Range/Perf Bank2 Sensor 1") \
  X(P2A04, 0x2A04U, "O2 Sensor Circ Range/Perf Bank2 Sensor 2") \
  X(P2A05, 0x2A05U, "O2 Sensor Circ Range/Perf Bank2 Sensor 3") \
  X(P3400, 0x3400U, "Cylinder Deactivation Sys Bank1") \
  X(P3401, 0x3401U, "Cyl1 Deactivation/Intake Valve Ctrl Circ/Open") \
  X(P3402, 0x3402U, "Cyl1 Deactivation/Intake Valve Ctrl Perf") \
  X(P3403, 0x3403U, "Cyl1 Deactivation/Intake Valve Ctrl Circ Low") \
  X(P3404, 0x3404U, "Cyl1 Deactivation/Intake Valve Ctrl Circ High") \
  X(P3405, 0x3405U, "Cyl1 Exhaust Valve Ctrl Circ/Open") \
  X(P3406, 0x3406U, "Cyl1 Exhaust Valve Ctrl Perf") \
  X(P3407, 0x3407U, "Cyl1 Exhaust Valve Ctrl Circ Low") \
  X(P3408, 0x3408U, "Cyl1 Exhaust Valve Ctrl Circ High") \
  X(P3409, 0x3409U, "Cyl2 Deactivation/Intake Valve Ctrl Circ/Open") \
  X(P3410, 0x3410U, "Cyl2 Deactivation/Intake Valve Ctrl Perf") \
  X(P3411, 0x3411U, "Cyl2 Deactivation/Intake Valve Ctrl Circ Low") \
  X(P3412, 0x3412U, "Cyl2 Deactivation/Intake Valve Ctrl Circ High") \
  X(P3413, 0x3413U, "Cyl2 Exhaust Valve Ctrl Circ/Open") \
  X(P3414, 0x3414U, "Cyl2 Exhaust Valve Ctrl Perf") \
  X(P3415, 0x3415U, "Cyl2 Exhaust Valve Ctrl Circ Low") \
  X(P3416, 0x3416U, "Cyl2 Exhaust Valve Ctrl Circ High") \
  X(P3417, 0x3417U, "Cyl3 Deactivation/Intake Valve Ctrl Circ/Open") \
  X(P3418, 0x3418U, "Cyl3 Deactivation/Intake Valve Ctrl Perf") \
  X(P3419, 0x3419U, "Cyl3 Deactivation/Intake Valve Ctrl Circ Low") \
  X(P3420, 0x3420U, "Cyl3 Deactivation/Intake Valve Ctrl Circ High") \
  X(P3421, 0x3421U, "Cyl3 Exhaust Valve Ctrl Circ/Open") \
  X(P3422, 0x3422U, "Cyl3 Exhaust Valve Ctrl Perf") \
  X(P3423, 0x3423U, "Cyl3 Exhaust Valve Ctrl Circ Low") \
  X(P3424, 0x3424U, "Cyl3 Exhaust Valve Ctrl Circ High") \
  X(P3425, 0x3425U, "Cyl4 Deactivation/Intake Valve Ctrl Circ/Open") \
  X(P3426, 0x3426U, "Cyl4 Deactivation/Intake Valve Ctrl Perf") \
  X(P3427, 0x3427U, "Cyl4 Deactivation/Intake Valve Ctrl Circ Low") \
  X(P3428, 0x3428U, "Cyl4 Deactivation/Intake Valve Ctrl Circ High") \
  X(P3429, 0x3429U, "Cyl4 Exhaust Valve Ctrl Circ/Open") \
  X(P3430, 0x3430U, "Cyl4 Exhaust Valve Ctrl Perf") \
  X(P3431, 0x3431U, "Cyl4 Exhaust Valve Ctrl Circ Low") \
  X(P3432, 0x3432U, "Cyl4 Exhaust Valve Ctrl Circ High") \
  X(P3433, 0x3433U, "Cyl5 Deactivation/Intake Valve Ctrl Circ/Open") \
  X(P3434, 0x3434U, "Cyl5 Deactivation/Intake Valve Ctrl Perf") \
  X(P3435, 0x3435U, "Cyl5 Deactivation/Intake Valve Ctrl Circ Low") \
  X(P3436, 0x3436U, "Cyl5 Deactivation/Intake Valve Ctrl Circ High") \
  X(P3437, 0x3437U, "Cyl5 Exhaust Valve Ctrl Circ/Open") \
  X(P3438, 0x3438U, "Cyl5 Exhaust Valve Ctrl Perf") \
  X(P3439, 0x3439U, "Cyl5 Exhaust Valve Ctrl Circ Low") \
  X(P3440, 0x3440U, "Cyl5 Exhaust Valve Ctrl Circ High") \
  X(P3441, 0x3441U, "Cyl6 Deactivation/Intake Valve Ctrl Circ/Open") \
  X(P3442, 0x3442U, "Cyl6 Deactivation/Intake Valve Ctrl Perf") \
  X(P3443, 0x3443U, "Cyl6 Deactivation/Intake Valve Ctrl Circ Low") \
  X(P3444, 0x3444U, "Cyl6 Deactivation/Intake Valve Ctrl Circ High") \
  X(P3445, 0x3445U, "Cyl6 Exhaust Valve Ctrl Circ/Open") \
  X(P3446, 0x3446U, "Cyl6 Exhaust Valve Ctrl Perf") \
  X(P3447, 0x3447U, "Cyl6 Exhaust Valve Ctrl Circ Low") \
  X(P3448, 0x3448U, "Cyl6 Exhaust Valve Ctrl Circ High") \
  X(P3449, 0x3449U, "Cyl7 Deactivation/Intake Valve Ctrl Circ/Open") \
  X(P3450, 0x3450U, "Cyl7 Deactivation/Intake Valve Ctrl Perf") \
  X(P3451, 0x3451U, "Cyl7 Deactivation/Intake Valve Ctrl Circ Low") \
  X(P3452, 0x3452U, "Cyl7 Deactivation/Intake Valve Ctrl Circ High") \
  X(P3453, 0x3453U, "Cyl7 Exhaust Valve Ctrl Circ/Open") \
  X(P3454, 0x3454U, "Cyl7 Exhaust Valve Ctrl Perf") \
  X(P3455, 0x3455U, "Cyl7 Exhaust Valve Ctrl Circ Low") \
  X(P3456, 0x3456U, "Cyl7 Exhaust Valve Ctrl Circ High") \
  X(P3457, 0x3457U, "Cyl8 Deactivation/Intake Valve Ctrl Circ/Open") \
  X(P3458, 0x3458U, "Cyl8 Deactivation/Intake Valve Ctrl Perf") \
  X(P3459, 0x3459U, "Cyl8 Deactivation/Intake Valve Ctrl Circ Low") \
  X(P3460, 0x3460U, "Cyl8 Deactivation/Intake Valve Ctrl Circ High") \
  X(P3461, 0x3461U, "Cyl8 Exhaust Valve Ctrl Circ/Open") \
  X(P3462, 0x3462U, "Cyl8 Exhaust Valve Ctrl Perf") \
  X(P3463, 0x3463U, "Cyl8 Exhaust Valve Ctrl Circ Low") \
  X(P3464, 0x3464U, "Cyl8 Exhaust Valve Ctrl Circ High") \
  X(P3465, 0x3465U, "Cyl9 Deactivation/Intake Valve Ctrl Circ/Open") \
  X(P3466, 0x3466U, "Cyl9 Deactivation/Intake Valve Ctrl Perf") \
  X(P3467, 0x3467U, "Cyl9 Deactivation/Intake Valve Ctrl Circ Low") \
  X(P3468, 0x3468U, "Cyl9 Deactivation/Intake Valve Ctrl Circ High") \
  X(P3469, 0x3469U, "Cyl9 Exhaust Valve Ctrl Circ/Open") \
  X(P3470, 0x3470U, "Cyl9 Exhaust Valve Ctrl Perf") \
  X(P3471, 0x3471U, "Cyl9 Exhaust Valve Ctrl Circ Low") \
  X(P3472, 0x3472U, "Cyl9 Exhaust Valve Ctrl Circ High") \
  X(P3473, 0x3473U, "Cyl10 Deactivation/Intake Valve Ctrl Circ/Open") \
  X(P3474, 0x3474U, "Cyl10 Deactivation/Intake Valve Ctrl Perf") \
  X(P3475, 0x3475U, "Cyl10 Deactivation/Intake Valve Ctrl Circ Low") \
  X(P3476, 0x3476U, "Cyl10 Deactivation/Intake Valve Ctrl Circ High") \
  X(P3477, 0x3477U, "Cyl10 Exhaust Valve Ctrl Circ/Open") \
  X(P3478, 0x3478U, "Cyl10 Exhaust Valve Ctrl Perf") \
  X(P3479, 0x3479U, "Cyl10 Exhaust Valve Ctrl Circ Low") \
  X(P3480, 0x3480U, "Cyl10 Exhaust Valve Ctrl Circ High") \
  X(P3481, 0x3481U, "Cyl11 Deactivation/Intake Valve Ctrl Circ/Open") \
  X(P3482, 0x3482U, "Cyl11 Deactivation/Intake Valve Ctrl Perf") \
  X(P3483, 0x3483U, "Cyl11 Deactivation/Intake Valve Ctrl Circ Low") \
  X(P3484, 0x3484U, "Cyl11 Deactivation/Intake Valve Ctrl Circ High") \
  X(P3485, 0x3485U, "Cyl11 Exhaust Valve Ctrl Circ/Open") \
  X(P3486, 0x3486U, "Cyl11 Exhaust Valve Ctrl Perf") \
  X(P3487, 0x3487U, "Cyl11 Exhaust Valve Ctrl Circ Low") \
  X(P3488, 0x3488U, "Cyl11 Exhaust Valve Ctrl Circ High") \
  X(P3489, 0x3489U, "Cyl12 Deactivation/Intake Valve Ctrl Circ/Open") \
  X(P3490, 0x3490U, "Cyl12 Deactivation/Intake Valve Ctrl Perf") \
  X(P3491, 0x3491U, "Cyl12 Deactivation/Intake Valve Ctrl Circ Low") \
  X(P3492, 0x3492U, "Cyl12 Deactivation/Intake Valve Ctrl Circ High") \
  X(P3493, 0x3493U, "Cyl12 Exhaust Valve Ctrl Circ/Open") \
  X(U0073, 0xC073U, "Control Module Communication Bus \"A\" Off") \
  X(U0100, 0xC100U, "Lost Communication With ECM/PCM \"A\"")

/** @brief Project codes in the manufacturer range, ascending by value. */
#define OBD_DTC_FIESTA_CATALOG(X) \
  X(U1900, 0xD900U, "Network CAN communication fault") \
  X(U1902, 0xD902U, "Lost communication with EGT module") \
  X(U1903, 0xD903U, "CAN0 bus init failure") \
  X(U1906, 0xD906U, "ISO-TP flow-control timeout") \
  X(U1907, 0xD907U, "ISO-TP flow-control abort from tester") \
  X(U1908, 0xD908U, "Lost communication with Adjustometer module") \
  X(U1909, 0xD909U, "Adjustometer oscillator signal lost") \
  X(U190A, 0xD90AU, "Adjustometer fuel temperature sensor fault") \
  X(U190B, 0xD90BU, "Adjustometer supply voltage out of range") \
  X(U190C, 0xD90CU, "RPM interrupt core-affinity/init failure") \
  X(U190D, 0xD90DU, "PCF8574 I/O expander communication fault")
/* clang-format on */

/** @brief Every code of both lists, in table order. */
#define OBD_DTC_CATALOG(X)                                                     \
  OBD_DTC_SAE_CATALOG(X)                                                       \
  OBD_DTC_FIESTA_CATALOG(X)

#define OBD_DTC_ENUM_ENTRY(name, value, description) OBD_DTC_##name = (value),

/** @brief The codes as constants: `OBD_DTC_P0116`, `OBD_DTC_U190C`. */
typedef enum { OBD_DTC_CATALOG(OBD_DTC_ENUM_ENTRY) } obd_dtc_code_t;

#undef OBD_DTC_ENUM_ENTRY

/**
 * @brief Number of rows in the catalogue.
 * @return Count of both lists together.
 */
size_t obdDtcCatalogCount(void);

/**
 * @brief Catalogue row by position.
 * @param index Position from 0 to `obdDtcCatalogCount() - 1`.
 * @return The row, or NULL when @p index is out of range.
 */
const obd_dtc_entry_t *obdDtcCatalogEntry(size_t index);

/**
 * @brief Description of a code.
 * @param code Two-byte SAE J2012 representation.
 * @return The description, or NULL when the catalogue has no such code.
 */
const char *obdDtcDescription(uint16_t code);

/**
 * @brief Print a code the way a scan tool shows it: `P0116`, `U190C`.
 * @param code Two-byte SAE J2012 representation.
 * @param buffer Output, at least `OBD_DTC_TEXT_SIZE` bytes; receives the
 * five characters and the terminator.
 * @param size Capacity of @p buffer in bytes.
 * @return HAL_OK, or HAL_EINVAL when @p buffer is NULL or @p size is below
 * `OBD_DTC_TEXT_SIZE`; the buffer is then left unchanged.
 */
hal_status_t obdDtcFormatCode(uint16_t code, char *buffer, size_t size);

#ifdef __cplusplus
}
#endif

#endif
