# ECU

Safety-critical ECU module for Ford Fiesta custom electronics.

## MISRA-C migration status

Two-level estimate:

- engineering/architecture alignment: **~80-85%**,
- formal compliance readiness (tooling + evidence): **~55-60%**.

Important:

- this module is in MISRA-C migration scope,
- CMake in this module is used for host test configuration/build,
- host test targets are compiled as C++ (`.cpp`),
- Arduino build path compiles ECU `.c` sources as C,
- final firmware link remains mixed C/C++ due to C++ HAL utilities,
- warning quality gate is enabled for host tests and Arduino ECU build paths (`-Werror`),
- project-local MISRA screening runner is available under `misra/check_misra.sh`,
- manual MISRA artifact workflow is available in `.github/workflows/ecu-misra.yml`,
- deviation tracking is bootstrapped in `misra/deviation-register.md`,
- full project-level C-only compilation is currently not feasible without
  replacing C++ dependencies in Arduino core/toolchain integration,
  JaszczurHAL C++ utility modules, and host C++ test path.

Current hardening/warning status:

- ECU warning fixes aligned with `-Werror` include unused-parameter cleanup in `obd-2.c` and `sensors.c`.
- CAN TX paths use full buffer zero-initialization before send.
- CAN RX callback rejects invalid frames (`NULL` buffer or `len > CAN_FRAME_MAX_LENGTH`) before payload reads.
- Multi-core state access tightened in `sensors.c` and `dtcManager.c`:
  adjustometer readout uses snapshot semantics under a dedicated mutex,
  `getVP37Adjustometer()` migrated to an out-parameter API so callers own
  the destination storage, PCF8574 shadow-latch RMW moved inside the I2C
  bus mutex, and `dtcManager` state is guarded by `dtcManagerMutex` with
  KV persistence performed outside the critical section via a snapshot
  captured under the mutex.
- `readHighValues()` no longer maintains its own change-detection cache
  (`reflectionValueFields` removed): `CAN_sendThrottleUpdate()` and
  `CAN_sendTurboUpdate()` already self-gate on `s_canState.last*Sent`,
  so the outer layer was redundant, racy, and broadcast both updates on
  any field change. Behavioural note: after a soft reset the first
  `readHighValues` tick now emits one throttle + one turbo frame
  immediately (previously suppressed by the NOINIT cache), refreshing
  CAN consumers that would otherwise hold a stale pre-reset value.
- Latest project-local MISRA screening snapshot (2026-04-21) reports 787 active findings; use it as a triage baseline, not as a clean-status indicator.
- Rule 10.4 dropped to 95 findings after recent `obd-2.c` cleanup and additional OBD/UDS regression tests.

## Build

Firmware build (Arduino path):

```bash
cd src/ECU
bash scripts/upload-uf2.sh
```

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
