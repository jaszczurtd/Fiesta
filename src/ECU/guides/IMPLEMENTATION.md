# ECU implementation

[Polski](IMPLEMENTATION.pl.md) · [ECU setup](../README.md)

## Execution and synchronization

The work is split between the cores:

- **Core 0** runs the main loop: a soft-timer table, CAN traffic, OBD-II, the
  relay outputs, DTC storage, and the Serial Configurator session.
- **Core 1** runs the time-critical engine path: the VP37 control cycle, the
  RPM interrupt, and turbo control.

There is no RTOS. `start.c` installs a table of `(period, callback)` pairs
that the core-0 loop calls when due, from about 10 ms for fast sensor reads to
one second for housekeeping. The VP37 pump belongs to core 1. Its control
steps run on a fixed grid of `pidTimeUpdate` periods: a step that starts late
does not move the next deadline, and a step a whole period late starts a new
grid instead of a burst of catch-up steps. Within `CORE1_VP37_GUARD_US` before
a deadline core 1 starts no other work (turbo, RPM, the test demand) and waits
for the deadline from RAM, so that work cannot delay the next control step.
Once the deadline is reached, core 1 also services demand and stop commands.
This keeps an overloaded loop responsive when the requested period is shorter
than the work; it does not make that period achievable.

The PWM backend cannot produce exactly the period of the control grid, so a
free grid would slide through the whole PWM period (about 40 µs per second at
200 Hz) and a command would wait anything from nothing to 5 ms for its latch.
The grid therefore follows the latch that the current scan measures: each step
moves the next deadline by at most `VP37_LATCH_LOCK_STEP_US`, so that the PWM
write lands `VP37_LATCH_LOCK_LEAD_US` before the latch. The lock also learns
the period difference and keeps applying it while the actuator is released and
no latch is measured. It works only while the control period equals the PWM
period; with another bench period or with the current scan off the grid runs without latch synchronization.

The bench tests record their step announcements on core 1 and core 0 prints
them, together with acknowledgements of default resets. A changed test demand
waits for its announcement, at most
`STEP_EVENT_APPLY_TIMEOUT_MS`, so the log still shows the announcement before
the move it announces. Every control step ends by publishing its state behind
a sequence counter, and core 0 copies that snapshot without a lock. The published words are written and read only
with relaxed atomic accesses, so an overlapping copy is discarded, never a
data race, and the control loop never waits for the
telemetry or for the global values it feeds. The bench telemetry waits on core
0 for the end of a step before it formats a report: code fetched from flash
by core 0 during a step made the step about 300 µs longer on average. The
bench trace buffer uses one atomic state to transfer ownership between capture
and output. The I²C bus with the PCF8574 output latch and
the DTC manager with its storage sit behind mutexes. Flash writes stay on
core 0.

The main controllers (VP37, turbo, RPM, fan, heater, glow plugs, windows, and
engine state) live in one `ecu_context_t` in
[`ecuContext.h`](../../../src/ECU/ecuContext.h). CAN, sensors, DTC storage, RTC time, OBD,
and the configurator session keep their state in file-local structs.

## Engine speed

The crank input supplies 32 equally spaced rising edges per revolution. The
ISR records microsecond timestamps; it rejects periods shorter than 94 µs
(the 20000 rpm capture ceiling) without moving the last accepted edge. This
ceiling is separate from the engine's 5000 rpm protection threshold.

On core 1, `RPM_process()` briefly masks local interrupts to copy nine
timestamps. It calculates RPM from the newest one to eight periods within
25 ms, using one period when it is longer than that window. The first edge
starts acquisition and the second gives a measurement. Integer rounding has
no additive correction, and the result is not clamped at the engine limit,
so the VP37 interlock can detect overspeed. Core 0 reads the published RPM
with an atomic scalar load.

Signal loss expires after three times the latest accepted period, bounded
to 20-500 ms. The first edge gets 500 ms for acquiring the second one. On
expiry the result is zero and the history is discarded; restarting requires
two new edges. Time differences handle the 32-bit microsecond counter wrap.
This measures speed on an evenly spaced wheel; it does not decode crank
phase or missing teeth.

## Control-loop RAM budget

The ECU retains current and supply samples in a two-channel ring; the sensor
multiplexer remains part of the three-channel DMA scan. Current reduction
and edge tracking use `HAL_RAM_FUNC` to limit instruction fetches from flash.
The bench trace retains 1024 complete control samples, with its boolean flags
stored as bits. `VP37_TRACE_SAMPLES` controls capture length. Inspect both
firmware variants' ELF memory maps when changing these buffers or RAM code.

## Source map

| File | Responsibility |
|---|---|
| [`start.c`](../../../src/ECU/start.c) | start-up order, soft-timer table, watchdog, both core loops |
| [`sensors.c`](../../../src/ECU/sensors.c) | analog inputs and filtered driver-demand cache, PCF8574 outputs, I²C register reads |
| [`can.c`](../../../src/ECU/can.c) | main CAN frames, including the RPM publisher |
| [`obd-2.c`](../../../src/ECU/obd-2.c) | OBD CAN input and ISO-TP responses |
| [`obd_j1979.c`](../../../src/ECU/obd_j1979.c) | SAE J1979 services and Mode 01 PIDs |
| [`obd_ford_diag.c`](../../../src/ECU/obd_ford_diag.c) | Ford EEC-V UDS, KWP2000, and SCP services |
| [`dtcManager.c`](../../../src/ECU/dtcManager.c) | DTC storage and diagnostic reads; the codes it registers are the aliases in [`dtc_codes.h`](../../../src/ECU/dtc_codes.h), and their descriptions come from the shared catalogue |
| [`src/common/obdDefinitions/`](../../../src/common/obdDefinitions/) | the DTC catalogue shared by the modules: the generic SAE J2012 codes and the project's U19xx codes with their descriptions, the description lookup and the printed form (`P0116`); its translation units are the list in [`obdDefinitions_sources.txt`](../../../src/common/obdDefinitions/obdDefinitions_sources.txt), read like the VP37 list below |
| [`rpm.c`](../../../src/ECU/rpm.c) | engine speed from the Hall sensor interrupt |
| [`src/common/vp37/`](../../../src/common/vp37/) | the VP37 module, shared by the ECU and VP37TestBench; its translation units are the list in [`vp37_sources.txt`](../../../src/common/vp37/vp37_sources.txt), which the host-test build reads through CMake and `sync_vscode_projects.py` writes into the firmware manifests of the modules the registry marks with `sharedSources` |
| [`vp37.c`](../../../src/common/vp37/vp37.c) | VP37 pump: start-up, the position-demand entry points, and the control cycle that calls the units below |
| [`vp37_adapter.c`](../../../src/ECU/vp37_adapter.c) | the ECU's board services for VP37 (PWM channels, PCF8574 enable, engine-speed guard, Adjustometer bus transfer), its start-up, `F_FUEL_TEMP`/`F_VOLTS` from the published status, the fuel temperature validity for CAN and OBD, and U190A after a lasting fuel sensor fault |
| [`vp37_adjustometer.c`](../../../src/common/vp37/vp37_adjustometer.c) | Adjustometer frames, the diagnostic extension, and the start-up baseline wait |
| [`vp37_snapshot.c`](../../../src/common/vp37/vp37_snapshot.c) | state published at the end of each control step and the lock-free copies of it |
| [`vp37_feedback.c`](../../../src/common/vp37/vp37_feedback.c) | Adjustometer position and the calibration sweep |
| [`vp37_compensation.c`](../../../src/common/vp37/vp37_compensation.c) | corrections for supply voltage, fuel temperature, and coil resistance |
| [`vp37_control.c`](../../../src/common/vp37/vp37_control.c) | feedforward from the holding map, learned trim, PID, dead zone, and hold |
| [`vp37_current.c`](../../../src/common/vp37/vp37_current.c) | coil current measured on the shunt |
| [`vp37_current_control.c`](../../../src/common/vp37/vp37_current_control.c) | bounded ON-current feedback and PWM command history |
| [`vp37_tuning.c`](../../../src/common/vp37/vp37_tuning.c) | gains, limits, and switches the bench may change, each checked against its range |
| [`vp37_telemetry.c`](../../../src/common/vp37/vp37_telemetry.c) | control samples and bench traces, printed on core 0; bench image only |
| [`turbo.c`](../../../src/ECU/turbo.c) | boost control from manifold pressure |
| [`engineMaps.c`](../../../src/ECU/engineMaps.c) | N75 duty table |
| [`vp37_maps.c`](../../../src/common/vp37/vp37_maps.c) | VP37 holding map, integral and dead-zone tapers |
| [`engineFan.c`](../../../src/ECU/engineFan.c), [`engineHeater.c`](../../../src/ECU/engineHeater.c), [`glowPlugs.c`](../../../src/ECU/glowPlugs.c), [`heatedWindshield.c`](../../../src/ECU/heatedWindshield.c) | relay outputs |
| [`engineFuel.c`](../../../src/ECU/engineFuel.c) | fuel level |
| [`rtcTime.c`](../../../src/ECU/rtcTime.c) | UTC from the `0x130` frame of Fiesta_clock: it dates the first occurrence of a DTC and the watchdog reboot report, and counts as unknown after 3 s without a trusted frame |
| [`config.c`](../../../src/ECU/config.c) | stored settings and the configurator session |
| [`tests.c`](../../../src/ECU/tests.c) | the functional test suite: each bench test, what it drives, and whether the sequence and the configurator include it |
| [`tests_workers.c`](../../../src/ECU/tests_workers.c) | what each test does: demand generators, one-shot actions and the progress they report |
| [`tests_helpers.c`](../../../src/ECU/tests_helpers.c) | ECU parameters, VP37 commands, demand policy and engine-speed interlock for the [shared functional-test runner](../../common/test_runner/README.md); idle stubs without `ECU_FUNCTIONAL_TESTS_ENABLED` |
