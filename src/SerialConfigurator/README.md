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
- CLI supports `detect`, `list`, `meta`, `param-list`, `get-values`,
  and `get-param <id>`.
- CLI prints parsed payloads with inferred value types
  (`BOOL`/`INT`/`UINT`/`FLOAT`/`TEXT`) for parameter responses.
- CLI target selection is fail-closed: ambiguous target resolution is rejected
  (use selectors `--module`, `--uid`, or `--port`).
- Core and CLI support transitional read-only `SC_*` requests across all
  in-scope firmware modules (`ECU`, `Clocks`, `OilAndSpeed`):
  `SC_GET_META`, `SC_GET_PARAM_LIST`, `SC_GET_VALUES`, `SC_GET_PARAM`.
- Current module behavior above that common baseline is module-specific:
  `ECU` exposes a richer parameter catalog, while `Clocks` and `OilAndSpeed`
  currently expose metadata + baseline empty list/value payloads.

## Code Structure

- `src/ui/main.c` is a thin entrypoint only (`sc_app_run(...)`).
- `src/ui/sc_app.c` contains GTK window/UI logic and async detection workflow.
- `src/ui/sc_module_details.c` contains module details rendering helpers.
- `src/core/sc_core.c` contains discovery/session/protocol orchestration.
- `src/core/sc_transport.c` contains Linux/POSIX serial transport operations.
- `src/core/sc_crypto.h` contains shared crypto bridge API (backend-selected).
- `src/cli/sc_cli_main.c` contains CLI shell implementation.

## CI / Test Baseline

At this stage, a minimal CI layer is already worth having. The project now has
four core test binaries integrated with CTest:

- `serial-configurator-core-tests` (smoke checks)
- `serial-configurator-core-api-tests` (API contract checks)
- `serial-configurator-core-protocol-tests` (read-only protocol parsing + flow)
- `serial-configurator-crypto-tests` (bridge checks for `sc_crypto`)

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
