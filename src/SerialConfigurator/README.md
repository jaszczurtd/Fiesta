# SerialConfigurator

GTK4 desktop companion for Fiesta modules (Linux-first) with a HELLO-based
module detection UI.

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

## Run

```bash
./build/serial-configurator
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
- `Disconnect` clears detected state and returns all lamps to red.
- Scanning stops early as soon as all known modules are detected.
- A scrollable log view shows HELLO responses and detection details.

## Code Structure

- `src/ui/main.c` is a thin entrypoint only (`sc_app_run(...)`).
- `src/ui/sc_app.c` contains GTK window/UI logic and async detection workflow.
- `src/core/sc_core.c` contains serial scan + HELLO protocol logic.

## CI / Test Baseline

At this stage, a minimal CI layer is already worth having. The project now has
two `sc_core` test binaries integrated with CTest:

- `serial-configurator-core-tests` (smoke checks)
- `serial-configurator-core-api-tests` (API contract checks)

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
for the shortcuts to work — see `keybindings.reference.json` for the exact JSON
to paste.
