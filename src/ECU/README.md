# ECU

Safety-critical ECU module for Ford Fiesta custom electronics.

## VP37 and Adjustometer

The VP37 quantity-actuator loop reads versioned feedback from the separate
RP2040 Adjustometer at I2C address `0x57`. Its fast block contains position,
raw/filtered frequency, sample number, timestamp, age, voltage, temperature
and status. Invalid or stale feedback cannot drive the controller. The original
register layouts remain available for older readers; current ECU firmware
requires an Adjustometer with fast-block support.

ECU uses fuel temperature as a proxy for the VP37 coil temperature and scales
the complete FF+PID command relative to the warm 49°C reference. The bounded
factor is filtered after initialization; bad temperature data holds its last
value. Physical output limits are accounted for before PID integration.
This model does not correct oscillator frequency or sensor baseline.
The installed 0.22-ohm source shunt uses a holding-map multiplier of 1.08
(`VP37_PWM_FF_HARDWARE_GAIN`). It adjusts the base command before voltage and
temperature compensation; the motion term and PID gains retain their own
values. The calibrated position range remains unchanged. GPIO26 is reserved
for the shunt input; SD logging requires a different chip-select pin.
Failed communication holds PID briefly and stops drive after 20 ms;
invalid position status stops it immediately.

GPIO26 current acquisition runs on core 0 every 20 ms while the actuator is
active. It observes one complete measured PWM period and excludes 60 us around
both ON edges. `VP37 IPULSE` reports the guarded ON mean (winsorized at P95),
P95, unfiltered peak, timing, zero calibration, clipping and validity, together
with the controller snapshot preceding acquisition. These are source-shunt ON
measurements; the freewheel path bypasses the shunt. Acquisition and logging
run outside the pump mutex. Current has no path to PWM, PID or a cutoff.
Bench commands `Q0` and `Q1` disable and enable acquisition for comparison.
At 1 kHz and above, acquisition shortens the sampling delay from 20 to 4 us;
the 60 us edge guards and minimum of eight guarded samples still apply.
A short ON phase can still yield too few samples; the result remains invalid.

Supply-voltage compensation uses the fast local ECU ADC. The first valid pair
after startup or a local conversion failure scales it against the Adjustometer;
other scale updates occur only when zero demand reaches MIN, the condition used
for drive release. The Adjustometer is also the fallback if the local conversion
fails; invalid voltage from both sources selects 15 V so the fallback cannot
increase drive. A 0.5 V
sample-and-hold hysteresis suppresses
steady load-correlated ripple. Once that band is exceeded, the direct
`12 / Vc` correction follows the local input in the current control step.
Bench `V1` selects supply voltage averaged over a complete PWM period by the
current acquisition task. ON and OFF means are weighted by their durations.
An invalid result, age of 100 ms, disabled acquisition or released drive uses
the ordinary local ADC path. `V0` selects that path explicitly and is the default.

The control loop uses an explicit period and elapsed seconds. Diagnostic
snapshots contain individual P/I/D terms and the effective correction limits;
serial output runs outside the controller mutex. See the
[Adjustometer README](../Adjustometer/README.md) for the measurement path, and
the shared [I2C register map](../common/adjustometer_protocol.h).
The default gains are P=0.05, I=0.20 and D=0 with a 5 ms control period.
Integral hold requires 100 ms
continuously inside 20 Hz; a brief crossing does not freeze I. It releases after
500 ms continuously outside 40 Hz. Active voltage tracking freezes integration.
Bench `E<0..1000>` selects the hold confirmation in milliseconds; `E0` allows
comparison with immediate entry. `R` restores the default PID and confirmation.

Trace output records the clamped Adjustometer-derived voltage as `V`, the local
ADC conversion as `Vl`, the selected input before hysteresis as `Ve`, and the
voltage actually used as `Vc`. `vcor` is the applied multiplier, `vt` marks
active voltage tracking, `ih` marks settled-target integral hold, and `vp`
marks use of the complete-period supply mean. `VP37 CFG` includes `pwm_hz`.

Bench builds can override `VP37_PWM_FREQUENCY_HZ` and the four
`CYCLIC_DELAYTIME_*` values through compile definitions. Their VP37 defaults
are 200 Hz and deterministic 4, 6, 12 and 2 ms cyclic steps, with six complete
0-100-0 cycles at each speed. The 2 ms step exceeds the normal demand slew
limit and is a stress case. `START_TEST_VP37_MODE=1` selects cyclic tests; the
fixture starts at zero demand and `C` starts the complete sequence from zero.
Trace samples include the active delay as `cyms`. Hold deadlines still include
the setpoint ramp. `START_TEST_VP37_MODE=3` selects persistent serial demand:
`S<0..100>` remains active until the next `S`, `X`, or restart, and RAM traces
are available without running the cyclic profile. The header currently defaults
to mode 2 for potentiometer bench firmware; an engine build must override it with mode 0.
Mode 2 rejects isolated one-percent potentiometer steps unless they persist for
150 ms; larger changes are immediate.

## Persistent data and GPS

ECU reserves 32 KiB of flash-backed EEPROM. The KV region begins at byte 4096
and contains two 8192-byte banks; the first sector remains separate from KV.
Firmware and host tests use the layout in `hal_project_config.h`. Old KV bank
locations at 96/128 are ignored; their data is not migrated. DTC clearing
removes only DTC keys and retains stored configuration.

Parameter and DTC operations are serialized on core 0. EEPROM flash-write
callbacks pause GPS only for a physical write and resume it after the attempt,
including a write failure. This releases the PIO/DMA receiver required by the
RP flash coordinator. Reads, invalid layouts, unchanged values and RAM-only
batches leave GPS running. A failed GPS resume is logged and retried once per
second; it does not turn a successful storage commit into a failed one.

## MISRA-C migration status

This module is in MISRA-C migration scope. Repository-level migration
status, hardening details, policy, and screening entry points are
consolidated in [`MISRA.md`](../../MISRA.md).

Module-local MISRA tooling:

- runner: [`misra/check_misra.sh`](misra/check_misra.sh),
- suppressions + deviation register: [`misra/`](misra/),
- manual CI artifact workflow: `.github/workflows/ecu-misra.yml`.

Latest local screening snapshot (2026-09-12, cppcheck 2.13.0, no licensed
rule texts): 949 active findings across 33 rule IDs. This is triage evidence,
not a compliance/pass result; detailed buckets and comparison caveats are in
[`MISRA.md`](../../MISRA.md).

## Build

Native firmware build:

```bash
cd src/ECU
JH=../../../libraries/JaszczurHAL/vscode/entry/jh-vscode
"$JH" build --project "$PWD"
"$JH" build-debug --project "$PWD"
"$JH" upload --project "$PWD"
"$JH" upload-uf2 --project "$PWD"
"$JH" refresh-intellisense --project "$PWD"
```

Notes:

- `jh-vscode upload --project "$PWD"` is the same path used by the VS Code upload task / `Ctrl+Shift+2`.
- `jh-vscode upload-uf2 --project "$PWD"` is the BOOTSEL mass-storage path.
- `jh-vscode refresh-intellisense --project "$PWD"` regenerates `compile_commands.json`, `compile_commands_patched.json`, and `.vscode/c_cpp_properties.json`.
- `jh-vscode monitor --project "$PWD"` is the same path used by the VS Code monitor task / `Ctrl+Shift+3`.
- `Ctrl+Shift+9` updates `jaszczurhal.uploadPort` in `.vscode/settings.json`; identity-guarded upload still verifies the selected `/dev/serial/by-id` target before flashing.
- Fiesta-specific manifest validation remains in `src/common/scripts/`; the per-module VS Code wrappers were removed.

Host tests (CMake path):

```bash
cmake -S src/ECU -B src/ECU/build_test -DCMAKE_BUILD_TYPE=Release
cmake --build src/ECU/build_test --parallel
ctest --test-dir src/ECU/build_test --output-on-failure
```

MISRA screening:

```bash
cd src/ECU
bash misra/check_misra.sh --out misra/.results
```

Notes:

- the repository does not ship licensed MISRA Appendix A rule texts,
- if a local licensed rule-text extract is available, pass it with `--rule-texts /absolute/path/to/file` to improve message quality and severity breakdown.
