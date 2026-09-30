# Fiesta - System Architecture

Fiesta replaces and extends parts of the electronics in a Ford Fiesta 1.8
(M)TDDI. Five firmware modules, each on its own PCB, share the car's work: the
ECU runs the engine, the others drive the dashboard, measure what the ECU does
not, keep time, and read the injection pump position. A desktop application,
the Fiesta Serial Configurator, connects to the modules over USB to read and
change their settings and to flash new firmware.

This document explains how the pieces fit together. Setup and builds are in
[`README.md`](README.md); the details of each part live next to its code.

## System overview

```text
                 CAN0 (main)
   ┌───────────┐ ◄──────────► Clocks          dashboard gauges, TFT, buzzer
   │           │ ◄──────────  OilAndSpeed     oil pressure, wheel speed, EGT
   │    ECU    │ ◄──────────  Fiesta_clock    RTC time, 1 Hz
   │  RP2040,  │
   │ two cores │ ◄── I²C ───  Adjustometer    VP37 pump position (0x57)
   │           │ ─── I²C ───► PCF8574         relays (0x38)
   │           │ ◄── UART ──  GPS receiver
   │           │ ─── CAN1 ──► OBD-II port
   └─────┬─────┘ ◄─ ADC, GPIO, PWM ─► sensors and actuators
         │
         │ USB CDC, off-vehicle
         ▼
   Fiesta Serial Configurator (Linux, GTK-4)
     also connects to Clocks, OilAndSpeed and Fiesta_clock
```

The ECU is the hub. It is the only module that controls the engine. The
MISRA-C requirements cover it, the shared VP37 module and the Adjustometer;
the other modules either feed the ECU data or display what it publishes.

## Hardware platform

All modules run on RP2040 boards: Raspberry Pi Pico, RP2040-Plus, or
RP2040-Zero. Each module picks its board in
`.vscode/jaszczurhal.project.json`. RP2040 was chosen because it is cheap and
easy to get, has two cores, flexible GPIO interrupts and PWM, and flash that
can emulate EEPROM for DTCs and settings. The firmware talks to the hardware
through JaszczurHAL, so moving a module to another MCU means porting the HAL
backend, not rewriting the module.

Schematics, layouts, and connector maps for every board are in
[`Fiesta_pcbs/`](Fiesta_pcbs/). [`pinout.txt`](Fiesta_pcbs/pinout.txt) maps
the 104-pin ECU connector and [`wirings.txt`](Fiesta_pcbs/wirings.txt) lists
the loom colours.

## Modules

| Module | Language | Role | MISRA |
|---|---|---|---|
| [`ECU`](src/ECU/) | C | engine control, diagnostics, actuators | required; `src/common/vp37` gated at zero findings, the rest a screening snapshot |
| [`Clocks`](src/Clocks/) | C++ | instrument cluster | no |
| [`OilAndSpeed`](src/OilAndSpeed/) | C++ | oil pressure, wheel speed, exhaust gas temperature | no |
| [`Fiesta_clock`](src/Fiesta_clock/) (`RTC_Clock`) | C | real-time clock, time broadcast on CAN | no |
| [`Adjustometer`](src/Adjustometer/) | C | VP37 pump position feedback, I²C slave | required; gated at zero findings |
| [`VP37TestBench`](src/VP37TestBench/) | C, C++ | VP37 pump test stand: the shared drive module on bench hardware | the shared `src/common/vp37` code is gated; the bench's own files are not scanned |
| [`SerialConfigurator`](src/SerialConfigurator/) | C, GTK-4 | desktop configuration and flashing | no |

The list itself lives in [`modules.json`](modules.json): each module's
kind, token, SerialConfigurator slot and host-test build. Adding a module
starts there. [`scripts/fiesta_modules.py`](scripts/fiesta_modules.py) checks
the list and generates the token header in `src/common/scDefinitions`, the
SerialConfigurator module table and a shell fragment; the VS Code generator,
`runalltests.sh`, `runmefirst.sh`, the bootstrap and SerialConfigurator read
those instead of keeping their own lists. The SerialConfigurator slot index is
explicit because the saved flash paths are keyed by it. CI workflow lists stay
literal, and a test checks them against the registry.

[`legacy/`](legacy/) keeps the retired `DPF_main`, `AdaptiveLights`, and
`Fading` sources for reference. They are not built or tested.

### ECU

The ECU controls fuel injection through the VP37 pump, boost through the N75
solenoid, glow plugs, the fan, the block heater, and the heated windshield. It
also stores DTCs and answers OBD-II requests, presenting itself as a Ford
Fiesta 1.8 DI EEC-V ECU. It is the MISRA-C migration target, together with the shared VP37 module
and the Adjustometer, both gated at zero findings; [`MISRA.md`](MISRA.md)
describes the current state.

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
[`ecuContext.h`](src/ECU/ecuContext.h). CAN, sensors, DTC storage, GPS, OBD,
and the configurator session keep their state in file-local structs.

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

Telemetry revision 81 exposes `cen` (enabled), `cuse` (matched fresh capture),
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

| File | Responsibility |
|---|---|
| [`start.c`](src/ECU/start.c) | start-up order, soft-timer table, watchdog, both core loops |
| [`sensors.c`](src/ECU/sensors.c) | analog inputs and filtered driver-demand cache, PCF8574 outputs, I²C register reads |
| [`can.c`](src/ECU/can.c) | main CAN frames, including the RPM publisher |
| [`obd-2.c`](src/ECU/obd-2.c) | OBD CAN input and ISO-TP responses |
| [`obd_j1979.c`](src/ECU/obd_j1979.c) | SAE J1979 services and Mode 01 PIDs |
| [`obd_ford_diag.c`](src/ECU/obd_ford_diag.c) | Ford EEC-V UDS, KWP2000, and SCP services |
| [`dtcManager.c`](src/ECU/dtcManager.c) | DTC catalogue, storage, and diagnostic reads |
| [`rpm.c`](src/ECU/rpm.c) | engine speed from the Hall sensor interrupt |
| [`src/common/vp37/`](src/common/vp37/) | the VP37 module, shared by the ECU and VP37TestBench; its translation units are the list in [`vp37_sources.txt`](src/common/vp37/vp37_sources.txt), which the host-test build reads through CMake and `sync_vscode_projects.py` writes into the firmware manifests of the modules the registry marks with `sharedSources` |
| [`vp37.c`](src/common/vp37/vp37.c) | VP37 pump: start-up, the position-demand entry points, and the control cycle that calls the units below |
| [`vp37_adapter.c`](src/ECU/vp37_adapter.c) | the ECU's board services for VP37 (PWM channels, PCF8574 enable, engine-speed guard, Adjustometer bus transfer), its start-up, and `F_FUEL_TEMP`/`F_VOLTS` from the published status |
| [`vp37_adjustometer.c`](src/common/vp37/vp37_adjustometer.c) | Adjustometer frames, the diagnostic extension, and the start-up baseline wait |
| [`vp37_snapshot.c`](src/common/vp37/vp37_snapshot.c) | state published at the end of each control step and the lock-free copies of it |
| [`vp37_feedback.c`](src/common/vp37/vp37_feedback.c) | Adjustometer position and the calibration sweep |
| [`vp37_compensation.c`](src/common/vp37/vp37_compensation.c) | corrections for supply voltage, fuel temperature, and coil resistance |
| [`vp37_control.c`](src/common/vp37/vp37_control.c) | feedforward from the holding map, learned trim, PID, dead zone, and hold |
| [`vp37_current.c`](src/common/vp37/vp37_current.c) | coil current measured on the shunt |
| [`vp37_current_control.c`](src/common/vp37/vp37_current_control.c) | bounded ON-current feedback and PWM command history |
| [`vp37_tuning.c`](src/common/vp37/vp37_tuning.c) | gains, limits, and switches the bench may change, each checked against its range |
| [`vp37_telemetry.c`](src/common/vp37/vp37_telemetry.c) | control samples and bench traces, printed on core 0; bench image only |
| [`turbo.c`](src/ECU/turbo.c) | boost control from manifold pressure |
| [`engineMaps.c`](src/ECU/engineMaps.c) | N75 duty table |
| [`vp37_maps.c`](src/common/vp37/vp37_maps.c) | VP37 holding map, integral and dead-zone tapers |
| [`engineFan.c`](src/ECU/engineFan.c), [`engineHeater.c`](src/ECU/engineHeater.c), [`glowPlugs.c`](src/ECU/glowPlugs.c), [`heatedWindshield.c`](src/ECU/heatedWindshield.c) | relay outputs |
| [`engineFuel.c`](src/ECU/engineFuel.c) | fuel level |
| [`gps.c`](src/ECU/gps.c) | NMEA time and date |
| [`config.c`](src/ECU/config.c) | stored settings and the configurator session |
| [`tests.c`](src/ECU/tests.c) | the functional test suite: each bench test, what it drives, and whether the sequence and the configurator include it |
| [`tests_workers.c`](src/ECU/tests_workers.c) | what each test does: demand generators, one-shot actions and the progress they report |
| [`tests_helpers.c`](src/ECU/tests_helpers.c) | test parameters and the runner behind the bench console and the configurator's test commands, with its engine-speed interlock and keepalive; idle stubs without `ECU_FUNCTIONAL_TESTS_ENABLED` |

The tests exist only in the bench image. `Project: Build variant: bench` and
`Project: Upload variant: bench` build it into `src/ECU/.build/variants/bench`;
`Project: Build` and `Project: Upload` give the image for the car.

[`hardwareConfig.h`](src/ECU/hardwareConfig.h) assigns every pin and address.
In short:

- **I²C** at 400 kHz, ECU as master: Adjustometer at `0x57`, PCF8574 relay
  expander at `0x38`.
- **SPI** shared by two MCP2515 CAN controllers, CAN0 for the car and CAN1 for
  the OBD-II port.
- **ADC**: six analog inputs through the HC4051 multiplexer (coolant, oil and
  air temperature, throttle, fuel level, manifold pressure), the supply
  voltage, and the VP37 shunt on GPIO26, which no other peripheral may use.
  While the VP37 current scan runs it owns the converter, and the other analog
  reads come from its buffer.
- **PWM** for the VP37, N75, and DPF lamp outputs; **GPIO interrupt** for the
  engine Hall sensor; **UART** for GPS.
- **Flash-backed EEPROM** for DTCs and settings.

### Clocks

Clocks drives the instrument cluster. It listens on the main CAN bus and
produces square waves for the factory speedometer, tachometer, and oil gauge,
draws extra readouts on a TFT display, and sounds the buzzer. It only
consumes data; the ECU needs nothing from it.

| File | Responsibility |
|---|---|
| [`Cluster.cpp`](src/Clocks/Cluster.cpp) | square waves for the factory gauges |
| [`Gauge.h`](src/Clocks/Gauge.h), [`simpleGauge.cpp`](src/Clocks/simpleGauge.cpp), [`tempGauge.cpp`](src/Clocks/tempGauge.cpp), [`pressureGauge.cpp`](src/Clocks/pressureGauge.cpp) | gauge scaling |
| [`TFTExtension.cpp`](src/Clocks/TFTExtension.cpp) | TFT drawing |
| [`logic.cpp`](src/Clocks/logic.cpp) | turns CAN signals into gauge, display, and buzzer state |
| [`buzzer.cpp`](src/Clocks/buzzer.cpp), [`buzzerStrategy.cpp`](src/Clocks/buzzerStrategy.cpp) | tones and warning patterns |
| [`can.cpp`](src/Clocks/can.cpp) | CAN reception; the MCP2515 filters admit only Fiesta IDs |

The CAN controller and the TFT share one SPI bus.

### OilAndSpeed

OilAndSpeed measures what the ECU does not read itself: oil pressure from a
resistive sender, vehicle speed from the ABS pulse line, and exhaust gas
temperature before and inside the DPF through two MCP9600 amplifiers on its
own I²C bus. It sends the results on CAN for the ECU and Clocks: oil pressure
and speed every 50 ms, exhaust temperatures every 250 ms and after each
thermocouple reading. The same frames tell the receivers the module is present.

| File | Responsibility |
|---|---|
| [`oilPressure.cpp`](src/OilAndSpeed/oilPressure.cpp) | ADC reading to bar |
| [`speed.cpp`](src/OilAndSpeed/speed.cpp) | ABS pulse frequency to speed |
| [`can.cpp`](src/OilAndSpeed/can.cpp) | oil, speed, and EGT frames |
| [`config.cpp`](src/OilAndSpeed/config.cpp) | configurator session, read-only sampling intervals |

### Adjustometer

Adjustometer measures the VP37 actuator position through the frequency of a
Hartley oscillator connected to the pump sensor coils, roughly 22-37 kHz.
JaszczurHAL captures blocks of 32 periods through RP2040 PIO and DMA.
The measurement combines four blocks into a 128-period window that slides by
32 periods and averages the raw windows over one VP37 drive period, so the PWM
ripple cancels without a filter lag. The exported deviation from a calibrated
baseline is measured in Hz; ECU calibration maps it to actuator position.

Core 0 drains capture data and publishes feedback. Core 1 handles auxiliary
ADC readings, diagnostics, LED and USB logging. Adjustometer has its own
RP2040 and usually sits on the ECU board.

The I²C slave at `0x57` exposes three blocks:

| Registers | Meaning |
|---|---|
| `0x00..0x04` | Legacy pulse, supply voltage, fuel temperature and status. |
| `0x05..0x16` | Frequency, baseline, signed deviation and chip-temperature diagnostics. |
| `0x17..0x34` | Versioned 30-byte control feedback with sample number, timestamp and age. |

The active VP37 controller requires the control-feedback block. ECU checks
frame coherence and freshness; old firmware exposing only the legacy block
cannot provide this feedback. HAL freezes the register map for each I²C read.
Status bits report signal loss (`0x01`), a broken fuel sensor (`0x02`), pending
baseline (`0x04`) and supply outside its valid range (`0x08`).

Startup includes 500 ms of warm-up, baseline convergence (80 ms minimum,
250 ms force-lock), and 1000 ms of drift verification. ECU waits up to
`VP37_ADJUSTOMETER_BASELINE_WAIT_MS` (8 s) before calibrating travel. Measurement,
startup and reset details are in the
[Adjustometer README](src/Adjustometer/README.md).
Adjustometer does not take part in the configurator protocol.

### VP37TestBench

VP37TestBench runs the shared VP37 drive module (`src/common/vp37`) on the
pump test stand: its own RP2040 with an ST7796S display, a power stage that
is the twin of the ECU one (same pins, parts and values, see
[`vp37_power_stage.h`](src/common/vp37/vp37_power_stage.h)) and the
Adjustometer on the same I²C protocol. The bench adapter
([`vp37_bench_adapter.c`](src/VP37TestBench/vp37_bench_adapter.c)) provides
the board services: the two PWM channels written active low like on the ECU,
the drive enable on a plain GPIO instead of the ECU's PCF8574, and the
Adjustometer transfer without a bus mutex, because the bench I²C has one
user. The demand comes from a potentiometer on the auxiliary scan input.
Core 0 runs the timers, the display and debug output; core 1 starts the pump
and runs `VP37_process()`. The screen redraws a value only when it changes,
and a corner dot shows the Adjustometer connection the way Clocks shows its
CAN devices: steady green while frames flow, blinking red while they do not
(with the pump stopped, core 1 probes the status register once a second). Without a pump attached the start ends after the
baseline timeout and the bench keeps running, which is also its bring-up
smoke test. VP37TestBench does not take part in the configurator protocol.

### Fiesta_clock

Fiesta_clock keeps calendar time in a PCF8563 and sends it once a second as
`CAN_ID_RTC_UPDATE` (`0x130`). It stays silent while the RTC reports lost
integrity. The configurator can read and set the date and time; a commit is
rejected unless the whole date is valid, leap years included. The module also
shows the time, temperatures from two DS18B20 sensors, and the supply voltage
on its own display.

### Serial Configurator

The configurator is a desktop application for Linux, written in C with GTK-4.
It finds Fiesta modules on USB, shows their identity, reads and writes their
parameters, runs the functional tests of an ECU test build, and flashes
firmware after checking the build manifest. A CLI
exposes the same functions.

It has two layers. The core library handles serial ports, the protocol,
authentication, parameters, and flashing, and has no GUI code. The GTK shell
only presents; it never opens a port or parses a frame. Platform-specific
code is limited to device enumeration, hot-plug, finding the UF2 drive,
config file location, and packaging. Linux is the working platform. Windows
10/11 is the intended second one, but the serial transport is still POSIX
only.

Commands, authentication, and signature status are described in the
[Serial Configurator README](src/SerialConfigurator/README.md).

## Shared code

- **[JaszczurHAL](https://github.com/jaszczurtd/JaszczurHAL)** is a separate
  repository pinned as a submodule in `src/JaszczurHAL`. Fiesta records the
  exact HAL commit; `runmefirst.sh` initializes that revision. It
  provides the hardware layer (GPIO, ADC, PWM, I²C, SPI, CAN, timers), soft
  timers, PID, a key-value store on emulated EEPROM, logging, and a mock
  backend that lets the module code build and run in host tests. Each module
  implements its `app_start()` and `app_task0()` entry points, plus
  `app_task1()` when it uses the second core.
- **[`canDefinitions`](src/common/canDefinitions/canDefinitions.h)** holds
  every CAN ID, frame layout, and scaling used between modules.
- **[`scDefinitions`](src/common/scDefinitions/)** holds the configurator
  protocol shared by the firmware modules and the desktop application. The
  protocol is described in
  [`PROTOCOL.md`](src/common/scDefinitions/PROTOCOL.md). Firmware modules run
  their configurator session through `sc_config_session_t`
  ([`sc_command_handlers.h`](src/common/scDefinitions/sc_command_handlers.h)):
  it starts the serial session, the SC command service and the adapter
  between them, so a module's `config` only lists its parameters, values and
  callbacks.

Most modules use the same layout. `Fiesta_clock` keeps older file names
(`main.c`, `RTC.c`) but the same shared pieces. `VP37TestBench` enters
through the HAL app-entry hooks in `logic.cpp` instead of `start.c` and has
no CAN or configurator session.

```text
src/<Module>/
├── start.{c,cpp}/.h       # entry points, soft-timer table, watchdog
├── hardwareConfig.h       # pins and addresses
├── hal_project_config.h   # enabled HAL features
├── config.{c,cpp}/.h      # settings and the configurator session
├── can.{c,cpp}/.h         # CAN frames
├── <domain logic files>
├── CMakeLists.txt         # host tests
├── tests/                 # host unit tests, compiled as C++
└── .vscode/               # jh-vscode manifest and tasks
```

## Communication

### CAN

The ECU sits on two separate CAN buses:

| Bus | Members | Purpose |
|---|---|---|
| CAN0 "main" | ECU, Clocks, OilAndSpeed, Fiesta_clock | traffic between modules |
| CAN1 "OBD-2" | ECU, OBD-II port | external diagnostic tools |

On CAN0 the ECU publishes engine state, boost, fuel, DTCs, and GPS time, and
reads oil pressure, wheel speed, and EGT. Clocks only listens. OilAndSpeed
and Fiesta_clock mostly transmit. On CAN1 the ECU answers OBD-II and UDS
requests from whatever tool is connected. A bus-off controller or a refused
transmit raises U0073; its failure type byte (`DTC_DETAIL_*`) names the frame
that failed, and the UDS DTC record carries it as the third DTC byte.

### I²C

The ECU is the master on its bus, with Adjustometer at `0x57` and the PCF8574
relay expander at `0x38`. OilAndSpeed and Fiesta_clock run separate I²C buses
for their own sensors.

### USB

ECU, Clocks, OilAndSpeed, and Fiesta_clock each show up as a separate USB
device named `Fiesta <Module>` with the board's unique ID as serial number.
The configurator uses these names to find the right module even when several
boards are connected. The protocol on that link, the identity rules, and the
flashing sequence are in
[`PROTOCOL.md`](src/common/scDefinitions/PROTOCOL.md).

## Vehicle interfaces

| Direction | Signals |
|---|---|
| Sensors to ECU | coolant, oil, and intake air temperature; fuel level; throttle; manifold pressure; engine RPM (Hall); heated-window button; supply voltage |
| Sensors to OilAndSpeed | oil pressure, ABS wheel speed, EGT before and inside the DPF |
| ECU to vehicle | VP37 pump (PWM and enable relay), N75 boost solenoid, glow plugs and their lamp, fan, block heater high/low, heated windshield left/right, DPF lamp |
| Clocks to driver | speedometer, tachometer, and oil gauge (frequency inputs), TFT display, buzzer |
| Diagnostics | OBD-II port on CAN1 |
| Auxiliary | GPS date and time on the ECU UART, republished on CAN; RTC time from Fiesta_clock |

## Persistence

Modules that store settings keep them in the JaszczurHAL key-value store on
flash-backed EEPROM (`ECU_EEPROM_SIZE_BYTES` on the ECU). The ECU also stores
its DTCs there; a mutex keeps core-1 reads from racing core-0 writes.

## Builds and CI

- **Host tests.** Each module builds a Unity test binary with the HAL mock.
  [`runalltests.sh`](runalltests.sh) runs them together with cppcheck,
  Valgrind, clang-tidy and the PMD CPD duplicate gate
  ([`scripts/fiesta_cpd.py`](scripts/fiesta_cpd.py): production code within
  each module, host tests and Python across the repository). Test helpers
  shared by modules live in [`src/common/tests/`](src/common/tests/). No
  hardware is needed.
- **Firmware.** `jh-vscode` from JaszczurHAL builds each module with the Pico
  SDK and writes `.build/firmware.uf2` with a checked
  `.build/firmware.manifest.json`. Each build also sets the module's USB
  name. ECU and Adjustometer compile with `-Werror`.
- **Desktop.** [`desktop-build.sh`](src/SerialConfigurator/scripts/desktop-build.sh)
  builds, tests, and packages the configurator.

| Workflow | Runs on | Does |
|---|---|---|
| [`ecu-tests.yml`](.github/workflows/ecu-tests.yml) | changes in ECU, shared code or HAL pin | ECU build, tests, cppcheck, Valgrind, clang-tidy |
| [`clocks-tests.yml`](.github/workflows/clocks-tests.yml), [`oilandspeed-tests.yml`](.github/workflows/oilandspeed-tests.yml), [`adjustometer-tests.yml`](.github/workflows/adjustometer-tests.yml) | changes in the module, shared code or HAL pin | build, tests, Valgrind, clang-tidy |
| [`firmware-build-scripts.yml`](.github/workflows/firmware-build-scripts.yml) | firmware, shared code or HAL pin changes | release and debug firmware for the five CI modules (`VP37TestBench` opts out through `"ci": false`) |
| [`serial-configurator-tests.yml`](.github/workflows/serial-configurator-tests.yml) | changes in SerialConfigurator, shared code or HAL pin | GUI and CLI build, tests, Valgrind, clang-tidy |
| [`ecu-cppcheck.yml`](.github/workflows/ecu-cppcheck.yml) | manual | cppcheck against [`cppcheck-baseline.log`](src/ECU/cppcheck-baseline.log) |
| [`ecu-misra.yml`](.github/workflows/ecu-misra.yml) | manual | MISRA screening report |

A systemd timer in [`src/ECU/scripts/systemd/`](src/ECU/scripts/systemd/) can
run the whole setup and build daily on a Raspberry Pi and email the result.
[`runmefirst.sh`](runmefirst.sh) sets up a fresh machine; the README lists
its steps and options.

## Repository layout

```text
Fiesta/
├── README.md                    # overview, setup, builds
├── ARCHITECTURE.md              # this file
├── MISRA.md                     # MISRA-C status and policy
├── modules.json                 # module registry
├── scripts/                     # registry, VS Code and duplicate-gate tools, git hooks
├── .github/workflows/           # CI
├── src/
│   ├── ECU/ Clocks/ OilAndSpeed/ Fiesta_clock/ Adjustometer/ VP37TestBench/
│   ├── JaszczurHAL/             # pinned HAL submodule
│   ├── SerialConfigurator/      # desktop application and CLI
│   └── common/
│       ├── canDefinitions/      # CAN IDs and frame layouts
│       ├── cmake/               # CMake helpers shared by module builds
│       ├── scDefinitions/       # configurator protocol
│       ├── scripts/             # manifest, UF2, and module-name helpers
│       ├── tests/               # host-test helpers shared by modules
│       └── vp37/                # the VP37 drive module and its tests
├── Fiesta_pcbs/                 # schematics, layouts, connector maps
├── materials/                   # datasheets, reference documents, photos
└── legacy/                      # retired modules
```

## Where to look next

- Frame IDs and layouts:
  [`canDefinitions.h`](src/common/canDefinitions/canDefinitions.h).
- Pins and addresses: each module's `hardwareConfig.h`.
- Start-up order and timing: each module's `start.{c,cpp}`.
- Connector pinout and wire colours:
  [`Fiesta_pcbs/pinout.txt`](Fiesta_pcbs/pinout.txt),
  [`Fiesta_pcbs/wirings.txt`](Fiesta_pcbs/wirings.txt).
- MISRA rule status: the `ecu-misra.yml` report and
  [`src/ECU/misra/`](src/ECU/misra/).
- JaszczurHAL internals: the JaszczurHAL repository.

### Control-loop RAM budget

The ECU retains current and supply samples in a two-channel ring; the sensor
multiplexer remains part of the three-channel DMA scan. Current reduction
and edge tracking use `HAL_RAM_FUNC` to limit instruction fetches from flash.
The bench trace retains 1024 complete control samples, with its boolean flags
stored as bits. `VP37_TRACE_SAMPLES` controls capture length. Inspect both
firmware variants' ELF memory maps when changing these buffers or RAM code.
