
#ifndef ENGINE_MAPS
#define ENGINE_MAPS

#include <JaszczurHAL.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file engineMaps.h
 * @brief The tables the ECU shapes its outputs with, values only. Each
 * consumer owns the meaning of its columns and the interpolation; the numbers
 * live here so a calibration can change in one spot and a host test can check
 * the shape of every table. The VP37 tables live in vp37_maps.h.
 */

// ── N75 duty against engine speed and pedal position ─────────────────────────
#define RPM_PRESCALERS 8
#define N75_PERCENT_VALS 10

/** @brief N75 duty [%] per RPM band (rows, from 1500 in steps of 500) and
 * pedal position (columns). */
extern const int32_t RPM_table[RPM_PRESCALERS][N75_PERCENT_VALS];

#ifdef __cplusplus
}
#endif

#endif
