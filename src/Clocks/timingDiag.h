#ifndef C_TIMING_DIAG
#define C_TIMING_DIAG

#include <stdbool.h>
#include <stdint.h>

// Timing statistics of the TIMING build variant. The core that receives CAN
// reports "T0": INT edge to the applied RPM frame, gaps between two CAN
// drains, the longest drain and send, ECU frames lost by frame number and
// those that came after the report that counted them lost (with the index
// of that report, win, for the latest TIMING_DIAG_LATE_HISTORY reports),
// tachometer toggle jitter, the charge lamp (state, changes, voltage range)
// and its loop duration. The core that draws reports "T1": its loop duration,
// the lateness of the half-second display timer and the longest pass over
// both thermocouples. Each core keeps and prints
// its own window, so no statistic crosses cores. Both lines also give the
// unused stack of that core (s0_free, s1_free) on RP targets. Without
// CLOCKS_TIMING_DIAG every hook compiles to nothing.
#ifndef CLOCKS_TIMING_DIAG
#define CLOCKS_TIMING_DIAG 0
#endif

#define TIMING_DIAG_REPORT_MS 5000u
// Load phase: a full gauge redraw every TIMING_DIAG_REDRAW_MS, alternating
// with an idle phase of the same length.
#define TIMING_DIAG_PHASE_MS 60000u
#define TIMING_DIAG_REDRAW_MS 1000u

#if CLOCKS_TIMING_DIAG

/**
 * @brief Fill the unused part of the calling core's stack with a pattern.
 * @param core 0 or 1; call first thing in that core's initialisation.
 */
void timingDiagPaintStack(uint8_t core);
/** @brief MCP2515 INT edge; call from the CAN interrupt handler. */
void timingDiagCanInterrupt(void);
/**
 * @brief Call after every CAN drain (hal_can_process_all()).
 * @param startUs timingDiagLoopStart() stamp taken before the drain.
 */
void timingDiagCanDrained(uint32_t startUs);
/**
 * @brief Call after every hal_can_send() of Clocks.
 * @param startUs timingDiagLoopStart() stamp taken before the send.
 */
void timingDiagCanSent(uint32_t startUs);
/** @brief Count one received frame; ECU frames feed the loss counter. */
void timingDiagCanFrame(uint32_t canID, const uint8_t *buf, uint8_t len);
/** @brief Call after an RPM frame reached the tachometer output. */
void timingDiagRpmApplied(void);
/** @brief Call from the tachometer toggle callback. */
void timingDiagTachoToggle(uint32_t halfPeriodUs);
/** @brief Call from the half-second display timer. */
void timingDiagDisplayTick(void);
/**
 * @brief Call after every reading pass of both thermocouples.
 * @param startUs timingDiagLoopStart() stamp taken before the pass.
 */
void timingDiagEgtRead(uint32_t startUs);
/**
 * @brief Call after every charge lamp update.
 * @param lit Output state just written.
 * @param millivolts Voltage the lamp judged.
 */
void timingDiagChargeLamp(bool lit, int32_t millivolts);
/** @return Start stamp for timingDiagLoopEnd(). */
uint32_t timingDiagLoopStart(void);
/**
 * @brief Close one loop iteration and print the window of that core.
 * @param core 0 for the CAN core report, 1 for the display core report.
 * @param startUs Stamp returned by timingDiagLoopStart().
 */
void timingDiagLoopEnd(uint8_t core, uint32_t startUs);
/** @return True when the load phase asks for a full redraw now. */
bool timingDiagRedrawDue(void);

#else

static inline void timingDiagPaintStack(uint8_t core) { (void)core; }
static inline void timingDiagCanInterrupt(void) {}
static inline void timingDiagCanDrained(uint32_t startUs) { (void)startUs; }
static inline void timingDiagCanSent(uint32_t startUs) { (void)startUs; }
static inline void timingDiagCanFrame(uint32_t canID, const uint8_t *buf,
                                      uint8_t len) {
  (void)canID;
  (void)buf;
  (void)len;
}
static inline void timingDiagRpmApplied(void) {}
static inline void timingDiagTachoToggle(uint32_t halfPeriodUs) {
  (void)halfPeriodUs;
}
static inline void timingDiagDisplayTick(void) {}
static inline void timingDiagEgtRead(uint32_t startUs) { (void)startUs; }
static inline void timingDiagChargeLamp(bool lit, int32_t millivolts) {
  (void)lit;
  (void)millivolts;
}
static inline uint32_t timingDiagLoopStart(void) { return 0u; }
static inline void timingDiagLoopEnd(uint8_t core, uint32_t startUs) {
  (void)core;
  (void)startUs;
}
static inline bool timingDiagRedrawDue(void) { return false; }

#endif

#endif
