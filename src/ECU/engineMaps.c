
#include "engineMaps.h"

//*** n75 percentage values in relation to RPM
const int32_t RPM_table[RPM_PRESCALERS][N75_PERCENT_VALS] = {
    {75, 74, 73, 72, 71, 70, 68, 65, 63, 61}, // 1500 RPM
    {73, 72, 71, 70, 68, 66, 63, 60, 57, 54}, // 2000 RPM
    {71, 70, 69, 68, 66, 64, 61, 58, 54, 51}, // 2500 RPM
    {70, 69, 68, 66, 64, 62, 59, 56, 53, 50}, // 3000 RPM
    {69, 68, 66, 64, 62, 60, 57, 54, 51, 48}, // 3500 RPM
    {67, 66, 64, 62, 60, 58, 55, 52, 49, 46}, // 4000 RPM
    {63, 62, 60, 58, 56, 54, 51, 48, 45, 42}, // 4500 RPM
    {60, 58, 56, 54, 52, 50, 47, 44, 41, 38}  // 5000 RPM
};

// VP37 holding map at 12 V, 49 C, taken from settled holds at 130 Hz (bench
// 2026-09-16, two series, both approach directions averaged; the upper stroke
// scatters about 25 counts between them). Those holds ran with the drive
// reference four percent high, so every entry carries the same 1.20/1.24
// rescale that re-measuring the reference called for; the commands it
// produces are the ones the holds measured. Columns: demand [%], holding
// command [nominal PWM], upward-motion correction at the reference rate
// [nominal PWM].
const float VP37_FF_MAP[VP37_FF_KNOTS][VP37_FF_COLUMNS] = {
    {0.0f, 566.0f, 0.0f},    // rest
    {5.0f, 585.0f, 9.7f},    //
    {10.0f, 603.0f, 11.6f},  //
    {25.0f, 676.0f, 14.5f},  //
    {50.0f, 768.0f, 19.4f},  //
    {75.0f, 828.0f, 21.3f},  //
    {90.0f, 838.0f, 31.0f},  // upper stroke: the motion scale
    {95.0f, 844.0f, 31.0f},  //
    {100.0f, 844.0f, 31.0f}, // full demand
};

// Integral authority above the holding map [nominal PWM]: full below the taper
// start, tapered near the upper endpoint where the stroke loses position
// authority. Columns: demand [%], limit.
const float VP37_INTEGRAL_LIMIT_MAP[VP37_STROKE_TAPER_KNOTS]
                                   [VP37_STROKE_TAPER_COLUMNS] = {
                                       {0.0f, 120.0f},  // full authority
                                       {75.0f, 120.0f}, // taper start
                                       {100.0f, 45.0f}, // full demand
};

// Integration dead zone [Hz]: above the taper start the same command settles
// hundreds of hertz apart and the same current holds very different
// positions, so the zone widens to a band that covers the insensitive range.
// Columns: demand [%], zone.
const float
    VP37_INTEGRAL_DEADBAND_MAP[VP37_STROKE_TAPER_KNOTS]
                              [VP37_STROKE_TAPER_COLUMNS] = {
                                  {0.0f, 12.0f},  // base
                                  {75.0f, 12.0f}, // taper start
                                  {100.0f,
                                   120.0f}, // full demand, the default top
};
