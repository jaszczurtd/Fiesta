# Fiesta

Firmware ecosystem for Ford Fiesta 1.8 (M)TDDI custom electronics.

The repository contains multiple embedded applications, hardware assets, and validation materials used to build a complete vehicle electronics stack.

Gallery:
https://postimg.cc/gallery/pHF4jy2

## Safety-First Engineering

Safety is treated as a top-level requirement.

- `src/ECU` is the safety-critical module and is being aligned with MISRA-C.
- Defensive coding and runtime hardening are prioritized over shortcuts.
- Coverage is expanded continuously (host tests + firmware build validation).
- Safety documentation is kept synchronized with code changes.

## Repository scope

Fiesta is a multi-module system, not a single firmware binary.

## Module layout

Active modules in `src/`:

- `src/ECU` - engine control logic, diagnostics (OBD/UDS over CAN), DTC manager, actuator logic.
- `src/Clocks` - dashboard/cluster rendering and signaling.
- `src/OilAndSpeed` - dedicated oil pressure and speed module.

Deprecated code in `legacy/`:

- old/archival sources that are not the primary development target,
- useful for reference and migration history,
- should not be treated as current production firmware.

## Current status (2026-04-12)

- Primary firmware modules compile with the current HAL (`src/ECU`, `src/Clocks`, `src/OilAndSpeed`).
- ECU host-side test suite currently passes (`11/11` suites, including `test_cppcheck`).
- ECU CI now runs cppcheck as part of standard test execution (`ctest`) and includes baseline gating in GitHub Actions.
- ECU startup reports compile timestamp (`__DATE__` + `__TIME__`).

## ECU MISRA-C migration status

Two-level estimate:

- engineering/architecture alignment: **~80-85%**,
- formal compliance readiness (tooling + evidence): **~50-55%**.

Scope:

- `src/ECU` is in scope for MISRA-C migration,
- `src/Clocks` and `src/OilAndSpeed` are currently out of MISRA scope.

Completed areas include:

- class-to-struct migration for core ECU modules,
- central state ownership (`ecu_context_t`),
- HAL C wrappers for PID and soft timers,
- `extern "C"` guards in public ECU headers,
- ECU source migration to `.c` files,
- Arduino build path compiles ECU `.c` sources as C while final firmware link remains mixed C/C++,
- state consolidation in ECU modules (`engineFuel`, `dtcManager`, `gps`, `sensors`, `can`, `start`, `obd-2`),
- explicit `HAL_TOOLS_*` config migration (legacy aliases retained in HAL),
- targeted runtime hardening (bounds checks, watchdog snapshot guard, mutex guards, regression tests).
- warning quality gate for ECU host tests and Arduino ECU build paths (`-Werror`).
- warning cleanups required by the quality gate (unused-parameter fixes in ECU and aligned external HAL dependency).
- defensive CAN updates currently applied in ECU: TX buffers are zero-initialized before send, RX path rejects invalid `NULL`/oversized frames.

Pending areas:

- full C linkage path for required HAL/tool APIs,
- replacement of C++ dependencies (Arduino core/HAL/test path) if full project-level C-only build is required,
- MISRA hardening pass (casts, bounds, overflow, naming, volatile/mutex review).

## MISRA documentation policy (mandatory)

For each MISRA-related change, update safety status in `README.md` (this file)
in the same change set.

Project-specific working notes can be kept locally, but repository-level safety
status in this file must remain synchronized with code changes.

## Dependencies

The project uses a single shared external libraries location.

Required libraries:

- `JaszczurHAL` (HAL and utility layer): https://github.com/jaszczurtd/JaszczurHAL
- `canDefinitions` (shared CAN IDs/signals): https://github.com/jaszczurtd/canDefinitions

Expected layout:

```text
<sketchbook>/libraries/JaszczurHAL
<sketchbook>/libraries/canDefinitions
```

Submodule-based dependency flow is no longer used.

## Build and development

Each module is an Arduino-style app (`*.ino` + companion sources).

Helper scripts are available in module-specific `scripts/` directories:

- `select-board.sh`
- `upload-uf2.sh`
- `refresh-intellisense.sh`
- `serial-monitor.sh` / `serial-monitor.py` (where available)

### Linux/WSL setup (shared for ECU/Clocks/OilAndSpeed)

Install Arduino CLI and RP2040 core once per machine:

```bash
arduino-cli config init
arduino-cli config set board_manager.additional_urls https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
arduino-cli core update-index
arduino-cli core install rp2040:rp2040
```

Set `arduino.sketchbookPath` in module `.vscode/settings.json` to the directory that contains `libraries/`.

Example:

```json
"arduino.sketchbookPath": "/home/youruser"
```

### ECU host tests (CMake)

Note: CMake in this repository is used for host test configuration/build, and
test targets are compiled as C++ (`.cpp`).

```bash
cmake -S src/ECU -B src/ECU/build_test -DCMAKE_BUILD_TYPE=Release
cmake --build src/ECU/build_test --parallel
ctest --test-dir src/ECU/build_test --output-on-failure
```

### Firmware build examples

ECU:

```bash
cd src/ECU
bash scripts/upload-uf2.sh
```

Clocks:

```bash
cd src/Clocks
bash scripts/upload-uf2.sh
```

OilAndSpeed:

```bash
cd src/OilAndSpeed
bash scripts/upload-uf2.sh
```

## Hardware and materials

- PCB projects and variants: `Fiesta_pcbs/`
- wiring/pinout notes: `Fiesta_pcbs/pinout.txt`, `Fiesta_pcbs/wirings.txt`
- supporting docs/graphics/examples: `materials/`

## Credits

- DS18B20 library origin: Davide Gironi.
- `PCF8563.c` created 2014-11-18 by Jakub Pachciarek.

## Photos

![Display](https://github.com/jaszczurtd/Fiesta/blob/main/materials/imgs/display.JPG?raw=true)

![ECU](https://github.com/jaszczurtd/Fiesta/blob/main/materials/imgs/ecu.jpg?raw=true)

![In-car](https://github.com/jaszczurtd/Fiesta/blob/main/materials/imgs/incar.jpg?raw=true)

![Workbench](https://github.com/jaszczurtd/Fiesta/blob/main/materials/imgs/workplace.jpg?raw=true)
