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
- Initial project-local MISRA screening run reports 976 active findings across 27 rule IDs; use it as a triage baseline, not as a clean-status indicator.

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
