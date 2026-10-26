#ifndef C_CLOCKS_STATE
#define C_CLOCKS_STATE

#include "../common/canDefinitions/canDefinitions.h"

#include <stdbool.h>
#include <stdint.h>

// Core 0 decodes the ECU frames, measures the ABS speed and publishes both
// as one snapshot. Core 1 copies the latest snapshot into its display view at
// the start of every pass, and publishes its own measurements (oil pressure,
// both EGT) for core 0. The lock covers only the copies; drawing, I2C, serial
// and parsing run outside it.

typedef struct {
  float values[F_LAST]; // decoded ECU frames and the ABS speed, by F_*
  bool ecuConnected;
  bool dpfConnected;
  uint32_t ecuUpdatedMs; // hal_millis() of the last ECU frame, 0 before one
  uint32_t redrawSeq;    // bumped when a value drawn at once has changed
} clocks_ecu_snapshot_t;

enum { EGT_PRE_DPF, EGT_IN_DPF, EGT_CHANNELS };

// Measurements of core 1. A value without its valid flag is no measurement.
typedef struct {
  float oilBar;
  bool oilValid;
  uint32_t oilUpdatedMs; // hal_millis() of the last valid reading
  float egtC[EGT_CHANNELS];
  bool egtValid[EGT_CHANNELS];
  uint32_t egtUpdatedMs[EGT_CHANNELS];
} clocks_local_snapshot_t;

/** @brief Create the lock; call on core 0 before core 1 starts. */
void clocksStateInit(void);

/**
 * @brief Publish the ECU state of core 0.
 * @param snapshot Copied under the lock; NULL is ignored.
 */
void clocksStatePublishEcu(const clocks_ecu_snapshot_t *snapshot);

/**
 * @brief Copy the latest ECU snapshot into the display view of core 1.
 * @return True when the redraw sequence changed since the previous call.
 */
bool clocksStateRefreshView(void);

/**
 * @brief Core 1: take new local measurements into the display view and
 *        publish them for core 0.
 * @param local Copied under the lock; NULL is ignored.
 */
void clocksStatePublishLocal(const clocks_local_snapshot_t *local);

/**
 * @brief Core 0: copy the latest local measurements of core 1.
 * @param out Receives the copy; NULL is ignored. A reading older than
 *        OIL_PRESSURE_MAX_AGE_MS or THERMOCOUPLE_MAX_AGE_MS at the end of the
 *        copy comes without its valid flag.
 */
void clocksStateCopyLocal(clocks_local_snapshot_t *out);

// Display view of core 1, written by clocksStateRefreshView() and
// clocksStatePublishLocal().
extern float valueFields[];

bool isEcuConnected(void);
bool isDPFConnected(void);
bool isOilPressureValid(void);
/** @param channel EGT_PRE_DPF or EGT_IN_DPF. */
bool isEgtValid(int channel);
bool isFanEnabled(void);
bool isDPFRegenerating(void);
float readFuel(void);
int getCurrentCarSpeed(void);
int getGPSSpeed(void);
bool isGPSAvailable(void);
bool isEngineRunning(void);
int getEngineRPM(void);
float getOilPressure(void);

#endif
