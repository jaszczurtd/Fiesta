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
JH=../../../libraries/JaszczurHAL/vscode/entry/jh-vscode
"$JH" build --project "$PWD"
"$JH" build-debug --project "$PWD"
"$JH" upload --project "$PWD"
"$JH" upload-uf2 --project "$PWD"
"$JH" refresh-intellisense --project "$PWD"
```

Notes:

- `jh-vscode upload --project "$PWD"` is the same path used by the VS Code upload task / `Ctrl+Shift+2`.
- `jh-vscode upload-uf2 --project "$PWD"` is the BOOTSEL mass-storage path.
- `jh-vscode refresh-intellisense --project "$PWD"` regenerates `compile_commands.json`, `compile_commands_patched.json`, and `.vscode/c_cpp_properties.json`.
- `jh-vscode monitor --project "$PWD"` is the same path used by the VS Code monitor task / `Ctrl+Shift+3`.
- `Ctrl+Shift+9` updates `jaszczurhal.uploadPort` in `.vscode/settings.json`; identity-guarded upload still verifies the selected `/dev/serial/by-id` target before flashing.
- Fiesta-specific manifest validation remains in `src/common/scripts/`; the per-module VS Code wrappers were removed.

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
