# Fiesta

Firmware ecosystem designed for Ford Fiesta 1.8 diesel with custom electronics.
It was built for a custom 1.8 diesel hybrid engine setup that combines
elements of the 1.8D (indirect injection) and 1.8TDDI (direct injection)
variants, with a VP37 pump installed.

At this stage the project is still a POC and has not yet been validated as one
fully integrated system in a road-going car.
For ECU and VP37 testing, a dedicated engine was built and mounted on a steel
frame in a garage.

The repository contains multiple embedded applications, hardware assets, and validation materials used to build a complete vehicle electronics stack.

This project would not exist without all the magic happening in
[JaszczurHAL](https://github.com/jaszczurtd/JaszczurHAL).

## Credits

Author: Marcin "Jaszczur" Kielesiński

## Repository scope

Fiesta is a system composed of several tightly integrated modules working together.

Some modules are already tested in the car (non-safety-critical paths such as
the Clocks extension, and ECU in supervisory/engine-parameter mode without
VP37 actuation).

For a full description of the modules, their responsibilities, how they talk
to each other and to the vehicle, and where the external dependencies fit in,
see [`ARCHITECTURE.md`](ARCHITECTURE.md).

The desktop companion lives in
[`src/SerialConfigurator/`](src/SerialConfigurator/).
Current scope: GTK detection / details UI, per-module Flash sections
with UF2 format check + manifest pre-flash gate, end-to-end flashing
flow (auth + reboot + BOOTSEL drive watcher + UF2 copy with progress +
re-enumeration), descriptor-driven parameter reads and authenticated writes,
and a live ECU GPS view (libshumate when available). The first-class CLI shell
exposes `detect`, `list`, `meta`, `param-list`, `get-values`, `get-param`,
`get-gps`, `reboot-bootloader`, `set-param`, `commit-params`, `revert-params`,
and `set-and-commit` over the same core.
Implementation status and milestone snapshots are tracked in
[`CHANGELOG.md`](CHANGELOG.md).

## Dependencies

Required external custom library (shared across all modules):

- `JaszczurHAL` (HAL and utility layer): https://github.com/jaszczurtd/JaszczurHAL

Expected layout:

```text
<parent-of-repo-root>/libraries/JaszczurHAL
```
Example: if this repo is cloned at `/home/you/projects/Fiesta`, library goes
into `/home/you/projects/libraries/`.

Required toolchain:

- firmware/common: `git`, `build-essential`, `cmake`, `ninja-build`, `python3`,
  `curl`, `ca-certificates`, `perl`
- desktop/package: `pkg-config`, `libgtk-4-dev`, `dpkg-dev`; `libshumate-dev`
  enables the live map instead of its fallback placeholder
- QA: `cppcheck`, `valgrind`, `clang-tidy`, `clang-tools`, `clang-format`
  (`cppcheck` ships the MISRA addon used by `src/ECU/misra/check_misra.sh`)
- native RP firmware: `gcc-arm-none-eabi`,
  `libstdc++-arm-none-eabi-newlib`, `libusb-1.0-0-dev`, and `pkg-config`;
  the setup flow prepares the pinned Pico SDK and `picotool` through
  JaszczurHAL

## Build and development

Each firmware module is a regular C/C++ application built with CMake and uses
JaszczurHAL's portable `app_start()` / `app_task0()` entry points directly.
Modules that opt in to the second execution context also implement
`app_task1()`.

### One-shot setup (Debian-like Linux / WSL)

`runmefirst.sh` performs the full environment setup end-to-end, and is idempotent (safe to re-run). It:

1. installs the firmware, desktop/package, map, and QA packages listed above,
2. verifies Python 3 is available,
3. verifies `cppcheck` is available and its MISRA addon is reachable,
4. verifies the Arm C++ runtime required by native firmware builds,
5. syncs `JaszczurHAL` into `$LIB_DIR` (default: `<parent-of-repo-root>/libraries`, matching the path expected by module `CMakeLists.txt` files): missing repos are cloned, existing git checkouts are force-reset to their remote default branch and cleaned,
6. prepares JaszczurHAL's pinned source dependencies, plus the Pico SDK and
   `picotool` required by native RP firmware builds,
7. runs the complete host-QA matrix through `runalltests.sh` for `ECU`,
   `Clocks`, `OilAndSpeed`, `Adjustometer`, and `SerialConfigurator` (runtime
   CTest plus cppcheck/Valgrind/clang-tidy gates),
8. compiles firmware for every Fiesta module and reports each module-named `.uf2` and `.manifest.json` artifact: `ECU`, `Clocks`, `OilAndSpeed`, `Adjustometer`, `Fiesta_clock`; firmware settings come from each module's `.vscode/jaszczurhal.project.json`,
9. builds and tests `SerialConfigurator` and, unless disabled, creates its
   Debian package.

The toolchain set up by `runmefirst.sh` also covers everything `src/ECU/misra/check_misra.sh` needs (`cppcheck` + Python 3; cppcheck's Debian package ships the `misra.py` addon).

Run from repository root as a regular (non-root) user - the script uses `sudo`
only for apt and will prompt for the password when needed:

```bash
bash runmefirst.sh
```

Do not run this script under `sudo` - generated build trees and cloned
libraries would end up owned by `root` and break later non-root builds.
The script exits early if it detects `EUID=0`. Override with `ALLOW_ROOT=1` only if you know what you are doing.

Useful env overrides: `LIB_DIR`, `ALLOW_ROOT=1`, `SKIP_APT=1`,
`APT_NONINTERACTIVE=1`,
`SKIP_TESTS=1`, `SKIP_BUILD=1`, `SKIP_DESKTOP=1`,
`SKIP_DESKTOP_PACKAGE=1`.

IMPORTANT: `runmefirst.sh` treats `JaszczurHAL` under `$LIB_DIR` as a disposable build dependency: if that directory already contains a git checkout, the script updates `origin`, fetches the remote state, runs `git reset --hard`, and removes untracked files before continuing.

`runmefirst.sh` exercises all five Fiesta firmware modules and
SerialConfigurator end-to-end. `Fiesta_clock` currently has firmware-build
validation only; the other four firmware modules and SerialConfigurator also
have host-test projects.

### Development environment

The project is developed primarily on **Linux** (Debian-compatible/Raspberry Pi OS). **Visual Studio Code** is the main editor. Firmware modules (`ECU`, `Clocks`, `OilAndSpeed`, `Adjustometer`, `Fiesta_clock`) ship ready-to-use `.vscode/` setups (`tasks.json`, `launch.json`, `extensions.json`, `settings.json`, and
`jaszczurhal.project.json`), so compile, upload, serial monitor, host tests, and debugger flows are wired out of the box, and fully controlled by JaszczurHAL scripts.
`src/SerialConfigurator` ships its own CMake-oriented VS Code task setup, with compatible keybindings.

Platform support summary:

- **Linux (Debian-like)** - primary target. `runmefirst.sh`, JaszczurHAL `jh-vscode` firmware tasks, host tests, MISRA screening, and the daily Pi runner all work.
- **WSL2 on Windows** - works the same as native Linux for everything except direct USB access; `jh-vscode upload` and BOOTSEL upload still require access to the real USB device / BOOTSEL drive from the Windows side or a native shell.
- **Native Windows** - supported for firmware development in all five modules.
  Run JaszczurHAL's `runmefirst.ps1`, then use the generated VS Code tasks or
  `jh-vscode.cmd` for release/debug builds, IntelliSense, identity-guarded
  upload, BOOTSEL/UF2 upload and serial monitoring. Host test, cppcheck, MISRA,
  Valgrind and desktop SerialConfigurator workflows remain Linux-oriented;
  the two direct quality tasks report this explicitly on Windows.
- **macOS** - untested; the CMake host tests should work, while the native RP
  toolchain and shell/Python scripts likely need minor tweaks.

### Unattended daily build on a Raspberry Pi

`src/ECU/scripts/systemd/` ships a user-scope systemd service + timer that
once a day (13:00 local) pulls the repo, wipes ECU build artifacts, repairs
missing apt dependencies through non-interactive `sudo`, runs
`src/ECU/scripts/bootstrap.sh`, and emails a PASS/FAIL status summary with the
HEAD SHA, commit subject, and last 80 lines of the log. The full log is attached
and capped at 512 KB.
Setup, sudo requirements, and SMTP notes are documented in
[`src/ECU/scripts/systemd/README.md`](src/ECU/scripts/systemd/README.md).

Firmware modules use the shared JaszczurHAL VS Code entry instead of
module-local wrapper scripts. Fiesta keeps only project-specific helpers:

- `scripts/sync_vscode_projects.py` - regenerates cross-platform settings,
  tasks and keybinding references for all five modules from JaszczurHAL's
  board/task registry. The repository pre-commit hook runs it with `--stage`
  and stages changed managed outputs. Run it with `--check` in review or CI.
- `scripts/configure_git_hooks.py` - configures the repository-local hook path
  from Linux, macOS or Windows without requiring Bash.

- `src/ECU/scripts/bootstrap.sh` - one-shot dev-env setup + tests + firmware
  build for all Fiesta modules. You can start immediately by invoking this
  script right after clone. See `One-shot setup` section below.
- `src/common/scripts/fiesta-firmware-common.sh` - Fiesta-only module token,
  manifest, UF2, and bootstrap helpers. The firmware build routes through the
  JaszczurHAL multi-target dispatcher (`jh_firmware_project`, rp2040 target).

Tracked module settings contain no COM port. Board and port selections are
written to ignored `.vscode/jaszczurhal.local.json` files. The `Windows
firmware` workflow repeats generation checks, builds every module and refreshes
all five compile databases on a native `windows-2025` runner; physical upload
remains a manual identity-guarded smoke test.

### Host tests (CMake) - per module

CMake in this repository is used for SerialConfigurator compilation and host
test configuration/build. The four host-tested firmware modules compile their
test targets as C++ (`.cpp`); SerialConfigurator has its own C project. The
firmware-module pattern is:

```bash
cmake -S src/<Module> -B src/<Module>/build_test -DCMAKE_BUILD_TYPE=Release
cmake --build src/<Module>/build_test --parallel
ctest --test-dir src/<Module>/build_test --output-on-failure
```

`ECU`, `Clocks`, `OilAndSpeed`, and `Adjustometer` have separate host-test
projects. `Fiesta_clock` does not currently have host tests and is covered by
firmware compilation in the bootstrap/build workflow.

For a single command that runs host tests across all primary modules (ECU,
Adjustometer, Clocks, OilAndSpeed, SerialConfigurator) and then executes
module-level `check-valgrind` / `check-clang-tidy` targets, use:

```bash
./runalltests.sh
```

The host-test gate runs runtime tests only (`ctest -LE static-analysis`) so
static analyzers do not look like a stuck test run.

Useful flags: `-j<N>`, `--skip-cppcheck`, `--skip-valgrind`,
`--skip-clang-tidy`.

### Firmware build - per module

```bash
cd src/<ECU|Clocks|OilAndSpeed|Adjustometer|Fiesta_clock>
../../../libraries/JaszczurHAL/vscode/entry/jh-vscode build --project "$PWD"
../../../libraries/JaszczurHAL/vscode/entry/jh-vscode build-debug --project "$PWD"
../../../libraries/JaszczurHAL/vscode/entry/jh-vscode upload --project "$PWD"
../../../libraries/JaszczurHAL/vscode/entry/jh-vscode upload-uf2 --project "$PWD"
../../../libraries/JaszczurHAL/vscode/entry/jh-vscode refresh-intellisense --project "$PWD"
```

Notes:

- `Project: Upload` / `Ctrl+Shift+2` uses the same `jh-vscode upload` path.
- `Project: Upload (UF2 / BOOTSEL)` is the BOOTSEL mass-storage path.
- `Project: Serial Monitor` / `Ctrl+Shift+3` uses `jh-vscode monitor` with stable
  `/dev/serial/by-id/usb-Jaszczur_Fiesta_*` identity matching.
- The module-local VS Code wrappers were removed; task behavior now comes from
  `libraries/JaszczurHAL/vscode/`.

### Desktop companion build (SerialConfigurator)

```bash
cd src/SerialConfigurator
./scripts/desktop-build.sh build
./scripts/desktop-build.sh run
./scripts/desktop-build.sh test
./build/serial-configurator-cli detect
```

### Debugging with Raspberry Pi Debug Probe

Each module ships a `.vscode/launch.json` with Cortex-Debug (`marus25.cortex-debug`) configurations for live debugging via the Raspberry Pi Debug Probe over CMSIS-DAP:

- **Debug: RP2040 (Pico/Pico W/Zero/Plus)** - flash + break at `main`
- **Debug: RP2350 (Pico 2/Pico 2 W)** - flash + break at `main`
- **Debug: Attach RP2040 / RP2350** - attach to a running target without re-flashing

Prerequisites:

- Debug Probe firmware v2 or later (USB VID:PID `2e8a:000c`). Older Picoprobe firmware (`2e8a:0004`) also works since the shipped configs use `interface/cmsis-dap.cfg`.
- SWD wiring: probe `SC`->target `SWCLK`, `SD`->`SWDIO`, `GND`->`GND`. Power the target independently or from the probe's debug header.
- The `marus25.cortex-debug` extension (listed in each module's `.vscode/extensions.json`).
- `openocd` and `arm-none-eabi-gdb` must be available to Cortex-Debug. Native
  Windows `runmefirst.ps1` configures the verified paths in the VS Code user
  profile. Other hosts may use `PATH` or their platform-specific Cortex-Debug
  user settings. Module launch files do not require machine-local path keys.

Usage: open the module in VS Code, press `F5`, and pick the configuration. The `launch` variants run `Project: Build (Debug)` as `preLaunchTask` so `${workspaceFolder}/.build/firmware.elf` stays fresh; `attach` variants skip the build step.

Note: `jh-vscode upload` does **not** use the probe for upload - it flashes over USB CDC. The probe is only used by Cortex-Debug's GDB path.

### What this project does *not* claim

- **No ISO 26262.** Correctly absent - no certification authority would
  accept it for a one-off retrofit, and faking it would be worse than
  omitting it.
- **No AUTOSAR.** Unjustifiable overhead for solo scope. JaszczurHAL plays
  the same architectural role (HAL -> application separation) at roughly one-
  thousandth of the bureaucracy cost.
- **No HIL rig.** Host SIL only via mock HAL. A HIL rig is the right answer
  at production scope, not at one-vehicle scope.
- **No formal MISRA compliance.** Partial scope (ECU only), runner used as
  triage evidence not as certification. Treated explicitly as "in progress"
  with a public deviation register, not as a checkmark.
- **No code review.** Solo project. Compensated by test infrastructure,
  cppcheck gating, MISRA screening, CI on every push. Test code is the
  proxy reviewer.
- **No AEC-Q100 silicon.** RP2040 is consumer-grade, deliberately. Cost,
  dual-core execution, flexible GPIO/PWM peripherals, and flash-backed EEPROM
  were picked over automotive silicon precisely because the vehicle is a
  personal car, not a production platform. The current engine Hall and
  Adjustometer resonance inputs use GPIO edge interrupts, not PIO capture.

Even though the full stack is not yet running in a real car as one integrated
system, safety is treated as a first-class priority.

- `src/ECU` is the safety-critical module and is being aligned with MISRA-C.
- Defensive coding and runtime hardening are prioritized over shortcuts,
  already at this stage of the project.
- Coverage is expanded continuously (host tests + firmware build validation).
- Safety documentation is kept synchronized with code changes.

MISRA-C migration status, policy, and screening entry points live in a
dedicated document: [`MISRA.md`](MISRA.md).

## Current status

Per-module build, test, and CI history is tracked in
[`CHANGELOG.md`](CHANGELOG.md).

Gallery:
https://postimg.cc/gallery/pHF4jy2

## Photos

This is a test prototype used to test the engine when it is not installed in the car:

![ECU](https://github.com/jaszczurtd/Fiesta/blob/main/materials/imgs/ecu.jpg?raw=true)

Bootstrap script + USB parameters:

![Bootstrap](https://github.com/jaszczurtd/Fiesta/blob/main/materials/imgs/bootstrap.png?raw=true)

Test engine:

![Engine](https://github.com/jaszczurtd/Fiesta/blob/main/materials/imgs/test_engine.jpg?raw=true)

Serial (Fiesta) Configurator:

![Main](https://github.com/jaszczurtd/Fiesta/blob/main/materials/imgs/SC1.png?raw=true)

![Flash](https://github.com/jaszczurtd/Fiesta/blob/main/materials/imgs/SC2.png?raw=true)

![Values](https://github.com/jaszczurtd/Fiesta/blob/main/materials/imgs/SC3.png?raw=true)

ISO 14229, ISO 14230, and ISO 15765 OBD-2 implementation running with Fordiag diagnostic tool:

![Main](https://github.com/jaszczurtd/Fiesta/blob/main/materials/imgs/fordiag1.png?raw=true)

![Main](https://github.com/jaszczurtd/Fiesta/blob/main/materials/imgs/fordiag2.png?raw=true)

![Display](https://github.com/jaszczurtd/Fiesta/blob/main/materials/imgs/display.JPG?raw=true)

![In-car](https://github.com/jaszczurtd/Fiesta/blob/main/materials/imgs/incar.jpg?raw=true)

Testing bench:

![Workbench](https://github.com/jaszczurtd/Fiesta/blob/main/materials/imgs/workplace.jpg?raw=true)
