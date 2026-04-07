# Fiesta

Firmware ecosystem for Ford Fiesta 1.8 (M)TDDI custom electronics.

The repository contains multiple embedded applications, shared libraries, hardware projects, and validation materials used to build a complete custom vehicle electronics stack.

Gallery:
https://postimg.cc/gallery/pHF4jy2

## Safety-First Engineering

Safety is treated as a top-level project requirement.

- We treat `src/ECU` as a safety-critical module and actively align it with MISRA-C.
- We prioritize defensive coding, predictable behavior, and runtime hardening over quick shortcuts.
- We continuously expand test coverage (host tests + firmware build validation) to reduce regression risk.
- We keep safety-related documentation synchronized with code changes so project status reflects reality.

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

## Current status (2026-04-04)

### Overall repository state

- primary firmware modules compile successfully with current HAL (`src/ECU`, `src/Clocks`, `src/OilAndSpeed`).
- ECU host-side test suite currently passes: `10/10` suites.
- ECU startup now reports compile build timestamp (`__DATE__` + `__TIME__`).

### ECU

- C-style architecture migration in progress (MISRA-oriented).
- `PIDController` and `SmartTimers` usage migrated through HAL C wrappers.
- Public ECU headers now include `extern "C"` guards.
- ECU source files have been renamed from `.cpp` to `.c`.
- module-local static runtime/persistent data is being consolidated into
  explicit state structs (already done for `engineFuel`, `dtcManager`, `gps`,
  `sensors`, `can`, `start`, `obd-2`).
- Host-side ECU tests (CMake + Unity) currently pass: `10/10` suites.

Covered suites include:

- `test_glowPlugs`
- `test_engineFan`
- `test_engineHeater`
- `test_sensors_calc`
- `test_obd2`
- `test_can`
- `test_hal_wrappers`
- `test_engineFuel`
- `test_dtcManager`
- `test_turbo`

### MISRA-C migration estimate (ECU)

Two-level estimate is used to keep status honest:

- engineering/architecture alignment with MISRA-C intent: **~75-80%**,
- formal compliance readiness (tooling + evidence): **~45-50%**.

Safety/MISRA scope (important):

- only `src/ECU` is treated as safety-critical and in-scope for MISRA-C migration,
- `src/Clocks` and `src/OilAndSpeed` are not currently required to be ported to MISRA-C.
- MISRA findings in `src/Clocks` / `src/OilAndSpeed` should not block ECU safety
  releases unless project scope is explicitly changed.

Important:

- this is an engineering progress estimate, not a formal compliance certification,
- exact compliance requires a dedicated MISRA checker and a documented deviation register,
- current Arduino build flow still compiles ECU `.c` files in transitional C++ mode and links C++ HAL utilities.

Main completed areas:

- class-to-struct migration for core ECU modules,
- central state ownership (`ecu_context_t`),
- HAL C wrappers for PID and soft timers,
- `extern "C"` guards in public ECU headers,
- ECU source files renamed from `.cpp` to `.c`.
- module-local static runtime/persistent data consolidated into dedicated
  module state structs (`engineFuel`, `dtcManager`, `gps`, `sensors`, `can`,
  `start`, `obd-2`),
- migration to explicit `HAL_TOOLS_*` config macros (legacy aliases retained in HAL for compatibility).
- targeted runtime hardening in ECU:
  - fixed fuel ring-buffer boundary condition,
  - added watchdog crash-snapshot bounds guard and Turbo/VP37 cross-core mutex guards,
  - added bounds validation for `sensors` global value index accessors with regression tests.

Main pending areas:

- full C linkage path for required HAL/tool APIs (current ECU `.c` sources are
  compiled in transitional C++ mode),
- MISRA hardening pass (fixed-width casts, bounds checks, overflow safeguards, naming consistency, volatile/mutex review).

## MISRA documentation policy (mandatory)

For every MISRA-related change, update in the same change set:

1. repository root `README.md` (this file),
2. `src/ECU/README.md`,
3. `src/ECU/doc/misra-context-provider.en.txt`.

MISRA-related code changes should not be merged without these documentation updates.

## Dependencies

Runtime code dependency:

- `JaszczurHAL` (HAL and utility layer): https://github.com/jaszczurtd/JaszczurHAL

Project submodule dependency:

- `canDefinitions` (CAN IDs/signals shared headers used across firmware modules)

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

### ECU compilation on Linux/WSL (debian family): full setup and build flow

The same Arduino/WSL setup pattern also applies to other firmware modules such as `src/Clocks` and `src/OilAndSpeed`, but the host-side `CMake` test flow documented below is specific to `src/ECU`.
The `src/ECU` module can be worked on in two modes under Linux/WSL:

- host/mock build with `CMake` for fast local validation,
- firmware build with `arduino-cli` for RP2040/Pico targets.

Recommended local library layout for Linux/WSL:

- `JaszczurHAL`: `/home/youruser/libraries/JaszczurHAL`
- Arduino sketchbook root: `/home/youruser`

Important configuration note:

- in `src/ECU/.vscode/settings.json`, `arduino.sketchbookPath` should point to the user home directory, not to `/home/`,
- for this workspace the correct value is:

```json
"arduino.sketchbookPath": "/home/youruser"
```

This makes `arduino-cli` and project helper scripts resolve user libraries from:

```text
/home/youruser/libraries
```

That is required when `JaszczurHAL` is stored outside the repository in:

```text
/home/youruser/libraries/JaszczurHAL
```

#### 1. Host-side ECU build and tests in Linux/WSL

Use this flow to quickly validate ECU logic without building firmware for the board:

```bash
cd /home/youruser/Fiesta/src/ECU
cmake -S . -B build_test
cmake --build build_test -j4
ctest --test-dir build_test --output-on-failure
```

This builds the host/mock test target and runs the ECU regression suite locally in Linux/WSL.

#### 2. Install Arduino CLI in Linux/WSL

Install `arduino-cli`:

```bash
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
sudo mv bin/arduino-cli /usr/local/bin/
arduino-cli version
```

Initialize CLI configuration and add the RP2040 package index used by the Pico core:

```bash
arduino-cli config init
arduino-cli config set board_manager.additional_urls https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
```

Install the RP2040 core:

```bash
arduino-cli core update-index
arduino-cli core install rp2040:rp2040
```

#### 3. Verify external libraries

Confirm that the HAL library is available in the expected sketchbook library path:

```bash
ls /home/youruser/libraries/JaszczurHAL
```

If `JaszczurHAL` is missing there, `arduino-cli` firmware compilation will fail.

#### 4. Build ECU firmware for Raspberry Pi Pico

Compile `src/ECU` firmware from Linux/WSL:

```bash
cd /home/youruser/Fiesta/src/ECU
arduino-cli compile \
  --fqbn rp2040:rp2040:rpipico \
  --build-path .build \
  --libraries /home/youruser/libraries \
  --build-property "compiler.cpp.extra_flags=-I '$(pwd)'" \
  --build-property "compiler.c.extra_flags=-I '$(pwd)'" \
  .
```

Project default board configuration is also stored in:

- `src/ECU/.vscode/settings.json`

Current default FQBN for ECU is:

```text
rp2040:rp2040:rpipico
```

#### 5. Build and copy UF2 to BOOTSEL drive

The ECU project provides a helper script that compiles and copies the generated `.uf2` file to a mounted BOOTSEL drive:

```bash
cd /home/youruser/Fiesta/src/ECU
bash scripts/upload-uf2.sh
```

The script:

- reads `arduino.cliPath`, `arduino.fqbn`, and `arduino.sketchbookPath` from `src/ECU/.vscode/settings.json`,
- compiles the sketch into `src/ECU/.build`,
- searches for a generated `.uf2`,
- copies it to a mounted RP2040 BOOTSEL volume such as `RPI-RP2`.

If the BOOTSEL drive is not found:

1. unplug the board,
2. hold the `BOOTSEL` button,
3. reconnect USB while holding `BOOTSEL`,
4. release the button,
5. run `bash scripts/upload-uf2.sh` again.

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
