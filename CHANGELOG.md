# Changelog

Repository-level status log for the Fiesta project. This file captures
build, test, and CI state for each module over time. Detailed
MISRA-C migration status lives in [`MISRA.md`](MISRA.md).

## 2026-04-23

- Primary firmware modules compile with the current HAL (`src/ECU`, `src/Clocks`, `src/OilAndSpeed`, `src/Adjustometer`).
- Host-side validation exists for ECU, Clocks, OilAndSpeed, and Adjustometer. ECU currently provides 13 executable host test targets under `src/ECU/tests/`; `test_cppcheck` is added as an extra CTest entry when `cppcheck` is installed. Clocks currently provides 2 host tests, OilAndSpeed 2 host tests, and Adjustometer 3 host tests.
- GitHub Actions test workflows currently cover ECU (`.github/workflows/ecu-tests.yml`), Clocks (`.github/workflows/clocks-tests.yml`), OilAndSpeed (`.github/workflows/oilandspeed-tests.yml`), and Adjustometer (`.github/workflows/adjustometer-tests.yml`).
- ECU CI also runs cppcheck baseline gating in GitHub Actions (`.github/workflows/ecu-cppcheck.yml`).
- ECU MISRA status, screening snapshot, and entry points live in [`MISRA.md`](MISRA.md).
- ECU startup reports compile timestamp (`__DATE__` + `__TIME__`).
