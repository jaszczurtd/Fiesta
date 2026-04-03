# Fiesta

Firmware ecosystem for Ford Fiesta 1.8 (M)TDDI custom electronics.

The repository contains multiple embedded applications, shared libraries, hardware projects, and validation materials used to build a complete custom vehicle electronics stack.

Gallery:
https://postimg.cc/gallery/pHF4jy2

## Repository scope

Fiesta is not a single firmware binary. It is a set of cooperating modules:

- engine ECU and diagnostics,
- cluster/dashboard control,
- oil pressure + speed instrumentation,
- related PCB and wiring assets,
- common libraries (including HAL layer).

## Active modules

Primary code is in `src/`:

- `src/ECU` - engine control logic, diagnostics (OBD/UDS over CAN), DTC manager, actuator logic.
- `src/Clocks` - dashboard/cluster rendering and signaling.
- `src/OilAndSpeed` - dedicated oil pressure and speed module.
- `src/Fiesta_clock` - legacy/specialized clock firmware.
- `src/library` - shared libraries and submodules (`JaszczurHAL`, `canDefinitions`).

## Current status (2026-04-03)

### ECU

- C-style architecture migration in progress (MISRA-oriented).
- `PIDController` and `SmartTimers` usage migrated through HAL C wrappers.
- Public ECU headers now include `extern "C"` guards.
- Host-side ECU tests (CMake + Unity) currently pass: `7/7` suites.

Covered suites include:

- `test_glowPlugs`
- `test_engineFan`
- `test_engineHeater`
- `test_sensors_calc`
- `test_obd2`
- `test_can`
- `test_hal_wrappers`

### MISRA-C migration estimate (ECU)

Estimated MISRA-C compatibility: **~70%**.

Important:

- this is an engineering progress estimate, not a formal compliance certification,
- exact compliance requires a dedicated MISRA checker and a documented deviation register.

Main completed areas:

- class-to-struct migration for core ECU modules,
- central state ownership (`ecu_context_t`),
- HAL C wrappers for PID and soft timers,
- `extern "C"` guards in public ECU headers.

Main pending areas:

- staged `.cpp` to `.c` migration for selected modules,
- MISRA hardening pass (fixed-width casts, bounds checks, overflow safeguards, naming consistency, volatile/mutex review).

## MISRA documentation policy (mandatory)

For every MISRA-related change, update in the same change set:

1. repository root `README.md` (this file),
2. `src/ECU/README.md`,
3. `src/ECU/doc/misra-context-provider.en.txt`.

MISRA-related code changes should not be merged without these documentation updates.

## Dependencies

Project uses Arduino ecosystem components and custom libraries.

- Arduino platform: https://www.arduino.cc/
- RP2040 core (Earle Philhower): https://github.com/earlephilhower/arduino-pico/
- Unity test framework: https://github.com/ThrowTheSwitch/Unity/

Required shared dependency:

- `JaszczurHAL` (HAL and utility layer): https://github.com/jaszczurtd/JaszczurHAL

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

## Build and development

### Firmware build/upload

Each module is organized as a standalone Arduino-style app (`*.ino` + companion sources).

Utility scripts are available in module-specific `scripts/` directories, for example:

- `src/ECU/scripts/`
- `src/Clocks/scripts/`
- `src/OilAndSpeed/scripts/`

Typical helpers:

- `select-board.sh`
- `upload-uf2.sh`
- `serial-monitor.sh` / `serial-monitor.py`
- `refresh-intellisense.sh`

### ECU host tests

Run locally:

```bash
cmake -S src/ECU -B src/ECU/build_test -DCMAKE_BUILD_TYPE=Release
cmake --build src/ECU/build_test --parallel
ctest --test-dir src/ECU/build_test --output-on-failure
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
