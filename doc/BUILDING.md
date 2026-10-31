# Setup and firmware development

[Polski](BUILDING.pl.md) · [Project overview](../README.md) · [Tests and CI](TESTING.md)

Run commands from the Fiesta repository root unless a section says otherwise.
Each firmware module uses the HAL application entry points `app_start()` and
`app_task0()`, with `app_task1()` for a second execution context.

## Prepare Linux or WSL

Clone with submodules as shown in the [overview](../README.md#getting-started).
On Debian-compatible Linux or Raspberry Pi OS, run as a regular user:

```bash
bash runmefirst.sh
```

This removes every firmware module's `build_test` and `.build` directories
and SerialConfigurator's `build` **before setup**, even when compilation or
tests are skipped. HAL build directories, source files and local VS Code
settings are preserved. The script then:

1. Installs apt dependencies and checks Python 3 and the Arm C++ runtime.
2. Initializes the HAL revision recorded by Fiesta; local HAL changes stop it.
3. Prepares HAL source dependencies, Pico SDK, picotool, PMD and the HAL-selected cppcheck with its MISRA addon.
4. Runs the [host quality checks](TESTING.md).
5. Builds every firmware module and prepares a verified `firmware.manifest.json` next to each `firmware.uf2`.
6. Builds and tests SerialConfigurator and creates its Debian package unless disabled.

The package list is maintained in
[`ensure-system-dependencies.sh`](../src/ECU/scripts/ensure-system-dependencies.sh).
It includes the C/C++ and Arm toolchains, CMake, Ninja, Python, Git, curl,
CA certificates, Perl, libusb, pkg-config, GTK4, dpkg-dev, libshumate,
Xvfb, xauth, locales, Valgrind, clang-format, clang-tidy, clang-tools and Java for PMD.
The setup builds the cppcheck version selected by HAL; distribution versions
can produce different findings.

The script uses `sudo` for apt. Running the entire setup as root is rejected
because it leaves root-owned build trees. `ALLOW_ROOT=1` explicitly overrides
that check. Other environment options are:

| Option | Effect |
|---|---|
| `SKIP_APT=1` | Skip system package installation. |
| `APT_NONINTERACTIVE=1` | Require apt installation through non-interactive sudo. |
| `SKIP_TESTS=1` | Skip the bootstrap's host-test step. |
| `SKIP_BUILD=1` | Skip firmware builds. |
| `SKIP_DESKTOP=1` | Skip the separate desktop build/package step. |
| `SKIP_DESKTOP_PACKAGE=1` | Build the desktop application without its Debian package. |

These options do not disable the initial cleanup. Host checks themselves
include SerialConfigurator; `SKIP_DESKTOP` is not a filter on the host matrix.
For unattended builds see the [systemd guide](../src/ECU/scripts/systemd/README.md).

## Development platforms

- **Linux** is the primary environment for firmware, desktop tools and quality checks.
- **WSL2** uses the Linux workflows, but upload needs access to the USB device
  or BOOTSEL drive. Use a Windows-side shell when that device is unavailable in WSL.
- **Windows** supports firmware development for the registered modules. Run
  `src/JaszczurHAL/runmefirst.ps1`, then use VS Code tasks or
  `src/JaszczurHAL/vscode/entry/jh-vscode.cmd`. Host tests, cppcheck, MISRA,
  Valgrind and the desktop application remain Linux-oriented.
- **macOS** is untested. No complete setup or hardware workflow is validated there.

## Build one module

This example selects ECU and its default `rp2350-plus` assembly explicitly;
`--assembly rp2040-plus` builds the same ECU for the RP2040-Plus:

```bash
src/JaszczurHAL/vscode/entry/jh-vscode build --project src/ECU --assembly rp2350-plus
src/JaszczurHAL/vscode/entry/jh-vscode build-debug --project src/ECU --assembly rp2350-plus
src/JaszczurHAL/vscode/entry/jh-vscode refresh-intellisense --project src/ECU --assembly rp2350-plus
```

Choose another module and an assembly from its `device_tree.json`. Firmware
settings and USB identity come from `.vscode/jaszczurhal.project.json`;
local selections are stored in the ignored `.vscode/jaszczurhal.local.json`.
A module with one assembly can select it automatically. Inspect the resolved
configuration without compiling:

```bash
src/JaszczurHAL/vscode/entry/jh-vscode config-dump --project src/ECU --assembly rp2350-plus --json
```

Artifacts are under `src/<Module>/.build/<target>/<assembly>/`, named
`firmware.elf` and `firmware.uf2`. ECU uses
`src/ECU/.build/rp2350-arm/rp2350-plus/` (`src/ECU/.build/rp2040/rp2040-plus/`
for the RP2040-Plus). For its bench image:

```bash
src/JaszczurHAL/vscode/entry/jh-vscode build --project src/ECU --assembly rp2350-plus --variant BENCH
```

That image goes under `variants/BENCH/` inside the assembly directory.
The [ECU guide](../src/ECU/README.md#bench-image-and-hardware) describes its
functional tests. Changing assembly or variant changes the artifact path;
use `config-dump` to confirm the file before selecting it for upload.

Fiesta_clock selects its RTC in `src/Fiesta_clock/hal_project_config.h`:
`RTC_pcf8563` is the default; `RTC_ds3231` selects the DS3231 at its fixed
I²C address, 0x68, on the assembly's RTC bus. Select only one. The `DS3231`
build variant defines `RTC_ds3231` without editing the header:

```bash
src/JaszczurHAL/vscode/entry/jh-vscode build --project src/Fiesta_clock --variant DS3231
```

Both use the same UTC synchronization and local display time. DS3231 has no
PCF8563 countdown timer; the application polls time independently of it.

## Upload and monitor

These commands act on connected hardware:

```bash
src/JaszczurHAL/vscode/entry/jh-vscode upload --project src/ECU --assembly rp2350-plus
src/JaszczurHAL/vscode/entry/jh-vscode upload-uf2 --project src/ECU --assembly rp2350-plus
src/JaszczurHAL/vscode/entry/jh-vscode monitor --project src/ECU
```

`upload` checks the USB identity and requests BOOTSEL over the running
firmware's CDC port before flashing. `upload-uf2` uses an existing BOOTSEL
mass-storage drive. Default USB IDs come from the selected target descriptor
in JaszczurHAL. Module manifests must leave VID/PID to that metadata so changing
the assembly also selects the correct USB identity.
Monitoring uses the module's USB identity,
including stable `/dev/serial/by-id/usb-Jaszczur_Fiesta_*` paths on Linux.
The corresponding VS Code tasks are `Project: Upload`,
`Project: Upload (UF2 / BOOTSEL)` and `Project: Serial Monitor`.
Keybinding references include Ctrl+Shift+1 for building, Ctrl+Shift+2 for upload and Ctrl+Shift+3 for
monitoring; copy the module's reference bindings into your user keybindings.

## Prepare a configurator manifest

A plain `jh-vscode build` writes firmware artifacts. The Fiesta bootstrap
adds and verifies the manifest used by SerialConfigurator. After a direct
build, prepare it explicitly for the selected UF2; regenerate it whenever
that UF2 changes. Example for the default ECU image:

```bash
(
    set -e
    source src/common/scripts/fiesta-firmware-common.sh
    fiesta_prepare_manifest_for_uf2 "$PWD/src/ECU" \
        "$PWD/src/ECU/.build/rp2350-arm/rp2350-plus/firmware.uf2"
)
```

The helper writes `firmware.manifest.json` beside the UF2 and checks its module,
filename and SHA-256. It reads version/build metadata from the module's
`config.h`, with defaults when those values are not literal definitions.
The [protocol](../src/common/scDefinitions/PROTOCOL.md#flashing) describes
required fields and the unverified optional signature. Manifest preparation
is a separate step from compilation and from identity checks during upload.

## Debug through SWD

Connect a CMSIS-DAP Raspberry Pi Debug Probe: `SC` to `SWCLK`, `SD` to `SWDIO`,
and a common ground. Supply the target as required by its board. Install
Cortex-Debug (`marus25.cortex-debug`) and make `openocd` and
`arm-none-eabi-gdb` available to it. HAL's Windows setup configures tool paths
in the VS Code user profile; on other hosts use PATH or Cortex-Debug settings.

Open the module directory, press F5 and select its assembly profile. ECU ships
`Project: Debug rp2350-plus (RP2350 ARM)`, `Project: Debug rp2040-plus (RP2040)`
and the corresponding `variant: BENCH` profiles. Each launches a debug build,
flashes it and stops at `main`. Other modules have profiles for their own
RP2040 assemblies. The generated files currently provide no attach-only
profiles.
USB upload uses `jh-vscode`; the probe serves the Cortex-Debug/GDB path.

## Maintain generated configuration

An entry with `kind: "inactive"` only reserves its SerialConfigurator index
and display name. DPF uses index 2, while ECU, Clocks and RTC_Clock keep
indices 0, 1 and 3. Inactive entries have no token, USB identity, firmware
project, tests or CI build. Their saved flash paths are ignored and omitted
when saving configuration.

[`modules.json`](../modules.json) lists module kinds, tokens, host tests and
CI selection. `scripts/fiesta_modules.py` generates the token header,
SerialConfigurator table and shell module lists. The VS Code generator reads
that registry, HAL registries and the assemblies. Check both from the root:

```bash
python3 scripts/fiesta_modules.py --check
python3 scripts/sync_vscode_projects.py --check
```

Run either generator without `--check` to regenerate its outputs. The local
pre-commit hook runs the VS Code generator with `--stage` and stages managed
outputs. `scripts/configure_git_hooks.py` configures that hook path on Linux,
macOS or Windows. Keep generator changes and their required HAL revision
together; see [dependency updates](../DEPENDENCIES.md).

SerialConfigurator has a separate [desktop build guide](../src/SerialConfigurator/README.md#build).
