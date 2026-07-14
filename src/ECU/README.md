# ECU

Safety-critical ECU module for Ford Fiesta custom electronics.

## VP37 and Adjustometer

The VP37 quantity-actuator loop uses the separate RP2040 Adjustometer module at
I2C address `0x57`. The controller keeps the original five-byte feedback block
(`PULSE`, voltage, fuel temperature, and status) and reads an optional
version-1 diagnostic extension separately. The extension reports filtered raw
oscillator frequency, locked baseline, signed `signalHz - baselineHz`, RP2040
die temperature, and validity metadata. Its failure does not affect the legacy
`commOk` state or PWM feedback path.

Current ECU-side thermal handling uses Adjustometer fuel temperature as the
closest available proxy for VP37 actuator-coil temperature. Above the `22 °C`
reference, only positive PID correction authority is expanded according to the
copper-resistance estimate, from the cold `+220` limit up to `+340` PWM counts.
Negative authority remains `-220`. Invalid temperature/status or lost
communication falls back to the cold limit. This compensates actuator-force
loss; it does not modify the measured oscillator signal or baseline.

VP37 diagnostics are emitted every `500 ms` as separate controller and sensor
lines so the raw Adjustometer data is not hidden at the end of a long control
record:

```text
ECU: VP37 thr:... des:... adj:... pwm:... err:... ff:... corr:... lim+:... sat+:... nom:...
ECU: VP37 ADJ p:... f:...Hz d:... v:... ft:... tc:... s:... bl:... ext:... fl:0x..
```

See the detailed
[VP37/Adjustometer context](doc/Fiesta-context-providers/vp37-adjustometer-context-provider.txt),
the [Adjustometer README](../Adjustometer/README.md), and the shared
[I2C register map](../common/adjustometer_protocol.h).

## MISRA-C migration status

This module is in MISRA-C migration scope. Repository-level migration
status, hardening details, policy, and screening entry points are
consolidated in [`MISRA.md`](../../MISRA.md).

Module-local MISRA tooling:

- runner: [`misra/check_misra.sh`](misra/check_misra.sh),
- suppressions + deviation register: [`misra/`](misra/),
- manual CI artifact workflow: `.github/workflows/ecu-misra.yml`.

Latest local screening snapshot (2026-07-10, cppcheck 2.13.0, no licensed
rule texts): 1026 active findings across 33 rule IDs. This is triage evidence,
not a compliance/pass result; detailed buckets and comparison caveats are in
[`MISRA.md`](../../MISRA.md).

## Build

Firmware build (Arduino path):

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
