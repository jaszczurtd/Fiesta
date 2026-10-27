#include "clocksState.h"
#include "config.h"

#include <JaszczurHAL.h>
#include <hal/core/hal_mutex_once.h>
#include <hal/hal.h>

#include <string.h>

float valueFields[F_LAST];

static hal_mutex_t s_stateMutex = NULL;
static clocks_ecu_snapshot_t s_published;
static clocks_ecu_snapshot_t s_view;
static uint32_t s_viewRedrawSeq = 0u;
static clocks_local_snapshot_t s_localPublished;
static clocks_local_snapshot_t s_local; // core 1
static float s_gpsSpeedView;            // derived once per local publication

// The local measurements of core 1 replace the unused ECU fields.
static void overlayLocal(void) {
  valueFields[F_OIL_PRESSURE] = s_local.oilValid ? s_local.oilBar : 0.0f;
  valueFields[F_EGT] =
      s_local.egtValid[EGT_PRE_DPF] ? s_local.egtC[EGT_PRE_DPF] : 0.0f;
  valueFields[F_DPF_TEMP] =
      s_local.egtValid[EGT_IN_DPF] ? s_local.egtC[EGT_IN_DPF] : 0.0f;
  valueFields[F_GPS_IS_AVAILABLE] = s_local.gps.available ? 1.0f : 0.0f;
  valueFields[F_GPS_CAR_SPEED] = s_gpsSpeedView;
}

void clocksStateInit(void) { (void)jh_hal_mutex_create_once(&s_stateMutex); }

void clocksStatePublishEcu(const clocks_ecu_snapshot_t *snapshot) {
  if (snapshot == NULL || s_stateMutex == NULL) {
    return;
  }
  hal_mutex_lock(s_stateMutex);
  s_published = *snapshot;
  hal_mutex_unlock(s_stateMutex);
}

bool clocksStateRefreshView(void) {
  if (s_stateMutex == NULL) {
    return false;
  }
  hal_mutex_lock(s_stateMutex);
  s_view = s_published;
  hal_mutex_unlock(s_stateMutex);

  memcpy(valueFields, s_view.values, sizeof(s_view.values));
  overlayLocal();
  const bool redraw = (s_view.redrawSeq != s_viewRedrawSeq);
  s_viewRedrawSeq = s_view.redrawSeq;
  return redraw;
}

void clocksStatePublishLocal(const clocks_local_snapshot_t *local) {
  if (local == NULL || s_stateMutex == NULL) {
    return;
  }
  s_local = *local;
  s_gpsSpeedView = (float)clocksGpsSpeedX10(&s_local.gps) / 10.0f;
  overlayLocal();
  hal_mutex_lock(s_stateMutex);
  s_localPublished = *local;
  hal_mutex_unlock(s_stateMutex);
}

// A reading stamped after nowMs is new, not 49 days old.
static bool readingExpired(uint32_t nowMs, uint32_t updatedMs,
                           uint32_t maxAgeMs) {
  const int32_t age = (int32_t)(nowMs - updatedMs);
  return age >= 0 && (uint32_t)age >= maxAgeMs;
}

void clocksStateCopyLocal(clocks_local_snapshot_t *out) {
  if (out == NULL || s_stateMutex == NULL) {
    return;
  }
  hal_mutex_lock(s_stateMutex);
  *out = s_localPublished;
  hal_mutex_unlock(s_stateMutex);

  // A reading core 1 stopped renewing is no measurement any more. The time
  // is taken after the copy, so core 1 stamped every reading in it before.
  const uint32_t nowMs = hal_millis();
  clocksGpsExpire(&out->gps, nowMs);
  if (readingExpired(nowMs, out->oilUpdatedMs, OIL_PRESSURE_MAX_AGE_MS)) {
    out->oilValid = false;
  }
  for (int channel = 0; channel < EGT_CHANNELS; channel++) {
    if (readingExpired(nowMs, out->egtUpdatedMs[channel],
                       THERMOCOUPLE_MAX_AGE_MS)) {
      out->egtValid[channel] = false;
    }
  }
}

bool isEcuConnected(void) { return s_view.ecuConnected; }

bool isDPFConnected(void) { return s_view.dpfConnected; }

bool isAbsSpeedValid(void) { return s_view.absReady; }

bool isOilPressureValid(void) { return s_local.oilValid; }

bool isEgtValid(int channel) {
  return (channel >= 0 && channel < EGT_CHANNELS) && s_local.egtValid[channel];
}

int getEngineRPM(void) { return int(valueFields[F_RPM]); }

bool isEngineRunning(void) { return (getEngineRPM() != 0); }

bool isFanEnabled(void) { return valueFields[F_FAN_ENABLED] > 0; }

bool isDPFRegenerating(void) { return valueFields[F_DPF_REGEN] > 0; }

float readFuel(void) { return valueFields[F_FUEL]; }

int getCurrentCarSpeed(void) { return int(valueFields[F_ABS_CAR_SPEED]); }

int getGPSSpeed(void) { return int(valueFields[F_GPS_CAR_SPEED]); }

float getOilPressure(void) { return valueFields[F_OIL_PRESSURE]; }

bool isGPSAvailable(void) { return valueFields[F_GPS_IS_AVAILABLE] > 0; }
