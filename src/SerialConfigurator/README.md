# SerialConfigurator

GTK4 desktop companion for Fiesta modules (Linux-first) with a HELLO-based
module detection UI.
The project now also includes a first-class CLI shell over the same core.

## Requirements

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config libgtk-4-dev dpkg-dev
```

Optional/full-development packages:

```bash
# Live OpenStreetMap GPS view; without it the Map tab shows a placeholder.
sudo apt install -y libshumate-dev

# Local check-valgrind / check-clang-tidy targets.
sudo apt install -y valgrind clang-tidy clang-tools
```

## Build

```bash
cmake -S . -B build
cmake --build build
```

### JaszczurHAL integration

The serial frame codec is compiled directly from
`JaszczurHAL/src/hal/serial/hal_serial_frame.h`. The build requires a
JaszczurHAL checkout and stops during CMake configuration when the header is
missing. There is no local frame-codec fallback.

Default path:

- `SC_JASZCZURHAL_DIR=../JaszczurHAL` (relative to this module; the pinned
  Fiesta submodule)

Override it when JaszczurHAL is stored elsewhere:

```bash
cmake -S . -B build \
  -DSC_JASZCZURHAL_DIR=/absolute/path/to/JaszczurHAL
```

#### Shared crypto and authentication code

`src/core/sc_crypto.h` and `src/core/sc_auth.h` are thin C entry points over
JaszczurHAL. `hal_crypto.cpp` (Base64, MD5, SHA-256/HMAC-SHA256,
ChaCha20-Poly1305) and `hal_sc_auth.cpp` (the per-device key derivation and
challenge response, the same code the firmware runs) are compiled straight
into `serial_configurator_core`. This repository keeps no crypto
implementation and no fallback backend; CMake stops when the HAL sources are
missing.

## Run

```bash
./build/serial-configurator
```

## CLI

```bash
./build/serial-configurator-cli detect
./build/serial-configurator-cli list
./build/serial-configurator-cli meta --module ECU
./build/serial-configurator-cli param-list --module ECU
./build/serial-configurator-cli get-values --uid E661A4D1234567AB
./build/serial-configurator-cli get-param nominal_rpm --module ECU
./build/serial-configurator-cli get-gps --module ECU
./build/serial-configurator-cli reboot-bootloader --module ECU \
  --manifest /path/to/firmware.manifest.json
./build/serial-configurator-cli set-param --id nominal_rpm --value 900 --module ECU
./build/serial-configurator-cli commit-params --module ECU
./build/serial-configurator-cli revert-params --module ECU
./build/serial-configurator-cli set-and-commit \
  --id nominal_rpm --value 900 --module ECU
./build/serial-configurator-cli test-list --module ECU
./build/serial-configurator-cli test-set --id cyclic_passes --value 2 --module ECU
./build/serial-configurator-cli test-run all --module ECU
./build/serial-configurator-cli test-status --module ECU
./build/serial-configurator-cli test-stop --module ECU
```

## Test

```bash
./scripts/desktop-build.sh test
```

## Debian package (.deb)

```bash
./scripts/desktop-build.sh package
```

The generated package is written to `build/*.deb`.
After installation, Linux desktop menus should show `Fiesta USB Configurator`.
The launcher icon is packaged from `scripts/icon.png`.

## Current Functionality

Detection (read-only, no auth):

- `Detect Fiesta Modules` button sends `HELLO` to devices discovered under
  `/dev/serial/by-id/usb-Jaszczur_Fiesta_*`.
- Detection runs in a background thread, so the GTK window stays responsive
  after button click (`Detecting...` state is shown immediately).
- Module status indicators (ECU, Clocks, OilAndSpeed, RTC_Clock) start red and switch to
  green when a valid `OK HELLO ... module=<name> ...` response is received.
- Metadata + read-only parameter catalog/value reads run in the same detection
  worker flow (sequentially, no separate queue).
- For each discovered parameter id, the app probes `SC_GET_PARAM <id>` and
  validates parsed min/max/default semantics (including range checks).
- `Disconnect` clears detected state and returns all lamps to red.
- Scanning walks all discovered candidates so duplicate module instances can
  be flagged as ambiguous targets.
- A scrollable log view shows HELLO responses and detection details.
- Core and CLI support read-only `SC_*` requests across all
  in-scope firmware modules (`ECU`, `Clocks`, `OilAndSpeed`, `RTC_Clock`):
  `SC_GET_META`, `SC_GET_PARAM_LIST`, `SC_GET_VALUES`, `SC_GET_PARAM`.

Authenticated bootloader entry (Phase 3 + 5):

- `sc_core_authenticate` runs HELLO -> `SC_AUTH_BEGIN` -> `SC_AUTH_PROVE`
  using HMAC-SHA256 over a per-device key derived from the RP2040 UID.
  One-shot challenge consumption defeats replay; a new HELLO clears the
  authenticated session.
- `sc_core_reboot_to_bootloader` issues `SC_REBOOT_BOOTLOADER` after a
  successful auth and verifies the firmware ACK before returning.

Manifest pre-flash gate (Phase 4):

- Hard-rejecting host-side parser that requires `module_name`,
  `fw_version`, `build_id`, `sha256` to match the artifact byte-for-byte.
  Optional `uf2_file` lets the host resolve the UF2 sidecar path from the
  manifest location (manifest-only picker flow).
  `signature` is parsed but verification is deferred to a future
  ed25519 backend.

Flash flow (Phase 6, end-to-end):

- Per-module Flash sections in the GUI: UF2 + optional manifest pickers
  with persistent paths (`flash-paths.json`), live status field, and a
  custom GTK progress bar widget that pulses during non-copy phases and shows fraction
  during the COPY phase.
- File-pickers open in a practical initial folder order: directory of the
  last remembered path for that slot, then the sibling slot's directory,
  then the module-local `./<Module>/.build` directory when available.
- `sc_core_flash` orchestrator composes UF2 format check + manifest verify
  + auth + reboot + BOOTSEL drive watcher (`/media/$USER` + `/run/media/$USER`,
  matching `RPI-RP2*` / `RP2350`) + chunked UF2 copy with progress +
  `/dev/serial/by-id/` re-enumeration waiter on the same UID + post-flash
  HELLO with optional `fw_version` match against the manifest.
- Returns a stable 13-code `ScFlashStatus` enum so the GUI can render
  the specific reason on failure (`MANIFEST_MODULE_MISMATCH`,
  `BOOTSEL_TIMEOUT`, `POST_FLASH_FW_MISMATCH`, ...). Lock policy: the
  Detect button + every other module's Flash button + every section's
  pickers go insensitive while the flow runs.

CLI:

- CLI supports `detect`, `list`, `meta`, `param-list`, `get-values`,
  `get-param <id>`, `get-gps`, `reboot-bootloader`, `set-param`,
  `commit-params`, `revert-params`, `set-and-commit`, and the functional
  test commands `test-list`, `test-status`, `test-set`, `test-run`,
  `test-stop`, and `test-skip`.
- CLI prints parsed payloads with inferred value types
  (`BOOL`/`INT`/`UINT`/`FLOAT`/`TEXT`) for parameter responses.
- CLI target selection is fail-closed: ambiguous target resolution is rejected
  (use selectors `--module`, `--uid`, or `--port`).

Authenticated parameter writes (Phase 8):

- ECU and RTC_Clock writable descriptors use a staged transaction:
  `SC_SET_PARAM` changes staging, `SC_COMMIT_PARAMS` validates and applies it,
  and `SC_REVERT_PARAMS` restores staging from active values.
- The GUI Values tab exposes per-module forms and Apply staged / Commit /
  Revert controls. Clocks and OilAndSpeed remain read-only.
- The CLI exposes the same flow and provides `set-and-commit` as a
  single-command operator convenience under one authenticated session; on
  commit failure it attempts to revert staging.

GPS view:

- ECU exposes the read-only `SC_GET_GPS` snapshot (`available`, latitude,
  longitude, speed, and UTC epoch).
- The GUI GPS View tab polls on the ECU update cadence (4 seconds). With
  libshumate it renders an OpenStreetMap marker and recenter control; without
  libshumate it builds a placeholder instead.
- `get-gps` exposes the same snapshot in the CLI.

ECU functional tests:

- On the Values tab the ECU page has two sub-tabs: Settings (the parameter
  form) and Tests. The Tests sub-tab lists the tests the ECU reports
  (`SC_TEST_LIST`), each with its description, runtime parameters, Defaults
  and Run. Run sequence starts every test marked for the sequence; Skip and
  Stop act on the running test. An ECU built without tests, or firmware
  without the commands, shows "No tests available".
- The status panel shows the running test, who started it, its step in a
  sequence, the elapsed time, the demanded and measured actuator position,
  the test's own progress (cyclic profile and rate, random target, staircase
  setpoint, pot phase, cycles and passes), and the last result.
- Run authenticates, sends the parameters edited in the form, then starts
  the test. Values set this way hold until the ECU resets.
- The ECU stops a test started here when the session ends or when status
  polls stop for 3 seconds, so the tab keeps polling (every 250 ms) while its
  test runs, even when another tab is shown. Closing the window, Disconnect,
  or a new detection therefore stops the test. With the engine-speed
  interlock built in, the ECU refuses to start and stops a test once the
  engine turns; the tab says so.
- `test-run` in the CLI follows the test the same way and prints its
  progress once a second; Ctrl-C asks the ECU to stop it. It exits 0 when
  every finished test reports `done` or `ok`, and 6 otherwise.
- The protocol is described in
  [`PROTOCOL.md`](../common/scDefinitions/PROTOCOL.md#functional-tests).

Module-specific behaviour above the common baseline:

- `ECU` exposes a richer parameter catalogue (six writable thresholds,
  schema-versioned KV blob).
- `Clocks` and `OilAndSpeed` expose read-only / not-persisted descriptors
  mirroring their compile-time thresholds and sampling intervals.
- `RTC_Clock` exposes writable RTC calendar fields
  (`rtc_year/month/day/hour/minute/second`) plus read-only
  `rtc_integrity`; commit is validated as a full date-time tuple.

The wire vocabulary, descriptor types, and reply machinery for every
module are shared via [`src/common/scDefinitions/`](../common/scDefinitions/);
the protocol is described in
[`PROTOCOL.md`](../common/scDefinitions/PROTOCOL.md).

## Code Structure

- `src/ui/main.c` is a thin entrypoint only (`sc_app_run(...)`).
- `src/ui/sc_app.c` assembles the GTK window and top-level tabs.
- `src/ui/sc_detection.c` owns the asynchronous detection workflow.
- `src/ui/sc_module_details.c` contains module details rendering helpers.
- `src/ui/sc_flash_tab.c`, `sc_values_tab.c`, and `sc_map_tab.c` own the
  Flash, Values, and GPS View tabs respectively; `sc_tests_tab.c` owns the
  Tests sub-tab of the ECU page.
- `src/core/sc_core.c` contains discovery/session/protocol orchestration.
- `src/core/sc_transport.c` contains Linux/POSIX serial transport operations.
- `JaszczurHAL/src/hal/serial/hal_serial_frame.h` provides the shared serial
  frame codec used by transport and host tests.
- `src/core/sc_flash.c`, `sc_manifest.c`, `sc_gps.c`, and `sc_tests.c` own
  UF2/BOOTSEL, manifest, GPS, and functional test logic.
- `src/core/sc_text.c` and `sc_time.c` hold the token, string, clock, and
  sleep helpers the core modules share.
- `src/core/sc_crypto.h` contains shared crypto bridge API (backend-selected).
- `src/cli/sc_cli_main.c` dispatches CLI commands; command, selector, and
  output logic live in the adjacent `sc_cli_*` files.

## CI / Test Baseline

The project registers 19 non-GTK host CTest targets. When GTK4 is available,
the progress-bar and Tests sub-tab targets are registered as well, making 21
targets in the full documented desktop environment. They cover core /
protocol / crypto / flash / manifest / parameter-write / GPS / functional
test / orchestrator surfaces:

- `serial-configurator-progressbar-tests` (custom flash progress bar widget)
- `serial-configurator-tests-tab-tests` (Tests sub-tab against a mock ECU;
  skips without a display)
- `serial-configurator-core-tests` (smoke checks)
- `serial-configurator-core-api-tests` (API surface checks)
- `serial-configurator-core-protocol-tests` (read-only protocol parsing + flow)
- `serial-configurator-crypto-tests` (bridge checks for `sc_crypto`)
- `serial-configurator-frame-tests` (`$SC,...*<crc>` codec)
- `serial-configurator-auth-tests` (HMAC-SHA256 + per-device key derivation)
- `serial-configurator-manifest-tests` (Phase 4 hard-reject parser)
- `serial-configurator-phase5-tests` (auth + reboot orchestration)
- `serial-configurator-phase8-host-tests` (authenticated parameter-write host flow)
- `serial-configurator-i18n-tests` (compiled-in EN+PL string tables)
- `serial-configurator-flash-tests` (UF2 format checker)
- `serial-configurator-flash-paths-tests` (`flash-paths.json` persistence)
- `serial-configurator-sc-param-tests` (descriptor framework - find /
  validate / get / set / load_defaults / 3 reply emitters /
  schema-versioned blob codec)
- `serial-configurator-transport-timeout-tests` (command deadlines and the
  `HELLO_REQUIRED` re-handshake rule)
- `serial-configurator-sc-gps-tests` (`SC_GET_GPS` parsing and validation)
- `serial-configurator-sc-tests-tests` (`SC_TEST_*` catalog, status, and
  actions against a mock ECU)
- `serial-configurator-flash-bootsel-tests` (Phase 6.3 BOOTSEL drive watcher)
- `serial-configurator-flash-copy-reenum-tests` (Phase 6.4 UF2 copy +
  re-enumeration waiter)
- `serial-configurator-flash-orchestrator-tests` (Phase 6.5 `sc_core_flash`
  end-to-end through mock transport + `mkdtemp` BOOTSEL/by-id fixtures)

Recommended first CI steps:

- Configure and build (`cmake -S ... -B ... && cmake --build ...`)
- Run tests (`ctest --test-dir ... --output-on-failure`)
- Keep `-Wall -Wextra -Werror` enabled to block warning regressions

GitHub Actions workflow for this module is in:
`.github/workflows/serial-configurator-tests.yml`.

## VS Code

The following tasks are defined in `.vscode/tasks.json`:

| Shortcut       | Task                          | Description                                      |
|----------------|-------------------------------|--------------------------------------------------|
| Ctrl+Shift+1   | `Project: Build`              | Configure (CMake) and compile                    |
| Ctrl+Shift+2   | `Project: Upload`             | Compile and launch the application               |
| Ctrl+Shift+5   | `Project: Test`               | Build and run CTest smoke tests                  |
| Ctrl+Shift+6   | `Project: Refresh IntelliSense` | Re-run CMake with `CMAKE_EXPORT_COMPILE_COMMANDS=ON` and regenerate `.vscode/c_cpp_properties.json` |
| Ctrl+Shift+7   | `Project: Clean`              | Remove `build/` directory                        |

Reference keybinding entries are in `.vscode/keybindings.reference.json`.
The global `~/.config/Code/User/keybindings.json` must contain matching entries
for the shortcuts to work - see `keybindings.reference.json` for the exact JSON
to paste.
