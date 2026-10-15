# ECU and VP37 control

[Polski](README.pl.md) · [System architecture](../../ARCHITECTURE.md)


The ECU controls fuel injection through the VP37 pump, boost through the N75
solenoid, glow plugs, the fan, the block heater, and the heated windshield. It
also stores DTCs and answers OBD-II requests, presenting itself as a Ford
Fiesta 1.8 DI EEC-V ECU. It is the MISRA-C migration target, together with the shared VP37 module
and the Adjustometer, both gated at zero findings; [`MISRA.md`](../../MISRA.md)
describes the current state.

Bench observations quoted below explain tuning choices. They are not
acceptance results for an arbitrary new firmware image. Control changes
require new measurements of the mechanical system.

## Execution and synchronization

The work is split between the cores:

- **Core 0** runs the main loop: a soft-timer table, CAN traffic, OBD-II, the
  relay outputs, DTC storage, and the Serial Configurator session.
- **Core 1** runs the time-critical engine path: the VP37 control cycle, the
  RPM interrupt, and turbo control.

There is no RTOS. `start.c` installs a table of `(period, callback)` pairs
that the core-0 loop calls when due, from about 10 ms for fast sensor reads to
one second for housekeeping. The VP37 pump belongs to core 1: every control
step ends by publishing its state behind a sequence counter, and core 0 copies
that snapshot without a lock. The published words are written and read only
with relaxed atomic accesses, so an overlapping copy is discarded, never a
data race, and the control loop never waits for the
telemetry or for the global values it feeds. The bench telemetry waits on core
0 for the end of a step before it formats a report: code fetched from flash
by core 0 during a step made the step about 300 µs longer on average. The
bench trace buffer changes hands through one atomic state. The I²C bus with the PCF8574 output latch and
the DTC manager with its storage sit behind mutexes. Flash writes stay on
core 0.

The main controllers (VP37, turbo, RPM, fan, heater, glow plugs, windows, and
engine state) live in one `ecu_context_t` in
[`ecuContext.h`](../../src/ECU/ecuContext.h). CAN, sensors, DTC storage, GPS, OBD,
and the configurator session keep their state in file-local structs.

## Position demand and input conditioning

VP37 takes a position request in one of two units:
`hal_status_t VP37_setPositionDemandPercentage(VP37Pump *pump, float percent)`
for a 0-100% demand across the usable stroke, and
`hal_status_t VP37_setPositionDemandValue(VP37Pump *pump, int32_t value)` for
raw feedback counts, bounded by `VP37_getPositionDemandMinValue()` and
`VP37_getPositionDemandMaxValue()`. The usable stroke ends at
`VP37_PHYSICAL_LIMIT_PERCENT` (85%) of the calibrated travel. This keeps the
actuator below the negative-stiffness steps of the upper stroke and below the
band under 86% that turns soft in cold fuel, at the cost of the largest fuel
quantities; 100 restores the full travel. The integration dead zone widens
steeply above 82% of the travel, so a full-demand hold settles about 100 Hz
short of the limit instead of creeping up to it. Both write the same
demand, so driver input, engine control, and bench tests share one position
ramp, feedforward, and PID. The unit is the only difference: the controller
has no input-source mode, source-specific behaviour, filter-mode flag, or
alternate position path.
Emergency stop and calibration remain separate operations.

Analog input conditioning belongs to `sensors.c`. A 10 ms update preserves
fractional percentages, applies a 30 ms filter with a 0.1 percentage-point
deadband, and caches the result for `getDriverDemandPercent()`. Zero input
reaches the cache without filter delay. Both direct driver demand and
`engineOperation` read this output; the application selects which request to
send to VP37. Optional engine control is enabled by
`ECU_ENGINE_CONTROL_ENABLED` in `config.h`.

## Position control

VP37 uses 130 Hz PWM. A lone rising demand step is approached as a standing
target from its first cycle: the ramp rises at 225%/s with 0.6 of the motion
assist and brakes before the target at 750%/s², so the actuator follows the
ramp closely and arrives slowly instead of overshooting and ringing. A target
that changes again within 25 ms is tracked at up to 300%/s without a brake;
its upward assist grows with the ramp rate up to 250%/s and stays there.
Descent always keeps 300%/s; a lone falling step keeps
the full assist for its first 25 ms and 0.3 of it afterwards. The
upper-stroke rules follow the physical position whatever the limit: the
upward-assist taper between 75% and 85%, the integral authority and dead-zone
tapers from 75%, the slower upper slews (275%/s, standing 187.5%/s) and the
settled-target damping, which reaches its full position weight between 85%
and 90% with a default gain of 0.001 PWM·s/Hz, engages after the target
settles and fades through a 50 ms blend when movement resumes. Without them a
fast ramp overshoots the physical limit by a tenth of the stroke and the descent
oscillates. The holding map and supply/temperature compensation remain shared
across the stroke. Once the
demand settles, the climb floor follows negative learned integral trim, so
crossing below the target preserves the reduction needed to hold position.
Moving demands retain the original climb floor. Telemetry reports
`mode:position` and a separate `test:<name|none>` field; it does not identify a
potentiometer controller mode.

## Supply and resistance compensation

Supply compensation scales the complete feedforward and PID command by
`12 V / compensated voltage`. ADC scan blocks arrive every 2.016 ms at 130 Hz
PWM. Core 1 copies them between position steps and tracks gate edges across
block boundaries. Each ADC code is compensated once on arrival; the 5 ms
control step reduces each completed pulse once
and refreshes the latest supply window at every step. The frames
provide a mean over the latest PWM-period window without waiting for a current
edge. While the mean is less than 20 ms old, a bounded voltage prediction
accounts for its midpoint age at the control step and half a PWM period for
output latching. The voltage slope grows through a 20 ms filter, stays within
the latest measured slope, and drops promptly when the ramp slows or reverses.
The predicted offset is limited to ±0.5 V and affects only the command scale.
The local ADC fallback retains its 50 ms filter without prediction. Current
and resistance estimation use a separate voltage mean from the same PWM period
as the current capture.

Resistance estimation uses the same historical PWM-latch matching as the
current loop. Each command records whether position, delivered PWM and supply
have remained quiet for 150 ms after the demand ramp finishes. Learning requires
both that historical qualification and a still-quiet present state, so a delayed
observation from an approach cannot become eligible just by arriving later.
Those quiet captures alone build the first estimate. Once it is ready, matched
captures taken in motion keep updating it at the same 2 s rate: the observed
resistance rides the current lag behind the duty during a sweep, so the
estimate follows coil self-heating and at the same time acts as motion
feedforward. Slowing or capping that update raised the cyclic tracking error
on the bench.
The quiet window permits 60 Hz of position movement and 8 PWM counts from its
anchors; exceeding either restarts it. Each current period can update the
slow resistance estimate once. Its filter uses observation intervals, capped
at 50 ms after gaps. A cumulative supply
change over 0.1 V pauses estimation for 100 ms. Healthy new current captures
keep the last ready correction active during long supply changes; losing
those captures still expires it after 10 s. Supply-change detection uses the
measured voltage before prediction. Telemetry exposes `vage` (window midpoint
age at the control step, in microseconds), `vlead` (prediction offset in volts
before the 7 V floor), `vdot` (voltage slope in V/s), and `rhold` (resistance
estimation paused by supply or drive movement). `Robs` is the latest matched
resistance observation in ohms; `rmatch` reports a matched capture, `rquiet` its
historical qualification, `rlearn` an update in this control step, and `rlcnt`
the cumulative update count. Unmatched observations keep the last ready
estimate alive without changing its value. `scanus` measures ADC collection
and reduction time; `execus` measures execution of the complete control step.
Both durations are in microseconds and exclude the wait for the next step.

## Current feedback

A bounded proportional current loop corrects the position command before
the supply and thermal multipliers. Its target is the nominal feedforward
plus position-PID output expressed as guarded ON-phase current. The conversion
preserves the resistance estimator's raw-voltage reference and the local
divider calibration. The source shunt does not measure recirculation current
while the MOSFET is off, so this target is not full-period coil current.

The inverted drive conducts at the end of each hardware PWM period. Each
observation is matched to the command written before the preceding current
falling edge, where the PWM counter wraps and latches its compare register.
A tracked demand that stops at the top of the range (pedal on the floor)
arrives at the full rate. The upward motion assist follows the ramp rate up
to `VP37_PWM_FF_MOTION_RATE_CAP_PERCENT_PER_S` (250 %/s, bench knob `X5`)
and stays there for faster ramps: with the assist of the 300 %/s slew the
actuator led the ramp by 260-500 Hz into the top and rang when hot, while a
250 %/s ramp led by about 150 Hz and settled. With the Adjustometer's
drive-period filter the arrival overshoots by 100-200 Hz and stays below the
negative-stiffness steps. The optional brake into the top
(`VP37_TOP_ARRIVAL_DECEL_PERCENT_PER_S2`, bench knob `X2`) is off: at 750 %/s²
it slowed the last 70 % of the range and at 3000 %/s² it still delayed the
arrival by 100 ms and deepened the sag after it; slowing only the ramp widens
the actuator's lead instead of removing it. The first demand after start
ramps from the calibrated bottom, where calibration leaves the actuator.
The ADC history retains 3.25 PWM periods. A current capture closes when its
falling edge is confirmed, without waiting for the next ON phase. Current,
duty and paired supply use the same fall-to-fall interval, whose duration does
not change when the duty changes.
Writes within 100 us of that edge are ambiguous and rejected, as are
duty mismatches, invalid captures and ON-midpoint ages of 25 ms or more.
The proportional gain is 0.35 in equivalent nominal-command units; correction
is limited to ±40 nominal PWM counts and changes at most 1000 counts/s.
Position-PID limits account for this correction before stepping the PID.
Repeated observations do not recompute the error; unavailable feedback slews
the correction to zero, and rest or stop clears it immediately. There is no
additional current integrator.

## Telemetry

Current-loop telemetry exposes `cen` (enabled), `cuse` (matched fresh capture),
`ctar` (latest current target, A), `cref` (historical target, A), `cerr`
(historical target minus measured ON current, A), `cpwm` (applied nominal
correction), and `cage` (ON-midpoint age, us). The current fields are also
reported with IPULSE, alongside `latch`, `lp`, `lduty` and `lv` for the
reconstructed latch time, period, duty and validity. `dma`, `take` and `reduce`
are MCU timestamps for DMA completion, retention and reduction; `reduce -
(us + on)` measures delivery delay after ON ends. Bench `IWAVE` lines, emitted
at most every 100 ms, share the IPULSE `us` identifier and report eight
time-ordered ON-phase means with their sample times. They exclude the edge
guards and cannot measure freewheel
decay. `gl` counts rejected gate excursions since the last history reset.
Bench builds accept
`C0`/`C1` to compare feedback off/on
through the same position API. Electrical-model tests do not establish
mechanical stability; upper-position holds and motion need hardware validation.

Upward motion assistance retains its full value through 75% of the calibrated
stroke, fades linearly to zero at 85%, and remains off above that point.
This limits acceleration into the sensitive upper region. The holding map,
downward assistance, demand ramp and position API retain their existing roles.

## Source map

| File | Responsibility |
|---|---|
| [`start.c`](../../src/ECU/start.c) | start-up order, soft-timer table, watchdog, both core loops |
| [`sensors.c`](../../src/ECU/sensors.c) | analog inputs and filtered driver-demand cache, PCF8574 outputs, I²C register reads |
| [`can.c`](../../src/ECU/can.c) | main CAN frames, including the RPM publisher |
| [`obd-2.c`](../../src/ECU/obd-2.c) | OBD CAN input and ISO-TP responses |
| [`obd_j1979.c`](../../src/ECU/obd_j1979.c) | SAE J1979 services and Mode 01 PIDs |
| [`obd_ford_diag.c`](../../src/ECU/obd_ford_diag.c) | Ford EEC-V UDS, KWP2000, and SCP services |
| [`dtcManager.c`](../../src/ECU/dtcManager.c) | DTC catalogue, storage, and diagnostic reads |
| [`rpm.c`](../../src/ECU/rpm.c) | engine speed from the Hall sensor interrupt |
| [`src/common/vp37/`](../../src/common/vp37/) | the VP37 module, shared by the ECU and VP37TestBench; its translation units are the list in [`vp37_sources.txt`](../../src/common/vp37/vp37_sources.txt), which the host-test build reads through CMake and `sync_vscode_projects.py` writes into the firmware manifests of the modules the registry marks with `sharedSources` |
| [`vp37.c`](../../src/common/vp37/vp37.c) | VP37 pump: start-up, the position-demand entry points, and the control cycle that calls the units below |
| [`vp37_adapter.c`](../../src/ECU/vp37_adapter.c) | the ECU's board services for VP37 (PWM channels, PCF8574 enable, engine-speed guard, Adjustometer bus transfer), its start-up, and `F_FUEL_TEMP`/`F_VOLTS` from the published status |
| [`vp37_adjustometer.c`](../../src/common/vp37/vp37_adjustometer.c) | Adjustometer frames, the diagnostic extension, and the start-up baseline wait |
| [`vp37_snapshot.c`](../../src/common/vp37/vp37_snapshot.c) | state published at the end of each control step and the lock-free copies of it |
| [`vp37_feedback.c`](../../src/common/vp37/vp37_feedback.c) | Adjustometer position and the calibration sweep |
| [`vp37_compensation.c`](../../src/common/vp37/vp37_compensation.c) | corrections for supply voltage, fuel temperature, and coil resistance |
| [`vp37_control.c`](../../src/common/vp37/vp37_control.c) | feedforward from the holding map, learned trim, PID, dead zone, and hold |
| [`vp37_current.c`](../../src/common/vp37/vp37_current.c) | coil current measured on the shunt |
| [`vp37_current_control.c`](../../src/common/vp37/vp37_current_control.c) | bounded ON-current feedback and PWM command history |
| [`vp37_tuning.c`](../../src/common/vp37/vp37_tuning.c) | gains, limits, and switches the bench may change, each checked against its range |
| [`vp37_telemetry.c`](../../src/common/vp37/vp37_telemetry.c) | control samples and bench traces, printed on core 0; bench image only |
| [`turbo.c`](../../src/ECU/turbo.c) | boost control from manifold pressure |
| [`engineMaps.c`](../../src/ECU/engineMaps.c) | N75 duty table |
| [`vp37_maps.c`](../../src/common/vp37/vp37_maps.c) | VP37 holding map, integral and dead-zone tapers |
| [`engineFan.c`](../../src/ECU/engineFan.c), [`engineHeater.c`](../../src/ECU/engineHeater.c), [`glowPlugs.c`](../../src/ECU/glowPlugs.c), [`heatedWindshield.c`](../../src/ECU/heatedWindshield.c) | relay outputs |
| [`engineFuel.c`](../../src/ECU/engineFuel.c) | fuel level |
| [`gps.c`](../../src/ECU/gps.c) | NMEA time and date |
| [`config.c`](../../src/ECU/config.c) | stored settings and the configurator session |
| [`tests.c`](../../src/ECU/tests.c) | the functional test suite: each bench test, what it drives, and whether the sequence and the configurator include it |
| [`tests_workers.c`](../../src/ECU/tests_workers.c) | what each test does: demand generators, one-shot actions and the progress they report |
| [`tests_helpers.c`](../../src/ECU/tests_helpers.c) | test parameters and the runner behind the bench console and the configurator's test commands, with its engine-speed interlock and keepalive; idle stubs without `ECU_FUNCTIONAL_TESTS_ENABLED` |

## Bench image and hardware

The tests exist only in the bench image, the `BENCH` variant that
[`hal_project_config.h`](../../src/ECU/hal_project_config.h) declares with
`ECU_FUNCTIONAL_TESTS_ENABLED=1`. The HAL modules and settings it shares with
the host tests are in [`ecu_hal_config.h`](../../src/ECU/ecu_hal_config.h), because
JaszczurHAL reads the variant declaration only from `hal_project_config.h`
itself. Select `Project: Build variant: BENCH` or
`Project: Upload variant: BENCH` in VS Code. Artifacts go to
`src/ECU/.build/rp2040/rp2040-plus/variants/BENCH/`. Tasks for `rp2040-plus`
without a variant build the car image. See the [build guide](../../doc/BUILDING.md)
for CLI equivalents.

[`device_tree.json`](../../src/ECU/device_tree.json) assigns every pin and address.
The interfaces are:

- **I²C** at 400 kHz, ECU as master: Adjustometer at `0x57`, PCF8574A relay
  expander at `0x38`. The PCF8574A is rated for 100 kHz, so it runs outside
  its datasheet until it is replaced by a PCA8574A or gets its own bus. Its
  local type in `src/ECU/bindings/` caps the bus at 400 kHz, so a faster
  I²C clock fails the device tree check.
- **SPI** shared by two MCP2515 CAN controllers, CAN0 for the car and CAN1 for
  the OBD-II port.
- **ADC**: six analog inputs through the HC4051 multiplexer (coolant, oil and
  air temperature, throttle, fuel level, manifold pressure), the supply
  voltage, and the VP37 shunt on GPIO26, which no other peripheral may use.
  While the VP37 current scan runs it owns the converter, and the other analog
  reads come from its buffer.
- **PWM** for the VP37 and N75 outputs; **GPIO** for the DPF lamp;
  **GPIO interrupt** for the engine Hall sensor; a **software serial** line
  on the PIO for GPS.
- **Flash-backed EEPROM** for DTCs and settings.


## Control-loop RAM budget


The ECU retains current and supply samples in a two-channel ring; the sensor
multiplexer remains part of the three-channel DMA scan. Current reduction
and edge tracking use `HAL_RAM_FUNC` to limit instruction fetches from flash.
The bench trace retains 1024 complete control samples, with its boolean flags
stored as bits. `VP37_TRACE_SAMPLES` controls capture length. Inspect both
firmware variants' ELF memory maps when changing these buffers or RAM code.
