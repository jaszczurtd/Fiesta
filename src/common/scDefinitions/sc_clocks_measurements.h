#pragma once

/* SC_GET_VALUES identifiers and validity bits shared by Clocks and the host.
 * Invalid readings carry zero; validity distinguishes a valid zero. */
#define SC_CLOCKS_ABS_ID "abs_kmh"
#define SC_CLOCKS_OIL_ID "oil_cbar"
#define SC_CLOCKS_EGT0_ID "egt0_c"
#define SC_CLOCKS_EGT1_ID "egt1_c"
#define SC_CLOCKS_VALID_ID "valid"
#define SC_CLOCKS_VALID_ABS 1u
#define SC_CLOCKS_VALID_OIL 2u
#define SC_CLOCKS_VALID_EGT0 4u
#define SC_CLOCKS_VALID_EGT1 8u
#define SC_CLOCKS_VALID_ALL                                                    \
  (SC_CLOCKS_VALID_ABS | SC_CLOCKS_VALID_OIL | SC_CLOCKS_VALID_EGT0 |          \
   SC_CLOCKS_VALID_EGT1)

/* X(identifier, validity bit, displayed unit, wire units per display unit). */
#define SC_CLOCKS_MEASUREMENTS(X)                                              \
  X(SC_CLOCKS_ABS_ID, SC_CLOCKS_VALID_ABS, "km/h", 1)                          \
  X(SC_CLOCKS_OIL_ID, SC_CLOCKS_VALID_OIL, "bar", 100)                         \
  X(SC_CLOCKS_EGT0_ID, SC_CLOCKS_VALID_EGT0, "°C", 1)                          \
  X(SC_CLOCKS_EGT1_ID, SC_CLOCKS_VALID_EGT1, "°C", 1)
