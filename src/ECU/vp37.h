#ifndef T_VP37
#define T_VP37

#include <JaszczurHAL.h>
#include <hal/control/hal_pid_controller.h>
#include <hal/serial/hal_serial.h>

#include "../common/adjustometer_protocol.h"
#include "vp37_adjustometer.h"
#include "vp37_config.h"
#include "vp37_current.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Adjustometer position feedback and the calibrated range. */
typedef struct {
  bool calibrationDone;
  int32_t position; /**< Newest quantity position, in adjustometer Hz. */
  int stabilityTable[STABILITY_ADJUSTOMETER_TAB_SIZE];
  int32_t adjustMin;      /**< Calibrated bottom of the stroke. */
  int32_t adjustMiddle;   /**< Calibrated middle of the stroke. */
  int32_t adjustMax;      /**< Calibrated top of the stroke. */
  int32_t operateMax;     /**< Highest position the controller may request. */
  uint32_t commLostSince; /**< hal_millis() when the adjustometer went quiet,
                             0 while it answers. */
  uint8_t lastStatus;     /**< Status byte of the last adjustometer frame. */
  bool fresh;             /**< A new sample arrived since the previous cycle. */
  uint32_t rawHz;         /**< Unfiltered position sample. */
  uint32_t filteredHz;    /**< Adjustometer-filtered position sample. */
  uint32_t sampleNumber;  /**< Sequence number of the newest sample. */
  uint32_t sampleUs;      /**< Adjustometer time stamp of the newest sample. */
  uint16_t ageUs;         /**< Age of the newest sample when the adjustometer
                               published it. */
  hal_status_t readStatus; /**< Result of the last adjustometer read. */
  uint32_t readUs;         /**< Duration of the last adjustometer read. */
  uint8_t retries;         /**< Retries the last adjustometer read needed. */
  bool commFailed;         /**< Adjustometer silent for longer than allowed. */
  float fuelTempC;         /**< Fuel temperature from the last good frame. */
  float supplyVolts;       /**< Supply measured by the Adjustometer [V]. */
  uint32_t readCount;      /**< Good frames since start, wrapping; the board
                                publishes temperature and supply when it
                                changes. */
} VP37Feedback;

/** @brief Source-independent requested position and its shared motion ramp. */
typedef struct {
  float requestedPercent;     /**< Requested position in 0..100%; -1 before a
                                 demand. */
  float physicalLimitPercent; /**< Share of the travel that 100 % demand
                                 reaches; VP37_PHYSICAL_LIMIT_PERCENT at start.
                                 Outside (0, 100] the full travel applies. */
  float topArrivalDecel;      /**< Brake of every rising ramp into the top of
                                 the range [% of travel/s^2]; 0 = off. */
  int32_t target;             /**< Calibrated position target [Hz]. */
  int32_t desired;            /**< Ramped target sent to the PID [Hz]. */
  float desiredPosition;      /**< Fractional ramp state [Hz]. */
  uint32_t targetChangedMs;
  bool targetMoving; /**< The last target change came within
                        VP37_TARGET_STABLE_MS of the one before it. */
  bool atRest;       /**< Zero demand after slew: PWM off, PID state cleared. */
} VP37Demand;

/** @brief Holding command from the map, the motion terms and the learned
 * trim. */
typedef struct {
  float pwm;
  float riseBlend;       /**< Filtered upward slew, normalized to the FF
                                       reference rate. */
  float motion;          /**< Additional nominal PWM for upward motion. */
  float motionBoostUp;   /**< Upward motion correction at the reference rate,
                            nominal PWM; scales the map's motion column. */
  float motionBoostDown; /**< Downward motion correction at the reference
                            rate, nominal PWM, subtracted while the target
                            falls. */
  float fallBlend;       /**< Filtered downward rate, reference units. */
  float motionRateCap;   /**< Ramp rate the upward assist saturates at
                            [% of travel/s]; 0 = the full slew. */
  float mapTrim[VP37_MAP_TRIM_KNOTS]; /**< Learned holding-map residual per
                                         position knot, nominal PWM. */
  float mapTrimApplied;      /**< Trim added to the feedforward this step. */
  bool mapTrimEnabled;       /**< Learning and application switch. */
  uint32_t mapTrimTransfers; /**< Integral-to-trim transfers so far. */
} VP37Feedforward;

/** @brief The correction loop: gains, authority, dead zone and the settled
 * hold. */
typedef struct {
  hal_pid_controller_t controller;
  int32_t error; /**< Position error the controller last saw, in Hz. */
  float correction;
  float positiveLimit;
  float tf;
  bool saturatedHigh;
  hal_pid_terms_t terms;
  bool integralHold; /**< Settled-position hysteresis currently freezes I. */
  bool integralHoldEnterPending;
  uint32_t integralHoldEnterStartedMs;
  uint32_t integralHoldConfirmMs; /**< Continuous time inside the entry band. */
  bool integralHoldReleasePending;
  uint32_t integralHoldReleaseStartedMs;
  float negativeLimit;
  float upperLimit;
  float kp, ki, kd;
  float topKd;         /**< Additional settled-target D gain [PWM*s/Hz], blended
                            from zero at 85% to its full value at 90% demand. */
  float effectiveKd;   /**< Base plus scheduled D gain last sent to the PID,
                            in PWM*s/Hz. */
  float topDBlend;     /**< Filtered upper-target damping activation, 0..1. */
  float integralLimit; /**< Available integral contribution in nominal PWM
                             counts. */
  float integralOverride;      /**< Bench cap in nominal PWM; zero selects
                                     position profile. */
  float integralDeadbandTopHz; /**< Dead zone at full stroke; zero keeps the
                                  base dead zone everywhere. */
  float integralDeadbandHz;    /**< Dead zone applied in the last step. */
  bool integralHoldEntered;    /**< Hold engaged in this step; transfer once. */
  bool softFloorActive;
} VP37Pid;

/** @brief Supply-voltage multiplier and the sources it is fused from. */
typedef struct {
  bool cycleEnabled; /**< Full-period supply mean from the shunt
                               capture scales the command; the local ADC is
                               the fallback. Bench switch V0/V1. */
  bool cycleUsed;
  bool cycleValid;
  float cycleVolts;
  uint32_t cycleUs; /**< Midpoint of the latest supply window [hal_micros]. */
  uint32_t cycleAgeUs; /**< Window age when used by the control step [us]. */
  uint32_t predictionSampleUs; /**< Last supply window consumed by the slope. */
  float predictionSampleVolts; /**< Calibrated mean of that window [V]. */
  float voltageSlope; /**< Filtered slope of fresh supply means [V/s]. */
  float
      predictionVolts; /**< Bounded lead added only to the voltage scale [V]. */
  bool predictionReady; /**< A previous supply window is available. */
  float correction;
  float inputVolts; /**< Measured, calibrated voltage before prediction. */
  float heldVolts;  /**< Predicted or fallback-filtered voltage used by PWM. */
  bool ready;       /**< Compensation voltage has been initialized. */
  bool frozen;      /**< Bench V2: hold the scale, open the supply loop. */
  float lastVolts;
  float localVolts;         /**< Unfiltered local ECU ADC voltage. */
  float previousLocalVolts; /**< Previous local ADC sample. */
  float localScale;         /**< Slow local-to-Adjustometer calibration. */
  bool localReady;          /**< Local ADC source has a previous sample. */
  bool overRange;           /**< Local reading above the calibrated range;
                                      it still scales the command down. */
} VP37Supply;

/** @brief Thermal multiplier: the fuel-temperature model, the measured drive
 * resistance, the handover between them and the current observation that
 * feeds it. */
typedef struct {
  bool observationEnabled; /**< Scan publish switch (bench Q0/Q1). */
  bool cycleValid;         /**< Healthy capture matched to a latched command. */
  bool cycleSettled;  /**< The matched command was settled before capture. */
  float cycleAmps;    /**< ON-phase mean of the captured PWM period. */
  float cycleVolts;   /**< Supply that belongs to that same capture. */
  int32_t cyclePwm;   /**< Duty reconstructed between actual PWM latches. */
  int32_t cycleDrive; /**< Historical command matched to the PWM latch. */
  uint32_t cycleUs;
  float lastFuelTemp;
  float temperatureCorrection; /**< Filtered multiplier of the complete FF + PID
                                  command. */
  float temperatureCompensationWeight; /**< Bench blend: 0 disables, 1 applies
                                          the model. */
  bool temperatureReady; /**< A valid temperature has initialized the
                            multiplier. */
  float driveResistance; /**< Filtered drive-path resistance from the
                            measured current, in ohms. */
  float
      driveObservationOhms; /**< Last valid matched ratio, including motion. */
  bool driveLearning;       /**< This step accepted a resistance observation. */
  uint32_t
      driveLearnedSamples; /**< Accepted updates, wrapping at UINT32_MAX. */
  float driveCorrection;   /**< Measured replacement for the fuel-temperature
                              multiplier; unity until enough samples. */
  uint32_t driveSamples;   /**< Accepted resistance observations. */
  uint32_t driveUpdatedMs; /**< Last observation that updated resistance. */
  uint32_t
      driveObservedMs;         /**< Last healthy new observation, for expiry;
                                  includes observations paused by a supply change. */
  uint32_t driveFirstSampleMs; /**< First observation of the current estimate;
                                  the filter needs time from here, not just
                                  a count. */
  uint32_t
      driveLastCycleUs; /**< Last consumed current observation timestamp. */
  bool driveCycleSeen;  /**< Timestamp zero is valid at timer wrap. */
  float driveVoltageReference; /**< Supply anchor for detecting a transition. */
  uint32_t driveVoltageChangedMs; /**< Last change of the supply anchor. */
  bool driveVoltageReady;         /**< Supply anchor has been initialized. */
  bool
      driveVoltageSettled; /**< Resistance learning may use the current rail. */
  bool driveResistanceReady; /**< Enough observations to drive the output. */
  bool driveCompensationEnabled; /**< Bench switch for the measured path. */
  bool driveCompensationUsed;    /**< Measured path scaled the last command. */
  float scale;                   /**< Rate-limited multiplier actually applied,
                                           between the measured path and the model. */
  bool scaleReady;               /**< The multiplier has taken a first value. */
} VP37Thermal;

/** @brief Shunt scan bookkeeping and the newest reduced block. */
typedef struct {
  VP37CurrentPulseResult cycleResult; /**< Newest reduced scan block, kept for
                                         the core-0 `VP37 IPULSE` report. */
  hal_status_t cycleResultStatus;
  uint32_t cycleResultSequence; /**< Increments once per reduced block. */
  uint32_t
      collectUs; /**< Time spent collecting and reducing the latest block. */
  uint32_t reducedUs;    /**< MCU time when the latest reduction completed. */
  uint32_t lastSequence; /**< Scan block sequence last retained. */
  uint32_t blocks;       /**< Blocks retained since start. */
  uint32_t gaps;         /**< Blocks the control loop never saw. */
  uint32_t frameNs;      /**< Frame period reported by the scan. */
  bool running;
} VP37Scan;

/** @brief One command before its application at the next PWM wrap. */
typedef struct {
  uint32_t writtenUs; /**< Local timestamp immediately before the PWM write. */
  float nominalPwm;   /**< FF+position PID, excluding current correction. */
  float voltageScale; /**< Local ADC calibration used by this command. */
  int32_t pwm; /**< Delivered duty including every correction and clamp. */
  bool driveSettled; /**< Quiet command, position and rail before this write. */
} VP37CurrentCommand;

/** @brief Bounded ON-current feedback and the command history it observes. */
typedef struct {
  VP37CurrentCommand history[VP37_CURRENT_CONTROL_HISTORY];
  VP37CurrentCommand
      matchedCommand; /**< Copy shared by this step's consumers. */
  uint32_t
      matchedSampleSequence; /**< Reduced-block sequence of the cached match. */
  bool
      matchCached; /**< No command was written since this match was resolved. */
  bool matchFound; /**< The cached observation matched one history command. */
  uint32_t count;
  uint32_t next;
  uint32_t lastCycleUs; /**< Last accepted rising edge; zero is valid. */
  uint32_t sampleAgeUs; /**< ON midpoint age when the control step used it. */
  float targetAmps; /**< Newest nominal position command in ON-current units. */
  float sampleTargetAmps; /**< Target belonging to the measured PWM period. */
  float measuredAmps;     /**< Guarded ON-phase mean [A]. */
  float errorAmps;        /**< Historical target minus measured current [A]. */
  float requestedPwm;     /**< Bounded proportional correction before slew. */
  float correctionPwm;    /**< Applied correction in nominal PWM counts. */
  bool enabled; /**< Bench C0/C1; normal control uses the same path. */
  bool active;  /**< A fresh observation matches a recorded command. */
  bool seen;    /**< At least one matching PWM period has been consumed. */
  uint32_t driveStableSinceUs; /**< Start of the present quiet window [us]. */
  uint32_t driveLastCommandUs; /**< Previous write, for continuity checks. */
  int32_t drivePositionReference; /**< Position anchor of the quiet window. */
  int32_t drivePwmReference; /**< Delivered-PWM anchor of the quiet window. */
  bool driveTracking; /**< The quiet-window anchors have been initialized. */
  bool driveSettled; /**< Latest command passed the resistance-learning gate. */
} VP37CurrentControl;

/** @brief The command as written to the actuator. */
typedef struct {
  float pwmValue;
  int32_t lastPWMval;
  int32_t finalPWM;
  bool pwmLimited;
} VP37Output;

/**
 * @brief Board services the VP37 module drives the pump through.
 *
 * Set once per pump with VP37_setCallbacks() before VP37_init(). Start-up and
 * calibration call them on the core that runs VP37_init(), the control step on
 * the core that runs VP37_process(), the bench telemetry adjustometerTransfer
 * on the core that prints it; none of them may wait for the other core.
 */
typedef struct {
  /** Write the quantity actuator command, 0..VP37_PWM_RESOLUTION. */
  void (*writeQuantityPwm)(int32_t command);
  /** Write the timing actuator command, 0..VP37_PWM_RESOLUTION. */
  void (*writeTimingPwm)(int32_t command);
  /** Switch the drive enable output. */
  void (*setDriveEnabled)(bool enabled);
  /** Read the drive enable output back. */
  bool (*driveEnabled)(void);
  /** Write the register address @p reg to the Adjustometer and read @p len
   * bytes back in one bus transaction; HAL_OK or the bus error. */
  hal_status_t (*adjustometerTransfer)(uint8_t reg, uint8_t *data, size_t len);
  /** Optional: whether the drive may run this step; NULL always allows it. */
  bool (*driveAllowed)(void);
  /** Optional: feed the watchdog during the blocking start-up waits. */
  void (*feedWatchdog)(void);
  /** Optional, telemetry: name of the running bench test, NULL for none. */
  const char *(*activeTestName)(void);
  /** Optional, telemetry: step delay of the running cyclic test [ms]. */
  uint32_t (*cyclicDelayMs)(void);
} VP37Callbacks;

/** @brief What each published status word holds: the run flag as 0 or 1, the
 * good-frame count, and the bit patterns of the two floats. */
enum {
  VP37_STATUS_WORD_INITIALIZED = 0,
  VP37_STATUS_WORD_READ_COUNT,
  VP37_STATUS_WORD_FUEL_TEMP,
  VP37_STATUS_WORD_SUPPLY,
  VP37_STATUS_WORDS
};

/** @brief What every build reads of a pump from another core. */
typedef struct {
  bool initialized;   /**< The pump runs; false again after a stop. */
  uint32_t readCount; /**< Good Adjustometer frames so far. */
  float fuelTempC;    /**< Fuel temperature of the newest good frame [C]. */
  float supplyVolts;  /**< Supply of the newest good frame [V]. */
} VP37Status;

#if VP37_TELEMETRY_ENABLED
/** @brief The pump as the bench telemetry prints it. */
typedef struct {
  float pidTimeUpdate;
  uint32_t controlLastUs, controlDtUs, controlSequence, pidDtUs;
  uint32_t controlExecUs; /**< Execution time of the published step up to
                             its publication [us]. */
  uint32_t cyclicDelayMs; /**< Step delay of the running cyclic test [ms]. */
  const char *activeTestName;          /**< Running bench test, or NULL. */
  adjustometer_reading_t adjustometer; /**< Newest Adjustometer reading. */
  VP37Feedback feedback;
  VP37Demand demand;
  VP37Feedforward feedforward;
  VP37Pid pid;
  VP37Supply supply;
  VP37Thermal thermal;
  VP37Scan scan;
  VP37CurrentControl currentControl;
  VP37Output output;
} VP37Telemetry;

/**
 * @brief The pump as published at the end of a control step. The control core
 * writes it with a sequence counter, other cores copy it without a lock.
 * @note A copy for the reader: the pump keeps only the status, so its layout
 * is the same with and without VP37_TELEMETRY_ENABLED.
 */
typedef struct {
  VP37Status status;
  VP37Telemetry telemetry;
} VP37Snapshot;
#endif

/**
 * @brief One VP37 injection pump: lifecycle and timing, then one member per
 * unit of the module. The members are embedded, not pointed to, because the
 * published snapshot copies them; pointers would make the copy alias the live
 * state.
 */
typedef struct {
  bool vp37Initialized;
  VP37Callbacks callbacks; /**< Board services; optional ones filled with
                              defaults by VP37_setCallbacks(). */
  bool servicesFixed;      /**< VP37_init() has run: the board services and the
                              Adjustometer frame stay as they are, also after a
                              stop. */
  VP37AdjustometerReader adjustometer; /**< Last reading, reader state. */
  float pidTimeUpdate;
  uint32_t controlLastUs;
  uint32_t controlDtUs;
  uint32_t
      controlExecUs; /**< Execution time of the latest control step [us]. */
  uint32_t controlSequence;
  bool controlStarted;
  bool pidStarted;
  uint32_t pidLastUs, pidDtUs;
  VP37Feedback feedback;
  VP37Demand demand;
  VP37Feedforward feedforward;
  VP37Pid pid;
  VP37Supply supply;
  VP37Thermal thermal;
  VP37Scan scan;
  VP37CurrentControl currentControl;
  VP37Output output;
  /** The status as VP37_STATUS_WORD_* words, written and read with relaxed
   * atomic accesses only; read it with VP37_readStatus() or
   * VP37_readSnapshot(). */
  uint32_t published[VP37_STATUS_WORDS];
  uint32_t publishedSequence; /**< Odd while a step writes the snapshot. */
} VP37Pump;

#if VP37_TELEMETRY_ENABLED
/** @brief One control step; positions in Hz, elapsed time in us, output in PWM
 * counts. */
typedef struct {
  uint32_t us, dt,
      sequence;           /**< MCU timestamp, elapsed time and step number. */
  float requestedPercent; /**< Requested position [%]. */
  int32_t target, desired, measured,
      pwm;             /**< Target, slewed target, feedback and PWM. */
  float motionFF;      /**< Upward-motion component included in feedforward. */
  float ff, low, high; /**< Feedforward and effective correction limits. */
  float volts;         /**< Latest measured supply voltage (V). */
  float localVolts;    /**< Simultaneous local ECU ADC supply voltage (V). */
  float compensationInputVolts; /**< Fused voltage before hysteresis (V). */
  float compensationVolts;      /**< Supply voltage used for PWM scaling (V). */
  float voltageCorrection;      /**< Effective supply-voltage multiplier. */
  float mapTrim;  /**< Learned holding-map residual in the feedforward. */
  float fuelTemp; /**< Fuel temperature (C) used by control. */
  float temperatureCorrection; /**< Temperature multiplier used by this step. */
  float thermalScale;          /**< Multiplier after the rate limit. */
  hal_pid_terms_t terms; /**< Contributions and limits from the same step. */
  uint32_t rawHz, filteredHz, sampleNumber, measuredUs;
  uint32_t readUs;
  uint32_t cyclicDelayMs; /**< Active cyclic step delay, or zero otherwise. */
  uint32_t
      pidDtUs; /**< Elapsed time for a successful PID step; zero when held. */
  /* Keep narrow fields together: every padding byte repeats in the trace. */
  hal_status_t readStatus;
  uint16_t ageUs;
  uint8_t status; /**< Adjustometer status from this control step. */
  uint8_t retries;
  bool cycleVoltageUsed : 1; /**< Uses a fresh complete PWM-period supply mean.
                              */
  bool voltageOverRange : 1; /**< Supply reading above the calibrated range. */
  bool integralHold : 1;     /**< Settled-position integral hold is active. */
  bool softFloor : 1;
  bool hardwareClamp : 1;  /**< Active downstream bounds. */
  bool quantityAtRest : 1; /**< Quantity drive released at zero demand. */
  bool fresh : 1;
} VP37TraceSample;
#endif

/**
 * @brief Copy the status part of the published snapshot without a lock.
 * @param self Pump; only its published snapshot is read.
 * @param out Non-NULL destination.
 * @return HAL_OK, HAL_EINVAL for NULL, HAL_ENOENT before the first
 * publication, or HAL_EAGAIN when every attempt overlapped a publication;
 * @p out is undefined unless HAL_OK.
 */
hal_status_t VP37_readStatus(const VP37Pump *self, VP37Status *out);

#if VP37_TELEMETRY_ENABLED
/** @brief Copy the whole published snapshot without a lock; results as
 * VP37_readStatus(). */
hal_status_t VP37_readSnapshot(const VP37Pump *self, VP37Snapshot *out);

/**
 * @brief Wait, on a core other than the control core, until the pump
 * publishes again.
 * @param self Pump; only its publication counter is read.
 * @param timeoutUs Longest wait [us].
 * @return True when a publication ended during the wait.
 * @note The control core idles from a publication to its next period, so
 * telemetry formatted right after one does not compete with the control step
 * for the XIP cache; formatted during a step it made the step ~300 us longer
 * on average. The control core never waits for the caller.
 */
bool VP37_waitForPublication(const VP37Pump *self, uint32_t timeoutUs);

/* The trace buffer passes between the cores without a lock: the control core
 * records a capture and hands it over complete, the reading core drains it and
 * hands the buffer back. */
/** @brief Start a capture on the control core. Non-NULL self must be running.
 * @return HAL_OK, HAL_EINVAL for NULL, HAL_EBUSY if capturing/draining,
 * or HAL_EAGAIN when control is inactive. */
hal_status_t VP37_startTrace(const VP37Pump *self);
/** @brief Take the next sample of a completed capture; non-NULL sample. A
 * stopped controller completes a partial capture. Error leaves sample
 * unchanged.
 * @return HAL_OK, HAL_EINVAL for NULL, HAL_EAGAIN while recording, or
 * HAL_ENOENT. */
hal_status_t VP37_readTrace(VP37TraceSample *sample);
/** @brief Whether a capture is being recorded. */
bool VP37_traceCapturing(void);
/** @brief Print a non-NULL captured sample. */
void VP37_showTrace(const VP37TraceSample *sample);
#endif

typedef enum {
  VP37_INIT_OK = 0,
  VP37_INIT_ALREADY_INITIALIZED,
  VP37_INIT_BASELINE_NOT_READY,
  VP37_INIT_PID_CREATE_FAILED,
  VP37_INIT_CALIBRATION_FAILED,
  VP37_INIT_OUTPUT_UNAVAILABLE /**< No board services or no drive outputs. */
} VP37InitStatus;

/**
 * @brief Install the board services of one pump.
 * @param self Pump that is not running.
 * @param callbacks Table with at least the two PWM writes, both enable
 * functions and the Adjustometer transfer; it is copied, and missing optional
 * entries get defaults.
 * @return HAL_OK, HAL_EINVAL for NULL or a missing required entry, HAL_ESTATE
 * once VP37_init() has run, also after a stop: another core reads the
 * Adjustometer transfer without a lock.
 */
hal_status_t VP37_setCallbacks(VP37Pump *self, const VP37Callbacks *callbacks);

/**
 * @brief Select the Adjustometer frame and reset the reader.
 * @param self Pump before its first VP37_init().
 * @param enabled True for the versioned fast frame, false for the legacy
 * register block.
 * @return HAL_OK, HAL_EINVAL for NULL, or HAL_ESTATE once VP37_init() has
 * run, since the control core owns the reader from then on.
 */
hal_status_t VP37_setAdjustometerFastFeedback(VP37Pump *self, bool enabled);

/**
 * @brief Read the Adjustometer once through the adjustometerTransfer service.
 * @return The new reading; a failed read returns the previous values with
 * commOk cleared (fast frame) or cleared after three failures (legacy block).
 */
adjustometer_reading_t VP37_readAdjustometer(VP37Pump *self);

/**
 * @brief Read the diagnostic extension on top of a reading.
 * @param self Pump whose adjustometerTransfer is used; nothing else of it is
 * read or changed.
 * @param reading Reading, for example from a snapshot, whose extension fields
 * are filled on success and left alone otherwise.
 * @return True when a coherent version-1 extension was received.
 */
bool VP37_readAdjustometerExtended(const VP37Pump *self,
                                   adjustometer_reading_t *reading);

/**
 * @brief Compute the available positive PID correction for a temperature.
 * @param fuelTempC Adjustometer fuel-temperature reading in degrees Celsius.
 * @param adjustometerStatus Latest Adjustometer status-bit field.
 * @param pwmFeedForward Nominal feedforward at the current requested position.
 * @return Positive PID correction limit in nominal-voltage PWM counts.
 * @note A broken or implausible temperature signal falls back to the original
 *       cold limit, so a sensor fault can never request extra actuator drive.
 */
float VP37_computePositiveCorrectionLimit(float fuelTempC,
                                          uint8_t adjustometerStatus,
                                          float pwmFeedForward);

/**
 * @brief Initialize the VP37 inner quantity-control loop and calibrate
 * Adjustometer limits.
 * @return Initialization status code.
 * @note Functionally this brings up the project-local N146/G149-like path.
 *       Adjustometer remains only G149-like, not a literal OEM G149.
 */
VP37InitStatus VP37_init(VP37Pump *self);

/**
 * @brief Reduce the newest completed scan block and publish the observation.
 * @param self Non-NULL pump owned by the calling control core.
 * @return HAL_EAGAIN when no new block was available, otherwise the reduce
 * status; a rejected block still updates cycleResult for the report.
 * @note Core 1, once per control step. Publishing follows the observation
 * switch; the report fields are updated regardless of it.
 */
hal_status_t VP37_serviceCurrentScan(VP37Pump *self);

#if VP37_TELEMETRY_ENABLED
/**
 * @brief Print the `VP37 IPULSE` report for the newest reduced block.
 * @param snapshot Non-NULL snapshot from VP37_readSnapshot().
 * @note Bench telemetry; absent without VP37_TELEMETRY_ENABLED.
 */
void VP37_showCurrentPulse(const VP37Snapshot *snapshot);
#endif

/**
 * @brief Process one cycle of the VP37 inner quantity-actuator loop.
 * @note This is the low-level N146/G149-like loop. Higher-level requested-fuel-
 *       quantity arbitration is still represented only partially in the current
 * code.
 */
void VP37_process(VP37Pump *self);

/**
 * @brief Enable or disable the VP37 output stage.
 * @param self VP37 controller instance issuing the command.
 * @param enable True to enable the actuator path, false to disable it.
 * @note This is a project-local run/enable output and is only loosely
 * comparable to the OEM N109 stop-solenoid path.
 */
void VP37_enableVP37(VP37Pump *self, bool enable);

/** @brief Latch control off and remove PWM. Non-NULL self; restart requires
 * initialization. */
void VP37_stop(VP37Pump *self);

/** @brief Whether the pump runs with a calibrated stroke and can take a
 * demand; control core. */
bool VP37_isReady(const VP37Pump *self);

/**
 * @brief Read back the current VP37 enable output state.
 * @return True when VP37 output is enabled, otherwise false.
 * @note The signal is project-local and should not be treated as a literal N109
 * alias.
 */
bool VP37_isVP37Enabled(VP37Pump *self);

#if VP37_TELEMETRY_ENABLED
/**
 * @brief Print VP37 controller state for diagnostics.
 * @param self Pump whose Adjustometer transfer reads the diagnostic extension;
 * nothing else of it is used.
 * @param snapshot Snapshot from VP37_readSnapshot().
 * @note Performs serial and I2C I/O. Bench telemetry; absent without
 * VP37_TELEMETRY_ENABLED.
 */
void VP37_showDebug(const VP37Pump *self, const VP37Snapshot *snapshot);
#endif

/**
 * @brief Set the VP37 timing-actuator output as a normalized angle command.
 * @param self VP37 controller instance issuing the command.
 * @param angle Requested timing angle in the 0..100 range.
 * @note In OEM terminology this is closest to commanding the N108
 * start-of-injection actuator path. Closed-loop G80/G28 SOI feedback is not
 * implemented here yet.
 */
void VP37_setInjectionTiming(VP37Pump *self, int32_t angle);

/**
 * @brief Set the quantity actuator's position in percent of the stroke.
 * @param self Controller instance; NULL returns HAL_EINVAL.
 * @param percent Position across the usable stroke, clamped to 0..100%;
 * 100 % is VP37_PHYSICAL_LIMIT_PERCENT of the calibrated travel.
 * @return HAL_OK on acceptance, HAL_ESTATE before calibration, or HAL_EINVAL
 * for NULL/non-finite input. A non-finite value requests zero when calibrated.
 * @note This is the unit engine control works in: the percentage is mapped
 * onto the usable part of the calibrated stroke, so the same number means the
 * same position whatever the calibration produced. Every source then shares one
 * ramp, feedforward, PID and set of limits. Analog sensor conditioning belongs
 * to the sensor layer. Call on the control core. Zero
 * follows the shared descent and release policy.
 */
hal_status_t VP37_setPositionDemandPercentage(VP37Pump *self, float percent);

/**
 * @brief Set the quantity actuator's position in raw feedback counts.
 * @param self Controller instance; NULL returns HAL_EINVAL.
 * @param value Position in feedback counts, clamped to the calibrated range
 * between VP37_getPositionDemandMinValue() and
 * VP37_getPositionDemandMaxValue().
 * @return HAL_OK on acceptance, HAL_EINVAL for NULL, or HAL_ESTATE before
 * calibration.
 * @note The unit the position loop itself works in, so a caller addressing a
 * measured or recorded position hits it exactly instead of through a
 * percentage that rounds. Everything past the entry point is shared with
 * VP37_setPositionDemandPercentage(), including the percent the telemetry
 * reports.
 */
hal_status_t VP37_setPositionDemandValue(VP37Pump *self, int32_t value);

/**
 * @brief Lowest position VP37_setPositionDemandValue() accepts.
 * @param self Controller instance to inspect.
 * @return Calibrated bottom of the stroke in feedback counts, or -1 for NULL
 * and before calibration.
 */
int32_t VP37_getPositionDemandMinValue(const VP37Pump *self);

/**
 * @brief Highest position VP37_setPositionDemandValue() accepts.
 * @param self Controller instance to inspect.
 * @return The physical limit of the stroke (VP37_PHYSICAL_LIMIT_PERCENT of the
 * calibrated travel) in feedback counts, or -1 for NULL and before
 * calibration.
 */
int32_t VP37_getPositionDemandMaxValue(const VP37Pump *self);

#ifdef __cplusplus
}
#endif

#endif
