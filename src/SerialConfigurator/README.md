# SerialConfigurator

[Polski](README.pl.md) · [Fiesta overview](../../README.md) · [Protocol](../common/scDefinitions/PROTOCOL.md)

SerialConfigurator is a Linux C/GTK4 application for discovering Fiesta USB
modules, reading and changing settings, flashing firmware and running ECU
bench tests. A CLI shares its core library for discovery, settings, GPS,
tests and authenticated bootloader entry. The complete firmware update flow
runs in the GUI.

## Build

Run from `src/SerialConfigurator`. On Debian-compatible Linux, install:

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config libgtk-4-dev dpkg-dev
```

`libshumate-dev` enables the OpenStreetMap view; without it the map tab shows
a placeholder. `valgrind`, `clang-tidy` and `clang-tools` enable the local
quality targets. The [Fiesta setup](../../doc/BUILDING.md) installs these too.

```bash
cmake -S . -B build
cmake --build build
./build/serial-configurator
```

CMake uses `SC_JASZCZURHAL_DIR=../JaszczurHAL`, the Fiesta submodule. For a
separate checkout, configure with `-DSC_JASZCZURHAL_DIR=/absolute/path/to/JaszczurHAL`.
The build requires the HAL frame codec and crypto/authentication sources and
stops if they are missing. It compiles `hal_serial_frame.h`, `hal_crypto.cpp`
and `hal_sc_auth.cpp` into the core instead of maintaining local copies.
The latter two provide Base64, MD5, SHA-256/HMAC-SHA256, ChaCha20-Poly1305 and
the same UID-based authentication as firmware, through `sc_crypto.h` and `sc_auth.h`.

## Discover and select a module

Click **Detect Fiesta Modules**. A background scan sends `HELLO` to candidates
under `/dev/serial/by-id/usb-Jaszczur_Fiesta_*`, reads metadata and parameter
catalogues, and probes each `SC_GET_PARAM` value against its range/default
metadata. The window shows `Detecting...` while scanning. A valid response
turns the corresponding module indicator green; the log shows discovery details.
All candidates are checked so duplicate instances can be reported as ambiguous.
**Disconnect** clears detected state and returns indicators to red.

The module registry selects ECU, Clocks, OilAndSpeed and RTC_Clock for this
protocol. Adjustometer and VP37TestBench are not configurator targets.
Read-only operations need no authentication:

```bash
./build/serial-configurator-cli detect
./build/serial-configurator-cli list
./build/serial-configurator-cli meta --module ECU
./build/serial-configurator-cli param-list --module ECU
./build/serial-configurator-cli get-values --uid E661A4D1234567AB
./build/serial-configurator-cli get-param nominal_rpm --module ECU
```

The UID above is an example; select the one reported for your board.
Use `--module`, `--uid` or `--port` to resolve the target. Ambiguous selection
is rejected. Parameter output includes inferred `BOOL`, `INT`, `UINT`,
`FLOAT` or `TEXT` types.

## Change settings

ECU exposes writable thresholds stored in a versioned KV blob. RTC_Clock
accepts `rtc_year/month/day/hour/minute/second` in CET/CEST and stores them in
its RTC as UTC. Read-only `rtc_integrity` is 1 only while the RTC holds trusted
UTC. Commit validates the whole date, including leap years.
Clocks and OilAndSpeed expose read-only, non-persisted thresholds and intervals.

In the GUI Values tab, edit fields and choose **Apply staged**, then **Commit**
to validate and apply them. **Revert** restores staging from active values.
Writing requires authentication. The CLI supports the same steps:

```bash
./build/serial-configurator-cli set-param --id nominal_rpm --value 900 --module ECU
./build/serial-configurator-cli commit-params --module ECU
./build/serial-configurator-cli revert-params --module ECU
```

For a single change in one authenticated session:

```bash
./build/serial-configurator-cli set-and-commit --id nominal_rpm --value 900 --module ECU
```

On commit failure, `set-and-commit` attempts to revert staging.
[The protocol guide](../common/scDefinitions/PROTOCOL.md#parameters) explains
storage schemas and how to extend the catalogue without losing saved values.

## Flash firmware

Prepare the selected UF2 and its [manifest](../../doc/BUILDING.md#prepare-a-configurator-manifest).
Each GUI module section has image and optional manifest pickers, remembers
paths in `flash-paths.json`, and shows progress. The picker starts in the
remembered directory, then tries the sibling picker's directory and the
module-local `./<Module>/.build` directory when available.

The manifest requires `module_name`, `fw_version`, `build_id` and `sha256`.
Optional `uf2_file` resolves the image beside the manifest when only a manifest
is selected; it must be a filename without path components. `signature` is
parsed but not verified: ed25519 verification is not implemented.

The update performs these steps:

1. Check UF2 format, manifest, module identity and SHA-256.
2. Authenticate with `HELLO`, `SC_AUTH_BEGIN` and `SC_AUTH_PROVE` using HMAC-SHA256 and a key derived from the device UID. Each challenge is consumed once; a new `HELLO` clears authentication.
3. Send `SC_REBOOT_BOOTLOADER` and wait for its acknowledgment.
4. Find the BOOTSEL drive under `/media/$USER` or `/run/media/$USER` (`RPI-RP2*` or `RP2350`).
5. Copy UF2 in chunks, then wait for the same UID under `/dev/serial/by-id/`.
6. Read `HELLO` again and, when requested by the operation, check firmware version against the manifest.

The progress bar pulses while waiting and shows a fraction during copying.
Detection, other modules' Flash buttons and all image pickers are disabled
while the update runs. `ScFlashStatus` distinguishes failures such as
`MANIFEST_MODULE_MISMATCH`, `BOOTSEL_TIMEOUT` and `POST_FLASH_FW_MISMATCH`.

The CLI can authenticate and request bootloader entry:

```bash
./build/serial-configurator-cli reboot-bootloader --module ECU \
  --manifest /path/to/firmware.manifest.json
```

This command does not perform the GUI's UF2 copy and reconnection sequence.

## Read GPS

```bash
./build/serial-configurator-cli get-gps --module ECU
```

ECU reports availability, latitude, longitude, speed and UTC epoch through
`SC_GET_GPS`. The GUI GPS View polls every 4 seconds. With libshumate it shows
an OpenStreetMap marker and recenter control; otherwise it shows a placeholder.

## Run ECU bench tests

The ECU Values page has Settings and Tests sub-tabs. Tests are discovered
through `SC_TEST_LIST` and shown with descriptions, parameters, Defaults and
Run controls. **Run sequence** starts tests marked for the sequence; **Skip**
and **Stop** affect the running test. Firmware without tests or without the
commands shows **No tests available**.

Run authenticates, sends the edited parameters and starts the test. Parameters
last until ECU reset. The status panel shows test, initiator, sequence step,
elapsed time, demanded/measured position, test-specific progress and last result.
It polls every 250 ms while its test runs, including when another tab is shown.
ECU stops the test on session end or after 3 seconds without test commands.
Closing, disconnecting or detecting again therefore stops it. The default RPM
interlock refuses or stops a test while engine speed exceeds its configured limit.

```bash
./build/serial-configurator-cli test-list --module ECU
./build/serial-configurator-cli test-set --id cyclic_passes --value 2 --module ECU
./build/serial-configurator-cli test-run all --module ECU
./build/serial-configurator-cli test-status --module ECU
./build/serial-configurator-cli test-skip --module ECU
./build/serial-configurator-cli test-stop --module ECU
```

`test-run` follows progress once a second; Ctrl-C requests a stop. Exit status
is 0 when every finished test reports `done` or `ok`, otherwise 6. See
[functional test rules](../common/scDefinitions/PROTOCOL.md#functional-tests)
for interlocks, console-only tests and status freshness.

## Tests

```bash
./scripts/desktop-build.sh test
ctest --test-dir build -N
```

The first command builds and runs tests; the second lists the configured
cases without executing them. Coverage includes selectors, discovery, frame
and reply parsing, authentication/crypto, manifests, parameter transactions,
GPS, functional tests, serial deadlines and `HELLO_REQUIRED` recovery.
Flash tests use mock transport and temporary BOOTSEL/by-id directories;
they do not demonstrate physical USB upload. GTK adds progress-bar and Tests
tab checks; the latter skips without a display.

CMake maintains the test list, including `serial-configurator-cli-selectors-tests`.
Keep `-Wall -Wextra -Werror` enabled. The
[CI workflow](../../.github/workflows/serial-configurator-tests.yml) builds
GUI/CLI and runs tests, Valgrind and clang-tidy.

## Package and editor

```bash
./scripts/desktop-build.sh package
```

The package is `build/*.deb`. Installation adds **Fiesta USB Configurator**
to desktop menus, using `scripts/icon.png`.

| Shortcut | VS Code task | Action |
|---|---|---|
| Ctrl+Shift+1 | `Project: Build` | Configure and compile. |
| Ctrl+Shift+2 | `Project: Upload` | Build and launch the application. |
| Ctrl+Shift+5 | `Project: Test` | Build and run CTest. |
| Ctrl+Shift+6 | `Project: Refresh IntelliSense` | Configure with `CMAKE_EXPORT_COMPILE_COMMANDS=ON` and regenerate `c_cpp_properties.json`. |
| Ctrl+Shift+7 | `Project: Clean` | Remove `build/`. |

Copy matching entries from `.vscode/keybindings.reference.json` into your
VS Code user keybindings (`~/.config/Code/User/keybindings.json` on Linux).

## Source map

`src/ui/main.c` calls `sc_app_run()`, and `sc_app.c` assembles the window.
`sc_detection.c` runs background discovery, `sc_module_details.c` renders
details, and `sc_flash_tab.c`, `sc_values_tab.c`, `sc_map_tab.c` and
`sc_tests_tab.c` implement their respective views.

`src/core/sc_core.c` coordinates discovery and sessions. `sc_transport.c`
handles POSIX serial I/O. `sc_flash.c`, `sc_manifest.c`, `sc_gps.c` and
`sc_tests.c` implement the corresponding operations; `sc_text.c` and
`sc_time.c` provide shared text and timing helpers. `src/cli/sc_cli_main.c`
dispatches commands to the adjacent command, selector and output modules.
