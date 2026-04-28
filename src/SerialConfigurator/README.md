# SerialConfigurator

GTK4 desktop companion for Fiesta modules (Linux-first) with a HELLO-based
module detection UI.
The project now also includes a first-class CLI shell over the same core.

## Requirements

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config libgtk-4-dev
```

## Build

```bash
cmake -S . -B build
cmake --build build
```

### Optional: shared crypto via JaszczurHAL

`SerialConfigurator` now exposes `src/core/sc_crypto.h` and, when available,
uses `JaszczurHAL/src/hal/hal_crypto.cpp` as the backend (no duplicated crypto
implementation in this repository).

Defaults:

- `SC_USE_JASZCZURHAL_CRYPTO=ON`
- `SC_JASZCZURHAL_DIR=../../../libraries/JaszczurHAL` (relative to this repo)

Examples:

```bash
# explicit path
cmake -S . -B build \
  -DSC_JASZCZURHAL_DIR=/absolute/path/to/JaszczurHAL \
  -DSC_USE_JASZCZURHAL_CRYPTO=ON

# force no-op backend
cmake -S . -B build -DSC_USE_JASZCZURHAL_CRYPTO=OFF
```

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
```

## Test

```bash
./scripts/desktop-build.sh test
```

## Current Functionality

Detection (read-only, no auth):

- `Detect Fiesta Modules` button sends `HELLO` to devices discovered under
  `/dev/serial/by-id/usb-Jaszczur_Fiesta_*`.
- Detection runs in a background thread, so the GTK window stays responsive
  after button click (`Detecting...` state is shown immediately).
- Module status indicators (ECU, Clocks, OilAndSpeed) start red and switch to
  green when a valid `OK HELLO ... module=<name> ...` response is received.
- Metadata + read-only parameter catalog/value reads run in the same detection
  worker flow (sequentially, no separate queue).
- For each discovered parameter id, the app probes `SC_GET_PARAM <id>` and
  validates parsed min/max/default semantics (including range checks).
- `Disconnect` clears detected state and returns all lamps to red.
- Scanning stops early as soon as all known modules are detected.
- A scrollable log view shows HELLO responses and detection details.
- Core and CLI support read-only `SC_*` requests across all
  in-scope firmware modules (`ECU`, `Clocks`, `OilAndSpeed`):
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
  `signature` is parsed but verification is deferred to a future
  ed25519 backend.

Flash flow (Phase 6, end-to-end):

- Per-module Flash sections in the GUI: UF2 + optional manifest pickers
  with persistent paths (`flash-paths.json`), live status field, and a
  GtkProgressBar that pulses during non-copy phases and shows fraction
  during the COPY phase.
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
  and `get-param <id>`.
- CLI prints parsed payloads with inferred value types
  (`BOOL`/`INT`/`UINT`/`FLOAT`/`TEXT`) for parameter responses.
- CLI target selection is fail-closed: ambiguous target resolution is rejected
  (use selectors `--module`, `--uid`, or `--port`).

Module-specific behaviour above the common baseline:

- `ECU` exposes a richer parameter catalogue (six writable thresholds,
  schema-versioned KV blob).
- `Clocks` and `OilAndSpeed` expose read-only / not-persisted descriptors
  mirroring their compile-time thresholds and sampling intervals.

The wire vocabulary, descriptor types, and reply machinery for every
module are shared via [`src/common/scDefinitions/`](../common/scDefinitions/);
see `ARCHITECTURE.md` §4.3.

## Code Structure

- `src/ui/main.c` is a thin entrypoint only (`sc_app_run(...)`).
- `src/ui/sc_app.c` contains GTK window/UI logic and async detection workflow.
- `src/ui/sc_module_details.c` contains module details rendering helpers.
- `src/core/sc_core.c` contains discovery/session/protocol orchestration.
- `src/core/sc_transport.c` contains Linux/POSIX serial transport operations.
- `src/core/sc_crypto.h` contains shared crypto bridge API (backend-selected).
- `src/cli/sc_cli_main.c` contains CLI shell implementation.

## CI / Test Baseline

The project ships 15 host CTest targets covering core / protocol /
crypto / flash / manifest / orchestrator surfaces:

- `serial-configurator-core-tests` (smoke checks)
- `serial-configurator-core-api-tests` (API contract checks)
- `serial-configurator-core-protocol-tests` (read-only protocol parsing + flow)
- `serial-configurator-crypto-tests` (bridge checks for `sc_crypto`)
- `serial-configurator-frame-tests` (`$SC,...*<crc>` codec)
- `serial-configurator-auth-tests` (HMAC-SHA256 + per-device key derivation)
- `serial-configurator-manifest-tests` (Phase 4 hard-reject parser)
- `serial-configurator-phase5-tests` (auth + reboot orchestration)
- `serial-configurator-i18n-tests` (compiled-in EN+PL string tables)
- `serial-configurator-flash-tests` (UF2 format checker)
- `serial-configurator-flash-paths-tests` (`flash-paths.json` persistence)
- `serial-configurator-sc-param-tests` (descriptor framework - find /
  validate / get / set / load_defaults / 3 reply emitters /
  schema-versioned blob codec)
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
