# Changelog

Repository-level status log for the Fiesta project. This file captures
build, test, and CI state for each module over time. Detailed
MISRA-C migration status lives in [`MISRA.md`](MISRA.md).

## 2026-06-08 (latest)

- Fixed host-link configuration for modules that compile
  `JaszczurHAL/src/hal/hal_crypto.cpp`: the test/mock libraries now also
  compile WireGuard crypto primitives required by that translation unit
  (`chacha20.c`, `chacha20poly1305.c`, `crypto.c`, `poly1305-donna.c`).
  This removes undefined-reference failures for
  `chacha20_init_ietf/chacha20/crypto_zero/chacha20poly1305_*` during
  host builds.
- Scope of CMake fixes:
  - `src/ECU/CMakeLists.txt`
  - `src/Clocks/CMakeLists.txt`
  - `src/OilAndSpeed/CMakeLists.txt`
  - `src/SerialConfigurator/CMakeLists.txt`
- Validation snapshot after the fix:
  - ECU host tests: 17/17 PASS.
  - Adjustometer host tests: 3/3 PASS.
  - Clocks host tests: 3/3 PASS.
  - OilAndSpeed host tests: 3/3 PASS.
  - SerialConfigurator host tests: 18/18 PASS.

## 2026-05-26 (latest)

- SerialConfigurator map view gained a dedicated recenter control.
  The Map tab now exposes a `Recenter` / `Wyśrodkuj` button that
  recenters the viewport to the latest GPS coordinates received from
  ECU telemetry (`SC_GET_GPS`) after the user pans away.
- SerialConfigurator map-tab polling cadence was aligned with ECU GPS
  refresh cadence: `src/SerialConfigurator/src/ui/sc_map_tab.c` now
  polls `SC_GET_GPS` every 4 s (4000 ms), matching firmware
  `GPS_UPDATE`.
- SerialConfigurator GPS telemetry support was added end-to-end.
  Protocol vocabulary now includes a dedicated `SC_GET_GPS` command and
  `SC_OK GPS ...` reply payload (`available`, `lat_e6`, `lon_e6`,
  `speed_kmh_x10`, `epoch`) for ECU real-time position data.
- ECU firmware now exposes GPS snapshot getters and handles
  `SC_GET_GPS` in the SC command dispatcher.
- Host-side GPS logic was split into a dedicated module
  (`src/SerialConfigurator/src/core/sc_gps.c/.h`) instead of inflating
  `sc_core`, with a thin public send-command facade in `sc_core` for
  transport reuse.
- SerialConfigurator CLI gained `get-gps` with parsed output.
- A dedicated parser test target was added:
  `tests/test_sc_gps.c` / `serial-configurator-sc-gps-tests`.
- SerialConfigurator GUI gained a fourth tab (`Map`/`Mapa`) implemented
  with libshumate when available, polling GPS every 2 s and showing the
  live marker/status; builds gracefully degrade to a placeholder when
  `shumate-1.0` is missing.
- Bootstrap dependencies were updated for this GUI feature set:
  `src/ECU/scripts/bootstrap.sh` now installs `libshumate-dev`.

## 2026-05-25

- Migrated every Fiesta firmware module to the JaszczurHAL 1.6.0 opt-in
  configuration model. Each `hal_project_config.h` was rewritten to only
  `#define HAL_ENABLE_*` flags for modules actually used by the sketch,
  relying on the new dependency propagation in `hal_config.h`
  (e.g. `HAL_ENABLE_PCF8563 -> RTC + I2C`,
  `HAL_ENABLE_ILI9341 -> TFT + DISPLAY`,
  `HAL_ENABLE_KV -> EEPROM`, `HAL_ENABLE_GPS -> SWSERIAL`):
  - Adjustometer: `I2C_SLAVE`, `RGB_LED`.
  - Clocks: `CAN`, `ILI9341` (+ `HAL_DISPLAY_ILI9341`), `RGB_LED`, `CRYPTO`.
  - ECU: `I2C`, `KV`, `CAN`, `GPS`, `PWM_FREQ`, `CRYPTO`.
  - OilAndSpeed: `CAN`, `MCP9600`, `RGB_LED`, `CRYPTO`.
  - Fiesta_clock: `PCF8563`, `DS18B20`.
  All five sketches compile cleanly with `arduino-cli` against
  JaszczurHAL 1.6.0. SerialConfigurator does not use HAL and is
  unaffected.
- Host test builds for Adjustometer / Clocks / ECU / OilAndSpeed now
  ship a test-only override at `tests/include/hal_project_config.h`
  that enables the full HAL_ENABLE_* matrix. This mirrors the host
  hal_mock target in JaszczurHAL's root `CMakeLists.txt` and is required
  because the `.mock` backend in JaszczurHAL references types from every
  optional HAL module unconditionally. Each project's `CMakeLists.txt`
  now puts `${PROJ_SRC}/tests/include` `BEFORE PUBLIC` on the mock
  static library so the override wins over the firmware config.
- Fixed an upstream `-Wmissing-field-initializers` issue in
  JaszczurHAL's `.mock/hal_rtc.cpp` (two `hal_rtc_datetime_t dt = {0}`
  aggregate initializers replaced with value-initialization `{}`),
  so ECU's `-Wall -Wextra -Werror` gate on `ecu_mock` keeps applying
  uniformly to upstream and project sources alike — no waiver needed.
- ECU test override bumps `HAL_PWM_FREQ_MAX_CHANNELS` to 128 because
  each Unity `setUp()` calls `initSensors()` which allocates three
  frequency-PWM channels without releasing them; the default 8-slot
  pool would otherwise exhaust across multi-case test executables.
- Host regression status after migration: Adjustometer 3/3, Clocks 3/3,
  OilAndSpeed 3/3, ECU 17/17 — all green on clean rebuilds.

## 2026-05-10

- ECU bootstrap SerialConfigurator step was fixed after integration
  regression: `src/ECU/scripts/bootstrap.sh` now invokes
  `src/SerialConfigurator/scripts/desktop-build.sh test` (supported mode)
  instead of `run_tests` (unsupported), so bootstrap no longer aborts with
  usage output right after desktop build.
- ECU bootstrap now also runs SerialConfigurator Debian packaging by default
  after desktop build + tests, so a `.deb` artifact is produced in
  `src/SerialConfigurator/build/` on successful runs. New override:
  `SKIP_DESKTOP_PACKAGE=1` skips only the packaging phase.
- Standardized array-length helpers on `COUNTOF` across SerialConfigurator
  core/UI/tests plus module parameter catalogs (Clocks/OilAndSpeed) and ECU
  host tests. SerialConfigurator now defines a local COUNTOF fallback for
  host builds without JaszczurHAL headers on the include path, with an
  additional fallback in `src/common/scDefinitions/sc_param_types.h` for
  shared SC helpers and tests.

## 2026-05-09

- SerialConfigurator can now generate a Debian package (`.deb`) directly
  from CMake/CPack. Install rules were added for the GUI binary (when GTK4
  is available), CLI binary, and README docs, and the build helper script now
  exposes `package`/`deb` modes for one-command packaging.
- Debian package integration now also installs a freedesktop launcher file
  (`/usr/share/applications/serial-configurator.desktop`), so Linux desktop
  menus (including Linux Mint/Cinnamon) can list the GUI app.
- SerialConfigurator launcher now uses the project-provided icon
  (`src/SerialConfigurator/scripts/icon.png`) installed as
  `/usr/share/pixmaps/serial-configurator.png` in the `.deb` package.

## 2026-04-29

- CAN definitions were moved in-tree to `src/common/canDefinitions/` and all
  module include/CMake paths now consume that shared header directly. The ECU
  bootstrap flow no longer clones `canDefinitions` as an external dependency.
- Module identity tokens were centralized in
  `src/common/scDefinitions/sc_fiesta_module_tokens.h` and adopted across ECU,
  Clocks, OilAndSpeed, Adjustometer, and SerialConfigurator tests/core paths.
- SerialConfigurator runtime constants were consolidated in
  `src/SerialConfigurator/src/config.h` and wired into core/transport/flash/UI
  code paths to remove duplicated literals.
- Flash pickers in SerialConfigurator now restore a practical initial folder
  (`remembered slot path` -> `sibling slot path` -> `<Module>/.build` fallback)
  so UF2/manifest selection works smoothly with hidden build folders.
- Validation snapshot after the refactor: SerialConfigurator 16/16,
  ECU 16/16, Clocks 3/3, OilAndSpeed 3/3, Adjustometer 3/3 (all PASS).

## 2026-04-26

- SerialConfigurator detection latency reduced: the host transport now
  reuses warm port file descriptors across repeated Detect-button
  presses (cache reconciliation instead of full close-and-rebuild) and
  the per-port HELLO budget was retuned (primary 1500 ms → 400 ms,
  retry 1500 ms, three attempts). The worst-case detection budget is
  unchanged; the happy-path warm-cache budget improves substantially.
- Module parameter catalog parity: Clocks and OilAndSpeed now return
  non-empty read-only catalogs over the SerialConfigurator session
  (Clocks: 6 thermal thresholds; OilAndSpeed: 2 sampling intervals).
  Values mirror compile-time constants from each module's `config.h`;
  the catalogs are read-only for now. ECU's catalog is unchanged.
- All in-scope test suites still pass on clean Release builds:
  SerialConfigurator 5/5, ECU 16/16, Clocks 3/3, OilAndSpeed 3/3.
  Adjustometer remains out of scope.

## 2026-04-26 (later)

- SerialConfigurator transport migrated to a framed wire protocol on the
  USB CDC channel for ECU, Clocks, and OilAndSpeed. Frames carry an
  integrity check and a per-request sequence number, and the firmware
  side no longer accepts non-framed input. The desktop companion (GUI
  and CLI) was updated in lock-step. Adjustometer remains out of scope.
- SerialConfigurator host tests grew a new CTest target dedicated to
  the wire-frame codec; total SerialConfigurator targets are now five
  (was four). All in-scope module suites still pass on clean Release
  builds: SerialConfigurator 5/5, ECU 16/16, Clocks 3/3, OilAndSpeed
  3/3, Adjustometer 3/3.
- JaszczurHAL `hal_serial_session.h` rewritten as a framed-only helper
  (see `libraries/JaszczurHAL/CHANGELOG.md`); module wrappers
  (`configSessionInit/Tick/Active/Id`) and identity contract are
  unchanged.

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
