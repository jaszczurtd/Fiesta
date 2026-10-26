#include "timingDiag.h"

#if CLOCKS_TIMING_DIAG

#include "../common/canDefinitions/canDefinitions.h"
#include <JaszczurHAL.h>
#include <hal/core/hal_memory.h>
#include <hal/hal.h>

#include <stdio.h>
#include <string.h>

#if HAL_TARGET_IS_RP
// Linker symbols of the bottom of both core stacks.
extern "C" uint32_t __StackBottom[];
extern "C" uint32_t __StackOneBottom[];
#endif

// Bucket upper edges in microseconds; the last bucket holds everything above.
static const uint32_t kLatencyEdgesUs[] = {100u,  250u,   500u,   1000u, 2000u,
                                           5000u, 10000u, 20000u, 50000u};
static const uint32_t kJitterEdgesUs[] = {5u,   10u,  25u,   50u,  100u,
                                          250u, 500u, 1000u, 5000u};

#define TIMING_DIAG_BUCKETS (COUNTOF(kLatencyEdgesUs) + 1u)

// Reports whose frame numbers a late frame is looked up in.
#define TIMING_DIAG_LATE_HISTORY 4u

// Late frames of one window that an earlier report counted as lost.
typedef struct {
  uint32_t window; // index of that report
  uint32_t count;
} timing_diag_late_t;

typedef struct {
  uint32_t count;
  uint32_t max;
  uint64_t sum;
  uint32_t buckets[TIMING_DIAG_BUCKETS];
} timing_diag_stat_t;

// Statistics of the CAN core; the INT and timer interrupts run on that core.
typedef struct {
  timing_diag_stat_t rx;    // INT edge -> RPM frame on the tachometer
  timing_diag_stat_t gap;   // between two CAN drains
  timing_diag_stat_t tacho; // |toggle interval - half period|
  uint32_t rxUnstamped;     // RPM frames read without a recorded INT edge
  uint32_t drainMax;        // duration of one CAN drain
  uint32_t sendMax;         // duration of one hal_can_send()
  uint32_t lampChanges;     // charge lamp output changes
  uint32_t lampSamples;
  int32_t lampMvMin;
  int32_t lampMvMax;
  uint32_t ecuFrames;
  uint32_t ecuLate;   // numbered at or below s_ecuAccountedSeq on arrival
  int32_t ecuLastSeq; // highest unwrapped frame number of the window
  timing_diag_late_t ecuLateFrom[TIMING_DIAG_LATE_HISTORY];
  uint32_t ecuLateWindows; // used entries of ecuLateFrom
  uint32_t ecuLateOld;     // late frames older than the history
  uint32_t loopMax;
} timing_diag_can_window_t;

typedef struct {
  uint32_t loopMax;
  uint32_t ticks;
  uint32_t lateMaxMs;
  uint32_t egtMax; // duration of one pass over both thermocouples
} timing_diag_display_window_t;

static timing_diag_can_window_t s_can;
static timing_diag_display_window_t s_display;

static volatile uint32_t s_intStampUs = 0u; // 0 = no edge since the drain
static uint32_t s_lastDrainUs = 0u;
static bool s_drained = false;
static uint32_t s_lastToggleUs = 0u;
static uint32_t s_lastHalfPeriodUs = 0u;
static int32_t s_ecuSeq = 0;
static bool s_ecuSeqKnown = false;
// Highest frame number accounted by the reports so far.
static int32_t s_ecuAccountedSeq = 0;
// Index of the next CAN report and the numbers the latest reports
// accounted, newest first: firstSeq..lastSeq of report `window`.
static uint32_t s_reportIndex = 0u;
typedef struct {
  uint32_t window;
  int32_t firstSeq;
  int32_t lastSeq;
} timing_diag_accounted_t;
static timing_diag_accounted_t s_accounted[TIMING_DIAG_LATE_HISTORY];
static uint32_t s_accountedCount = 0u;
static uint32_t s_bootCanMs = 0u;
static uint32_t s_bootRpmMs = 0u;
static uint32_t s_lastDisplayTickMs = 0u;
static bool s_displayTicked = false;
static uint32_t s_lastReportMs[2] = {0u, 0u};
static uint32_t s_lastRedrawMs = 0u;

static const uint32_t kStackPaint = 0xA5A5A5A5u;
// Room left for the frame of the painting call itself.
static const uintptr_t kStackPaintMarginBytes = 64u;

static uint32_t *stackBottom(uint8_t core) {
#if HAL_TARGET_IS_RP
  return (core == 0u) ? __StackBottom : __StackOneBottom;
#else
  (void)core;
  return NULL;
#endif
}

void timingDiagPaintStack(uint8_t core) {
  uint32_t *word = stackBottom(core);
  if (word == NULL) {
    return;
  }
  const uintptr_t limit =
      (uintptr_t)__builtin_frame_address(0) - kStackPaintMarginBytes;
  while ((uintptr_t)word < limit) {
    *word++ = kStackPaint;
  }
}

// Bytes at the bottom of the stack that still hold the paint pattern.
static uint32_t stackFreeBytes(uint8_t core) {
  const uint32_t *word = stackBottom(core);
  if (word == NULL) {
    return 0u;
  }
  uint32_t count = 0u;
  while (word[count] == kStackPaint) {
    count++;
  }
  return count * (uint32_t)sizeof(uint32_t);
}

static void HAL_RAM_FUNC(statAdd)(timing_diag_stat_t *stat, uint32_t value,
                                  const uint32_t *edges, size_t edgeCount) {
  size_t bucket = 0u;
  while (bucket < edgeCount && value > edges[bucket]) {
    bucket++;
  }
  stat->buckets[bucket]++;
  stat->count++;
  stat->sum += value;
  if (value > stat->max) {
    stat->max = value;
  }
}

static bool isEcuFrame(uint32_t canID) {
  switch (canID) {
  case CAN_ID_ECU_UPDATE_01:
  case CAN_ID_RPM:
  case CAN_ID_THROTTLE:
  case CAN_ID_ECU_UPDATE_02:
  case CAN_ID_ECU_UPDATE_03:
  case CAN_ID_TURBO_PRESSURE:
  case CAN_ID_GPS_EXT_LAT:
  case CAN_ID_GPS_EXT_LON_TIME:
    return true;
  default:
    return false;
  }
}

void timingDiagCanInterrupt(void) {
  if (s_intStampUs == 0u) {
    s_intStampUs = hal_micros() | 1u;
  }
}

static bool s_lampLit = true;
static bool s_lampSeen = false;
static uint32_t s_lampChangedMs = 0u; // hal_millis() of the last change

void timingDiagChargeLamp(bool lit, int32_t millivolts) {
  if (s_lampSeen && lit != s_lampLit) {
    s_can.lampChanges++;
    s_lampChangedMs = hal_millis();
  }
  if (s_can.lampSamples == 0u || millivolts < s_can.lampMvMin) {
    s_can.lampMvMin = millivolts;
  }
  if (s_can.lampSamples == 0u || millivolts > s_can.lampMvMax) {
    s_can.lampMvMax = millivolts;
  }
  s_can.lampSamples++;
  s_lampSeen = true;
  s_lampLit = lit;
}

void timingDiagCanSent(uint32_t startUs) {
  const uint32_t duration = hal_micros() - startUs;
  if (duration > s_can.sendMax) {
    s_can.sendMax = duration;
  }
}

void timingDiagCanDrained(uint32_t startUs) {
  const uint32_t now = hal_micros();
  hal_critical_section_enter();
  if (now - startUs > s_can.drainMax) {
    s_can.drainMax = now - startUs;
  }
  if (s_drained) {
    statAdd(&s_can.gap, now - s_lastDrainUs, kLatencyEdgesUs,
            COUNTOF(kLatencyEdgesUs));
  } else {
    s_bootCanMs = hal_millis();
  }
  s_drained = true;
  s_lastDrainUs = now;
  s_intStampUs = 0u;
  hal_critical_section_exit();
}

// A late frame: the report whose numbers hold it counted it as lost.
static void countLateFrame(int32_t seq) {
  s_can.ecuLate++;
  for (uint32_t i = 0u; i < s_accountedCount; i++) {
    if (seq < s_accounted[i].firstSeq || seq > s_accounted[i].lastSeq) {
      continue;
    }
    const uint32_t window = s_accounted[i].window;
    for (uint32_t j = 0u; j < s_can.ecuLateWindows; j++) {
      if (s_can.ecuLateFrom[j].window == window) {
        s_can.ecuLateFrom[j].count++;
        return;
      }
    }
    // One entry per report of the history, so there is always room.
    s_can.ecuLateFrom[s_can.ecuLateWindows].window = window;
    s_can.ecuLateFrom[s_can.ecuLateWindows].count = 1u;
    s_can.ecuLateWindows++;
    return;
  }
  s_can.ecuLateOld++;
}

void timingDiagCanFrame(uint32_t canID, const uint8_t *buf, uint8_t len) {
  if (buf == NULL || len == 0u || !isEcuFrame(canID)) {
    return;
  }
  // The ECU numbers every frame it sends; unwrap the 8-bit counter so that
  // reordered transmit buffers are not counted as losses.
  const uint8_t number = buf[CAN_FRAME_NUMBER];
  if (s_ecuSeqKnown) {
    s_ecuSeq += (int8_t)(uint8_t)(number - (uint8_t)s_ecuSeq);
  } else {
    s_ecuSeq = number;
    s_ecuSeqKnown = true;
    s_ecuAccountedSeq = s_ecuSeq - 1;
  }
  if (s_ecuSeq <= s_ecuAccountedSeq) {
    countLateFrame(s_ecuSeq);
  } else if (s_can.ecuFrames == s_can.ecuLate || s_ecuSeq > s_can.ecuLastSeq) {
    s_can.ecuLastSeq = s_ecuSeq;
  }
  s_can.ecuFrames++;
}

void timingDiagRpmApplied(void) {
  const uint32_t now = hal_micros();
  const uint32_t stamp = s_intStampUs;
  if (s_bootRpmMs == 0u) {
    s_bootRpmMs = hal_millis();
  }
  if (stamp == 0u) {
    s_can.rxUnstamped++;
    return;
  }
  statAdd(&s_can.rx, now - stamp, kLatencyEdgesUs, COUNTOF(kLatencyEdgesUs));
}

// Called from the timer interrupt of the tachometer output.
void HAL_RAM_FUNC(timingDiagTachoToggle)(uint32_t halfPeriodUs) {
  const uint32_t now = hal_micros();
  if (s_lastToggleUs != 0u && halfPeriodUs == s_lastHalfPeriodUs) {
    const uint32_t interval = now - s_lastToggleUs;
    const uint32_t error = (interval > halfPeriodUs) ? interval - halfPeriodUs
                                                     : halfPeriodUs - interval;
    statAdd(&s_can.tacho, error, kJitterEdgesUs, COUNTOF(kJitterEdgesUs));
  }
  s_lastToggleUs = now | 1u;
  s_lastHalfPeriodUs = halfPeriodUs;
}

void timingDiagEgtRead(uint32_t startUs) {
  const uint32_t duration = hal_micros() - startUs;
  if (duration > s_display.egtMax) {
    s_display.egtMax = duration;
  }
}

void timingDiagDisplayTick(void) {
  const uint32_t now = hal_millis();
  if (s_displayTicked) {
    const uint32_t interval = now - s_lastDisplayTickMs;
    const uint32_t late =
        (interval > SECOND / 2u) ? interval - SECOND / 2u : 0u;
    if (late > s_display.lateMaxMs) {
      s_display.lateMaxMs = late;
    }
  }
  s_lastDisplayTickMs = now;
  s_displayTicked = true;
  s_display.ticks++;
}

uint32_t timingDiagLoopStart(void) { return hal_micros(); }

static void formatBuckets(char *out, size_t size,
                          const timing_diag_stat_t *stat) {
  size_t used = 0u;
  out[0] = '\0';
  for (size_t i = 0u; i < TIMING_DIAG_BUCKETS && used < size; i++) {
    const int written =
        snprintf(&out[used], size - used, "%s%lu", (i == 0u) ? "" : ",",
                 (unsigned long)stat->buckets[i]);
    if (written < 0) {
      break;
    }
    used += (size_t)written;
  }
}

static uint32_t statAverage(const timing_diag_stat_t *stat) {
  return (stat->count == 0u) ? 0u : (uint32_t)(stat->sum / stat->count);
}

static void reportCanWindow(void) {
  timing_diag_can_window_t window;
  hal_critical_section_enter();
  window = s_can;
  memset(&s_can, 0, sizeof(s_can));
  hal_critical_section_exit();

  static char rxBuckets[96];
  static char gapBuckets[96];
  static char tachoBuckets[96];
  formatBuckets(rxBuckets, sizeof(rxBuckets), &window.rx);
  formatBuckets(gapBuckets, sizeof(gapBuckets), &window.gap);
  formatBuckets(tachoBuckets, sizeof(tachoBuckets), &window.tacho);

  // Frames numbered after the last one accounted, against those received of
  // them: a frame lost between two windows counts in the later one. A frame
  // that comes after the report that counted it as lost is reported as late,
  // with the index (win) of that report, so a lost frame can be given back
  // to the window it was counted in.
  const uint32_t reportIndex = s_reportIndex++;
  uint32_t expected = 0u;
  const uint32_t received = window.ecuFrames - window.ecuLate;
  if (received != 0u && window.ecuLastSeq > s_ecuAccountedSeq) {
    expected = (uint32_t)(window.ecuLastSeq - s_ecuAccountedSeq);
    memmove(&s_accounted[1], &s_accounted[0],
            sizeof(s_accounted[0]) * (TIMING_DIAG_LATE_HISTORY - 1u));
    s_accounted[0].window = reportIndex;
    s_accounted[0].firstSeq = s_ecuAccountedSeq + 1;
    s_accounted[0].lastSeq = window.ecuLastSeq;
    if (s_accountedCount < TIMING_DIAG_LATE_HISTORY) {
      s_accountedCount++;
    }
    s_ecuAccountedSeq = window.ecuLastSeq;
  }
  const uint32_t lost = (expected > received) ? expected - received : 0u;

  // "<win>x<count>" per report, "-" without late frames
  static char lateFrom[TIMING_DIAG_LATE_HISTORY * 24u];
  size_t used = 0u;
  lateFrom[0] = '\0';
  for (uint32_t i = 0u; i < window.ecuLateWindows; i++) {
    const int written = snprintf(&lateFrom[used], sizeof(lateFrom) - used,
                                 "%s%lux%lu", (i == 0u) ? "" : ",",
                                 (unsigned long)window.ecuLateFrom[i].window,
                                 (unsigned long)window.ecuLateFrom[i].count);
    if (written < 0 || (size_t)written >= sizeof(lateFrom) - used) {
      break;
    }
    used += (size_t)written;
  }
  if (used == 0u) {
    (void)snprintf(lateFrom, sizeof(lateFrom), "-");
  }

  deb("T0 load:%d rx_n:%lu rx_max:%lu rx_avg:%lu rx_h:%s rx_ns:%lu "
      "gap_n:%lu gap_max:%lu gap_h:%s drain_max:%lu tx_max:%lu ecu_n:%lu "
      "ecu_lost:%lu ecu_late:%lu ecu_late_win:%s ecu_late_old:%lu win:%lu "
      "tg_n:%lu tg_max:%lu tg_h:%s l0_max:%lu boot_can_ms:%lu "
      "boot_rpm_ms:%lu s0_free:%lu chg:%d chg_n:%lu chg_ms:%lu "
      "chg_mv:%ld..%ld",
      (int)((hal_millis() / TIMING_DIAG_PHASE_MS) % 2u),
      (unsigned long)window.rx.count, (unsigned long)window.rx.max,
      (unsigned long)statAverage(&window.rx), rxBuckets,
      (unsigned long)window.rxUnstamped, (unsigned long)window.gap.count,
      (unsigned long)window.gap.max, gapBuckets, (unsigned long)window.drainMax,
      (unsigned long)window.sendMax, (unsigned long)window.ecuFrames,
      (unsigned long)lost, (unsigned long)window.ecuLate, lateFrom,
      (unsigned long)window.ecuLateOld, (unsigned long)reportIndex,
      (unsigned long)window.tacho.count, (unsigned long)window.tacho.max,
      tachoBuckets, (unsigned long)window.loopMax, (unsigned long)s_bootCanMs,
      (unsigned long)s_bootRpmMs, (unsigned long)stackFreeBytes(0u),
      s_lampLit ? 1 : 0, (unsigned long)window.lampChanges,
      (unsigned long)s_lampChangedMs, (long)window.lampMvMin,
      (long)window.lampMvMax);
}

static void reportDisplayWindow(void) {
  const timing_diag_display_window_t window = s_display;
  s_display.loopMax = 0u;
  s_display.ticks = 0u;
  s_display.lateMaxMs = 0u;
  s_display.egtMax = 0u;
  deb("T1 load:%d l1_max:%lu ticks:%lu late_max_ms:%lu s1_free:%lu "
      "egt_max:%lu",
      (int)((hal_millis() / TIMING_DIAG_PHASE_MS) % 2u),
      (unsigned long)window.loopMax, (unsigned long)window.ticks,
      (unsigned long)window.lateMaxMs, (unsigned long)stackFreeBytes(1u),
      (unsigned long)window.egtMax);
}

void timingDiagLoopEnd(uint8_t core, uint32_t startUs) {
  const uint32_t duration = hal_micros() - startUs;
  const uint8_t index = (core == 0u) ? 0u : 1u;
  if (index == 0u) {
    hal_critical_section_enter();
    if (duration > s_can.loopMax) {
      s_can.loopMax = duration;
    }
    hal_critical_section_exit();
  } else if (duration > s_display.loopMax) {
    s_display.loopMax = duration;
  }

  // A muted debug stream (open SC session) keeps the window collecting.
  if (hal_debug_is_muted() ||
      !hal_millis_interval_elapsed_now(&s_lastReportMs[index],
                                       TIMING_DIAG_REPORT_MS)) {
    return;
  }
  if (index == 0u) {
    reportCanWindow();
  } else {
    reportDisplayWindow();
  }
}

bool timingDiagRedrawDue(void) {
  const uint32_t now = hal_millis();
  if (((now / TIMING_DIAG_PHASE_MS) % 2u) == 0u) {
    return false;
  }
  return hal_millis_interval_elapsed(now, &s_lastRedrawMs,
                                     TIMING_DIAG_REDRAW_MS);
}

#endif
