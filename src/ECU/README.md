# ECU

Safety-critical ECU module for Ford Fiesta custom electronics.

## MISRA-C migration status

Two-level estimate:

- engineering/architecture alignment: **~75-80%**,
- formal compliance readiness (tooling + evidence): **~45-50%**.

Important:

- this module is in MISRA-C migration scope,
- CMake in this module is used for host test configuration/build,
- host test targets are compiled as C++ (`.cpp`),
- Arduino build path compiles ECU `.c` sources as C,
- final firmware link remains mixed C/C++ due to C++ HAL utilities,
- warning quality gate is enabled for host tests and Arduino ECU build paths (`-Werror`),
- full project-level C-only compilation is currently not feasible without
  replacing C++ dependencies in Arduino core/toolchain integration,
  JaszczurHAL C++ utility modules, and host C++ test path.

Current hardening/warning status:

- ECU warning fixes aligned with `-Werror` include unused-parameter cleanup in `obd-2.c` and `sensors.c`.
- CAN TX paths use full buffer zero-initialization before send.
- CAN RX callback rejects invalid frames (`NULL` buffer or `len > CAN_FRAME_MAX_LENGTH`) before payload reads.

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
