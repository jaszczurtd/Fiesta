# Fiesta

Firmware ecosystem for Ford Fiesta 1.8(M)TDDI custom electronics.

The repository contains multiple MCU applications (ECU, clocks, indicators), hardware assets (PCBs/wiring), and materials used to build and validate the project.

Gallery:
https://postimg.cc/gallery/pHF4jy2

## What Is In This Repository

Fiesta is not a single source code. It is a set of cooperating firmware modules designed for vehicle functions such as:

- engine control and diagnostics,
- dashboard and gauge control,
- dedicated oil pressure and speed module,
- related PCB and wiring documentation.

## Current Features (2026-03)

- Modular architecture with separate apps in `src/`:
	`ECU`, `Clocks`, `OilAndSpeed`, `Fiesta_clock`.
- Dual-core style runtime pattern in major modules (`setup/loop` plus `setup1/loop1`) for split workloads.
- ECU feature set:
	sensor processing, RPM/turbo logic, fan/heater/glow plugs/heated windshield control, VP37 support, GPS integration.
- OBD-II over CAN support in ECU (`src/ECU/obd-2.*`) with PID handling, VIN/ECU name responses and DTC integration.
- Dedicated DTC manager (`src/ECU/dtcManager.*`) with active/stored/pending/permanent code handling.
- Dashboard/cluster stack (`src/Clocks`) including TFT abstractions, buzzer, gauge rendering and physical cluster signal generation.
- Oil pressure and speed module (`src/OilAndSpeed`) for dedicated instrumentation and CAN communication.
- ECU host-side unit tests (Unity + CMake) with CI workflow in `.github/workflows/ecu-tests.yml`.

## Code Structure

Top-level layout:

- `src/` - firmware source code.
- `Fiesta_pcbs/` - PCB projects and wiring/pinout notes.
- `materials/` - reference notes, graphics, schematics, examples, and supporting assets.

Main firmware modules in `src/`:

- `src/ECU/` - main engine control logic.
	Key files:
	`start.*`, `sensors.*`, `rpm.*`, `turbo.*`, `engineFuel.*`, `engineFan.*`, `engineHeater.*`,
	`glowPlugs.*`, `heatedWindshield.*`, `obd-2.*`, `obd-2_mapping.*`, `dtcManager.*`, `can.*`.
- `src/Clocks/` - dashboard/cluster and display logic.
	Key files:
	`logic.*`, `Cluster.*`, `TFTExtension.*`, `pressureGauge.*`, `tempGauge.*`, `simpleGauge.*`, `buzzer.*`, `can.*`.
- `src/OilAndSpeed/` - oil pressure + speed module, CAN handling and start/runtime orchestration.
- `src/Fiesta_clock/` - legacy/specialized clock-related firmware in C.
- `src/library/` - shared libraries and submodules (see dependencies section).

## Dependencies

Project uses Arduino ecosystem components and custom libraries.

- Arduino platform:
	https://www.arduino.cc/
- RP2040 core (Earle F. Philhower, III):
	https://github.com/earlephilhower/arduino-pico/
- Unity test framework:
	https://github.com/ThrowTheSwitch/Unity/
- OBD-2 simulator inspiration/base:
	https://github.com/spoonieau/OBD2-ECU-Simulator

### Required: JaszczurHAL

`JaszczurHAL` is a required dependency for this project:
https://github.com/jaszczurtd/JaszczurHAL

What it is:

- a shared hardware abstraction layer used by Fiesta modules,
- a common place for low-level hardware helpers, timers, tools, and platform-specific adapters,
- a compatibility bridge that allows the same higher-level logic to be reused across firmware modules.

Why this solution is used:

- it reduces duplicated low-level code in each module,
- it keeps application logic (ECU, Clocks, OilAndSpeed) cleaner and easier to maintain,
- it improves testability by enabling host-side stubs/mocks used by ECU CMake tests,
- it makes migration and board/platform changes easier because hardware-specific details are centralized.

Current status and direction:

- today the project runs on Arduino-based stack,
- thanks to `JaszczurHAL` and the abstraction layer approach, the codebase is prepared for future ports to other platforms such as ESP and STM32.

Git submodules declared in `.gitmodules`:

- `src/library/JaszczurHAL`
- `src/library/canDefinitions`

Clone with submodules:

```bash
git clone --recursive <repo-url>
```

If already cloned:

```bash
git submodule update --init --recursive
```

## Build and Development Workflow

### Firmware Build/Upload

Each module is organized as a standalone Arduino-style app (`*.ino` + companion `.cpp/.h`).

Utility scripts are available in module-specific `scripts/` directories (for example in `src/ECU/scripts/`, `src/Clocks/scripts/`, `src/OilAndSpeed/scripts/`):

- `select-board.sh`
- `upload-uf2.sh`
- `serial-monitor.sh` / `serial-monitor.py`
- `refresh-intellisense.sh`

Exact board/port setup depends on your local environment and toolchain.

### ECU Host Tests

ECU includes host-side tests via CMake in `src/ECU/CMakeLists.txt`.

Run locally:

```bash
cmake -S src/ECU -B src/ECU/build_test -DCMAKE_BUILD_TYPE=Release
cmake --build src/ECU/build_test --parallel
ctest --test-dir src/ECU/build_test --output-on-failure
```

Current tested areas include:

- glow plugs,
- engine fan,
- engine heater,
- sensor calculations.

CI automatically runs these tests for ECU changes via `.github/workflows/ecu-tests.yml`.

## Hardware and Materials

- PCB projects and variants are under `Fiesta_pcbs/`.
- Wiring/pinout notes are included in `Fiesta_pcbs/pinout.txt` and `Fiesta_pcbs/wirings.txt`.
- Supporting docs, graphics and examples are under `materials/`.

## Credits

- DS18B20 library origin: Davide Gironi.
- `PCF8563.c` created 2014-11-18 by Jakub Pachciarek.

## Photos

![Display](https://github.com/jaszczurtd/Fiesta/blob/main/materials/imgs/display.JPG?raw=true)

![ECU](https://github.com/jaszczurtd/Fiesta/blob/main/materials/imgs/ecu.jpg?raw=true)

![In-car](https://github.com/jaszczurtd/Fiesta/blob/main/materials/imgs/incar.jpg?raw=true)

![Workbench](https://github.com/jaszczurtd/Fiesta/blob/main/materials/imgs/workplace.jpg?raw=true)
