# ECU

Safety-critical ECU module for Ford Fiesta custom electronics.

## MISRA-C migration status

This module is in MISRA-C migration scope. Repository-level migration
status, hardening details, policy, and screening entry points are
consolidated in [`MISRA.md`](../../MISRA.md).

Module-local MISRA tooling:

- runner: [`misra/check_misra.sh`](misra/check_misra.sh),
- suppressions + deviation register: [`misra/`](misra/),
- manual CI artifact workflow: `.github/workflows/ecu-misra.yml`.

## Build

Firmware build (Arduino path):

```bash
cd src/ECU
./scripts/arduino-build.sh build
./scripts/arduino-build.sh debug
./scripts/arduino-build.sh upload
./scripts/upload-uf2.sh
./scripts/refresh-intellisense.sh
```

Notes:

- `./scripts/arduino-build.sh upload` is the same path used by the VS Code upload task / `Ctrl+Shift+2`.
- `./scripts/upload-uf2.sh` is the BOOTSEL mass-storage path.
- `./scripts/refresh-intellisense.sh` regenerates `compile_commands.json`, `compile_commands_patched.json`, and `.vscode/c_cpp_properties.json`.
- `python3 ./scripts/serial-persistent.py -m pico` is the same path used by the VS Code monitor task / `Ctrl+Shift+3`.
- `Ctrl+Shift+9` updates `arduino.uploadPort` in `.vscode/settings.json`; the running persistent monitor re-reads that setting and switches to the new preferred port without needing a manual restart.
- The module-local wrappers delegate into the shared implementation under `src/common/scripts/`.

Host tests (CMake path):

```bash
cmake -S src/ECU -B src/ECU/build_test -DCMAKE_BUILD_TYPE=Release
cmake --build src/ECU/build_test --parallel
ctest --test-dir src/ECU/build_test --output-on-failure
```

MISRA screening:

```bash
cd src/ECU
bash misra/check_misra.sh --out misra/.results
```

Notes:

- the repository does not ship licensed MISRA Appendix A rule texts,
- if a local licensed rule-text extract is available, pass it with `--rule-texts /absolute/path/to/file` to improve message quality and severity breakdown.
