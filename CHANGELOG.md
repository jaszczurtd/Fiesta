# Changelog

Repository-level status log for the Fiesta project. This file captures
build, test, and CI state for each module over time. Detailed
MISRA-C migration status lives in [`MISRA.md`](MISRA.md).

## 2026-04-26

- `src/SerialConfigurator` moved past the initial HELLO-only shell:
  core transport now lives in `src/core/sc_transport.c/.h` (line assembly
  with retry/backoff), UI details view was split into
  `src/ui/sc_module_details.c/.h`, and a first-class CLI shell exists in
  `src/cli/sc_cli_main.c`.
- SerialConfigurator protocol surface now includes read-only `SC_*` flow in
  core/CLI (`SC_GET_META`, `SC_GET_VALUES`, `SC_GET_PARAM_LIST`,
  `SC_GET_PARAM`) with fail-closed target selection (`--module` / `--uid` /
  `--port`) and explicit rejection of ambiguous targets.
- SerialConfigurator host tests now include three CTest targets:
  `serial-configurator-core-tests`,
  `serial-configurator-core-api-tests`,
  `serial-configurator-core-protocol-tests`.
- Firmware-side Phase-2 baseline `SC_*` behavior is now aligned across all
  in-scope modules (`ECU`, `Clocks`, `OilAndSpeed`) for metadata/values
  refresh compatibility.
- Current host-test inventory in-tree:
  `ECU` 15 executable tests (+ optional `test_cppcheck` when `cppcheck` is
  installed), `Clocks` 3 tests, `OilAndSpeed` 3 tests, `Adjustometer` 3
  tests.

## 2026-04-24

- `src/SerialConfigurator` desktop baseline is now integrated in-tree (GTK4 UI shell + HELLO detection core, with UI split into `src/ui/sc_app.c` and thin `src/ui/main.c` entrypoint).
- Baseline scope also includes non-blocking background detection with early-stop scanning once all known modules are found, plus HELLO-based Detect/Disconnect flow in the GTK shell.
- SerialConfigurator host tests now include two CTest targets: `serial-configurator-core-tests` and `serial-configurator-core-api-tests`.
- Dedicated SerialConfigurator CI workflow added: [`.github/workflows/serial-configurator-tests.yml`](.github/workflows/serial-configurator-tests.yml).
- Firmware-side Phase 0 (bootstrap identity groundwork) is closed: active modules expose the shared configurator session through JaszczurHAL `hal_serial_session_*` and report module identity, firmware version, build id, and device UID on first contact.
- SerialConfigurator implementation-status details were moved out of `ARCHITECTURE.md`; architecture docs now stay changelog-neutral.
- Pre-commit validation snapshot (2026-04-24): SerialConfigurator CTest (2/2) PASS; ECU host CTest (14/14) PASS; Arduino firmware builds PASS for ECU, Clocks, OilAndSpeed, and Adjustometer.

## 2026-04-23

- Primary firmware modules compile with the current HAL (`src/ECU`, `src/Clocks`, `src/OilAndSpeed`, `src/Adjustometer`).
- Host-side validation exists for ECU, Clocks, OilAndSpeed, and Adjustometer. ECU currently provides 13 executable host test targets under `src/ECU/tests/`; `test_cppcheck` is added as an extra CTest entry when `cppcheck` is installed. Clocks currently provides 2 host tests, OilAndSpeed 2 host tests, and Adjustometer 3 host tests.
- GitHub Actions test workflows currently cover ECU (`.github/workflows/ecu-tests.yml`), Clocks (`.github/workflows/clocks-tests.yml`), OilAndSpeed (`.github/workflows/oilandspeed-tests.yml`), and Adjustometer (`.github/workflows/adjustometer-tests.yml`).
- ECU CI also runs cppcheck baseline gating in GitHub Actions (`.github/workflows/ecu-cppcheck.yml`).
- ECU MISRA status, screening snapshot, and entry points live in [`MISRA.md`](MISRA.md).
- ECU startup reports compile timestamp (`__DATE__` + `__TIME__`).
