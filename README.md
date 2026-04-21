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
- `src/Adjustometer` - dedicated RP2040-based VP37 feedback module within the ECU subsystem. It has its own firmware tree, is commonly deployed on the same PCB as the ECU, measures pump-coil resonance with a Hartley oscillator, derives baseline-relative pulse magnitude, reports fuel temperature and supply voltage, and exposes its state over I2C to the ECU.

Completed / not actively developed:

- `src/Fiesta_clock` - standalone clock/temperature display (AVR, finished project, currently not under active development).

Deprecated code in `legacy/`:

- old/archival sources that are not the primary development target,
- useful for reference and migration history,
- should not be treated as current production firmware.

## Current status (2026-04-21)

- Primary firmware modules compile with the current HAL (`src/ECU`, `src/Clocks`, `src/OilAndSpeed`, `src/Adjustometer`).
- Host-side validation exists for ECU, Adjustometer, and Clocks. ECU currently provides 11 executable host test targets under `src/ECU/tests/`; `test_cppcheck` is added as an extra CTest entry when `cppcheck` is installed. Adjustometer currently provides 2 host tests.
- ECU CI runs cppcheck as part of standard test execution (`ctest`) and includes baseline gating in GitHub Actions.
- ECU now has a dedicated project-local MISRA screening runner under `src/ECU/misra/`, a deviation register scaffold, and a manual artifact workflow (`.github/workflows/ecu-misra.yml`).
- Initial ECU MISRA screening baseline reports 976 active findings across 27 rule IDs, so the new runner should be treated as a triage/evidence path, not a pass signal.
- ECU startup reports compile timestamp (`__DATE__` + `__TIME__`).

## ECU MISRA-C migration status

Two-level estimate:

- engineering/architecture alignment: **~80-85%**,
- formal compliance readiness (tooling + evidence): **~55-60%**.

Scope:

- `src/ECU` is in scope for MISRA-C migration,
- `src/Clocks`, `src/OilAndSpeed`, and `src/Adjustometer` are currently out of MISRA scope.

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
- project-local MISRA screening infrastructure for ECU: repeatable runner, CI artifact path, and deviation register bootstrap.

Pending areas:

- full C linkage path for required HAL/tool APIs,
- replacement of C++ dependencies (Arduino core/HAL/test path) if full project-level C-only build is required,
- MISRA hardening pass (casts, bounds, overflow, naming, volatile/mutex review).

## MISRA documentation policy (mandatory)

For each MISRA-related change, update safety status in `README.md` (this file)
in the same change set.

Project-specific working notes can be kept locally, but repository-level safety
status in this file must remain synchronized with code changes.

## ECU MISRA screening entry points

Local run:

```bash
cd src/ECU
bash misra/check_misra.sh --out misra/.results
```

Manual CI artifact workflow:

- `.github/workflows/ecu-misra.yml`

Notes:

- the repository does not ship MISRA rule-text extracts,
- severity split by Mandatory / Required / Advisory is only available when a licensed local rule-text file is provided to the runner.

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

Each module is an Arduino-style app (`*.ino` + companion sources), but .ino is just a simple wrapper for setup() and loop()/loop1().

Helper scripts are available in module-specific `scripts/` directories:

- `select-board.sh`
- `upload-uf2.sh`
- `refresh-intellisense.sh`
- `serial-monitor.sh` / `serial-monitor.py` (where available)

### Linux/WSL setup (shared for all modules)

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

### Host tests (CMake)

Note: CMake in this repository is used for host test configuration/build, and
test targets are compiled as C++ (`.cpp`).

ECU:

```bash
cmake -S src/ECU -B src/ECU/build_test -DCMAKE_BUILD_TYPE=Release
cmake --build src/ECU/build_test --parallel
ctest --test-dir src/ECU/build_test --output-on-failure
```

Clocks:

```bash
cmake -S src/Clocks -B src/Clocks/build_test -DCMAKE_BUILD_TYPE=Release
cmake --build src/Clocks/build_test --parallel
ctest --test-dir src/Clocks/build_test --output-on-failure
```

Adjustometer:

```bash
cmake -S src/Adjustometer -B src/Adjustometer/build_test -DCMAKE_BUILD_TYPE=Release
cmake --build src/Adjustometer/build_test --parallel
ctest --test-dir src/Adjustometer/build_test --output-on-failure
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

Adjustometer:

```bash
cd src/Adjustometer
bash scripts/upload-uf2.sh
```

## Hardware and materials

- PCB projects and variants: `Fiesta_pcbs/`
- wiring/pinout notes: `Fiesta_pcbs/pinout.txt`, `Fiesta_pcbs/wirings.txt`
- supporting docs/graphics/examples: `materials/`

## Credits

Libraries used in `src/Fiesta_clock`:

- DS18B20 library origin: Davide Gironi.
- `PCF8563.c` created 2014-11-18 by Jakub Pachciarek.

## Photos

![Display](https://github.com/jaszczurtd/Fiesta/blob/main/materials/imgs/display.JPG?raw=true)

![ECU](https://github.com/jaszczurtd/Fiesta/blob/main/materials/imgs/ecu.jpg?raw=true)

![In-car](https://github.com/jaszczurtd/Fiesta/blob/main/materials/imgs/incar.jpg?raw=true)

![Workbench](https://github.com/jaszczurtd/Fiesta/blob/main/materials/imgs/workplace.jpg?raw=true)
