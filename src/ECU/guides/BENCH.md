# Working with the ECU on the bench

[Polski](BENCH.pl.md) · [ECU setup](../README.md)

Complete the [setup procedure](../README.md) before using these commands. This page covers
the firmware variant, test-session rules, hardware interfaces and data capture.

## Commands by task

Send commands through the console that owns the port. Do not open another monitor alongside active capture.

| Task | Command | Result / condition |
| --- | --- | --- |
| Inspect tests and parameters | `list`, `params` | Read-only; does not start a test. |
| Supervise a console session | `watch <ms>` | 1000-600000 ms; every line renews it. Disabled after restart. |
| End a test | `stop` | Ends the test and releases its outputs; ECU clears test demand. |
| Stop VP37 drive | `X` | Stops the pump; restart ECU to initialize it again. |
| Request rest | `S0` | The controller continues running and can record steps at zero demand. |
| Record a complete run | `B2`, then `B0` | Starts and ends the stream; drain and validate its completion record. |

## Reading tuning parameters

Names explain the purpose; console identifiers select the setting. Full definitions and
ranges belong to [vp37_tuning.c](../../common/vp37/vp37_tuning.c) and
[vp37_config.h](../../common/vp37/vp37_config.h).

| Setting | Identifier | Default / range | When it affects the result |
| --- | --- | --- | --- |
| Physical travel limit | `X13` | 90%; above 0 through 100% | Maps full demand to a share of calibrated travel. Maps retain their physical-position scale. |
| Descent assistance | `X25` | Off; 0 or 1 | Research option that unwinds excess integral during descent. |
| Retained descent correction | `X26` | 0.4; 0.25-0.75 | With `X25` enabled and no learned correction for the target. A dimensionless share of correction. |

The [control guide](CONTROL.md) explains position scales, controller terms and the dependencies of these options.

## Bench image and hardware

The tests exist only in the bench image, the `BENCH` variant that
[`hal_project_config.h`](../../../src/ECU/hal_project_config.h) declares with
`ECU_FUNCTIONAL_TESTS_ENABLED=1`. The HAL modules and settings it shares with
the host tests are in [`ecu_hal_config.h`](../../../src/ECU/ecu_hal_config.h), because
JaszczurHAL reads the variant declaration only from `hal_project_config.h`
itself. Select `Project: Build variant: BENCH` or
`Project: Upload variant: BENCH` in VS Code. Artifacts go to
`src/ECU/.build/rp2350-arm/rp2350-plus/variants/BENCH/`, or under
`.build/rp2040/rp2040-plus/` for the RP2040-Plus assembly. Tasks without a
variant build the car image. See the [build guide](../../../doc/BUILDING.md)
for CLI equivalents.

In `BENCH`, `ECU_SC_TESTS_RPM_INTERLOCK` defaults to `0`, so configurator
tests can run with RPM supplied by a signal generator. Set it to `1` to
refuse or stop configurator tests above `ECU_SC_TESTS_RPM_LIMIT` rpm.
This option does not apply to console tests; see the
[functional test rules](../../common/scDefinitions/PROTOCOL.md#functional-tests).

Console tests have an optional watch. `watch <ms>` arms it with a limit of
1000 to 600000 ms: when no console line arrives for that long, the running
console test stops with `host_lost` and the demand returns to zero, as for a
configurator test whose host went quiet. Every console line renews the watch,
`watch` alone renews it and reports the limit, `watch 0` disarms it, and a
firmware restart leaves it off. A bench tool that holds the port arms the
watch when its recording starts and renews it while it runs; a killed
collector or an unplugged cable then leaves the actuator at its last demand
only until the limit runs out. `G` keeps its auto-zero of a single `S` hold.

DTC and KV one-shot actions run on core 0. The control core continues to
service VP37 and advances the test sequence after the result is published.
Stopping a test cancels an action that has not yet been dispatched.
`run dtc` reports `ok` when U0073 (`DTC_CAN_BUS_FAULT`) is active with
the RPM transmit detail (`DTC_DETAIL_CAN_TX_RPM`), or `failed` otherwise.
The result checks runtime state immediately after injection. It does not
verify persistence across a reset. Read the code through OBD, clear the
DTCs after the test and read them again to confirm the clear.
Read-only `list` and `params` also print on the console core, after the
serial callback releases its temporary debug mute.
Test start, stop and result messages use a bounded queue to that core.
Their output never makes control wait for the debug mutex. A queue overflow is reported;
stopping the actuator still takes effect.

[`device_tree.json`](../../../src/ECU/device_tree.json) assigns every pin and address.
Its `rp2350-plus` assembly (Waveshare RP2350-Plus, the default) and
`rp2040-plus` assembly (Waveshare RP2040-Plus) share the PCB and the wiring;
`--assembly` or the VS Code assembly picker selects one. The interfaces are:

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
  **GPIO interrupt** for the engine Hall sensor.
- **Flash-backed EEPROM** for DTCs and settings.

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

## Recording calibration and control steps

During startup calibration the bench image uses the same storage for every
MIN/MAX qualification sample. After USB connects, `VP37 CAL` lines export
the input position, raw and filtered frequencies, baseline, sample number,
both clocks, freshness, status, applied PWM, supply voltage in 0.1 V and fuel
temperature in degrees C. `VP37 CAL END` reports the sample count, qualified
endpoints, validity and any buffer overflow. The current estimator's window
and timeout settings are defined in `vp37_config.h`. The normal `VP37 C`
stream shows the return to rest after calibration; calibration does not
sample current. Normal RAM capture remains busy until this export finishes.

For metrics covering a complete run, send `B2` to start a stream and `B0`
to finish it. The control core records every step in the trace buffer while
the console core drains it as `VP37 N` lines. `B` and `B1` retain their finite
capture modes; calibration and all capture modes share the same memory.
The console continues to print `VP37 C` for live supervision. It drains `N`
on every console loop, independently of the C report interval, with a 20 ms
budget per pass. A USB write already in progress can outlast that budget.
Each `N` line includes CRLF and is sent in one serial write without the
`ECU:` debug prefix, avoiding separate USB transfers for that prefix and
the line ending.

Use the MCU clock `us` and consecutive step numbers `n` for metric windows,
not the time a buffered line reaches the host. `VP37 STREAM END` gives the
capture times, sample count, first and last step numbers and `overflow`.
Reject incomplete captures, sequence gaps, mismatched counts and overflow.
The recorder never waits for USB: if the reader falls behind, recording ends
with `overflow:1`. A stopped pump also ends the stream; sending `S0` keeps
recording the control steps at rest. Drain the completion record before
starting another capture or closing the collector.

Each `N` sample includes the requested and ramped positions, feedback,
timing, R sample count (`rn`), active R compensation (`ruse`) and thermal
scale (`tsc10000`, scale times 10000). `thr10` gives demand in tenths of a
percent. The console assembles each N line before sending it to USB.
The latest current scan supplies `latch`, `lp` and `lv`;
`cmd_us` is usable only with `match:1`. These observations can precede the
recorded step and do not by themselves identify the first PWM latch after
a command. Keep the normal C-based guards active throughout the recording.
