# Changelog

Repository-level status log for the Fiesta project. This file captures
build, test, and CI state for each module over time. Detailed
MISRA-C migration status lives in [`MISRA.md`](MISRA.md).

## 2026-07-16 (latest)

- Migrated ECU RPM interrupt initialization to the JaszczurHAL status API with
  explicit core-1 ownership:
  - wrong-core registration now propagates `HAL_ESTATE` through `RPM_init()`,
    `RPM_create()`, and the core-1 startup path,
  - core 0 logs the exact HAL status and persists new DTC `U190C`,
  - failed core-1 startup withholds its started flag and watchdog liveness
    updates, allowing the dual-core watchdog to reset the ECU after the DTC
    write,
  - the existing nine-entry legacy DTC migration remains bounded to its old
    layout while the new DTC uses an appended KV slot.
- Added host coverage for successful core-1 IRQ ownership, wrong-core failure,
  RPM resource cleanup, and `U190C` reporting/persistence. The ECU host suite,
  cppcheck static analysis, and the RP2040 firmware build pass with warnings
  treated as errors.

## 2026-07-10

- Synchronized repository documentation with the post-migration source tree:
  - corrected ECU EEPROM size (`32768` bytes) and its explicit 200 MHz FQBN,
  - documented GPIO-interrupt capture for ECU RPM and Adjustometer instead of
    describing those inputs as PIO state machines,
  - aligned OilAndSpeed documentation with its read-only, non-persisted SC
    parameter catalogue,
  - aligned the ECU cppcheck workflow description with its manual trigger,
  - updated bootstrap dependencies, environment overrides, and the actual
    host-test matrix (`Fiesta_clock` remains firmware-build-only),
  - expanded SerialConfigurator documentation for Phase 8 writes, GPS/Map,
    the current CLI, and the conditional 17/18-test CTest matrix,
  - replaced obsolete source-owned Adjustometer `.ino` references with the
    generated JaszczurHAL entry-adapter model.
- Recorded the current build-system baseline introduced after 2026-06-10:
  - all five firmware modules use `.vscode/jaszczurhal.project.json` and the
    shared `jh-vscode` entry,
  - module-local build/upload/monitor wrappers and the experimental Clocks
    STM32 PoC were removed,
  - firmware builds route through JaszczurHAL's multi-target dispatcher and
    retain Fiesta-specific manifest/UF2 helpers only,
  - bootstrap was repaired for the new CMake build/cache layout.
- Recorded the CAN migration to the current JaszczurHAL CAN API across ECU,
  Clocks, OilAndSpeed, and Fiesta_clock.
- Validation after the documentation audit: all 43 runtime host tests passed
  (`ECU` 16, `Adjustometer` 3, `Clocks` 3, `OilAndSpeed` 3,
  `SerialConfigurator` 18). A fresh ECU MISRA screening run with cppcheck
  2.13.0 recorded 1026 active findings across 33 rule IDs. This snapshot does
  not claim fresh Valgrind, clang-tidy, general cppcheck-baseline, firmware, or
  hardware validation.

## 2026-06-10

- Fixed CI clang-tidy target failures caused by missing
  `compile_commands.json` in module test workflows.
  - Configure steps in all module CI test workflows now pass
    `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`:
    - `.github/workflows/adjustometer-tests.yml`
    - `.github/workflows/ecu-tests.yml`
    - `.github/workflows/clocks-tests.yml`
    - `.github/workflows/oilandspeed-tests.yml`
    - `.github/workflows/serial-configurator-tests.yml`
  - Outcome: `check-clang-tidy` can always locate a compilation database
    in CI (`-p <build_dir>`), avoiding FileNotFoundError crashes.

- SerialConfigurator Valgrind compatibility was hardened for older
  distro toolchains that reject newer suppression classes and can fail
  all memcheck tests immediately (0/18):
  - `src/SerialConfigurator/CMakeLists.txt` now probes suppression-file
    parser compatibility at configure time by running a tiny Valgrind
    command.
  - When `tests/valgrind.supp` is rejected, CMake automatically falls
    back to `tests/valgrind-legacy.supp` (portable syntax only), instead
    of crashing every memcheck test invocation.
  - Added `src/SerialConfigurator/tests/valgrind-legacy.supp` for
    cross-version compatibility.
  - Outcome: modern hosts keep the NVIDIA-specific suppressions, while
    older Valgrind builds continue running `check-valgrind` normally.

- Standardized clang-tidy noisy-security heuristic suppression across all
  module `check-clang-tidy` targets.
  - Added
    `-checks=-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling`
    to:
    - `src/ECU/CMakeLists.txt`
    - `src/Adjustometer/CMakeLists.txt`
    - `src/Clocks/CMakeLists.txt`
    - `src/OilAndSpeed/CMakeLists.txt`
    - `src/SerialConfigurator/CMakeLists.txt`
  - Outcome: uniform analyzer signal quality in local and CI module gates
    without disabling unrelated clang-analyzer checks.

- GitHub Actions module test workflows were expanded from runtime tests only
  to full host QA targets:
  - `serial-configurator-tests.yml`, `ecu-tests.yml`,
    `adjustometer-tests.yml`, `clocks-tests.yml`,
    `oilandspeed-tests.yml` now install `valgrind`, `clang-tidy`,
    and `clang-tools` and run both:
    - `cmake --build <build-dir> --target check-valgrind --parallel`
    - `cmake --build <build-dir> --target check-clang-tidy --parallel`
  - Existing `ctest --output-on-failure` runtime step remains unchanged and
    still executes before static/dynamic analysis gates.
- Dead-store warning cleanup continued:
  - removed repeated `Value stored to 'oscillating' is never read` reports in
    `src/ECU/tests/test_hal_wrappers.cpp` by keeping warm-up calls explicit and
    asserting only on the final meaningful oscillation state.
  - Validation snapshot from refreshed logs: dead-store warning count in
    `/tmp/fiesta_runalltests_after_ci_deadstore.log` is now `0`.

- Reduced SerialConfigurator clang-tidy output noise while keeping analysis
  active:
  - `check-clang-tidy` for SerialConfigurator now excludes the
    `clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling`
    checker (the dominant source of repetitive warnings for standard C APIs).
  - Fixed remaining actionable analyzer warnings in touched paths:
    - dead stores in `src/SerialConfigurator/src/core/sc_flash.c`,
    - dead stores in `src/common/scDefinitions/sc_param_handlers.c`,
    - potential malloc leak path in
      `src/SerialConfigurator/tests/test_sc_flash_copy_reenum.c`,
    - struct padding warning in `src/SerialConfigurator/src/core/sc_transport.c`,
    - intentional enum out-of-range casts in
      `src/SerialConfigurator/tests/test_sc_i18n.c` marked with focused
      `NOLINTNEXTLINE` comments.
  - Validation snapshot: `cmake --build src/SerialConfigurator/build --target check-clang-tidy`
    now completes with `WARN_COUNT=0` in captured output.

- SerialConfigurator valgrind gate stabilization completed:
  - Fixed uninitialized-stack reads in protocol readonly host tests by
    zero-initializing local log/error buffers in
    `src/SerialConfigurator/tests/test_sc_core_protocol_readonly.c`
    (eliminates `strlen`/`log_append` memcheck noise from non-NUL stack data).
  - Added targeted third-party-only Valgrind suppressions in
    `src/SerialConfigurator/tests/valgrind.supp` for known host-driver noise
    from NVIDIA EGL and fontconfig/expat during
    `serial-configurator-progressbar-tests`.
  - SerialConfigurator CMake now force-applies
    `MEMORYCHECK_SUPPRESSIONS_FILE` when the suppression file exists,
    preventing stale cache values from silently disabling suppressions in
    existing build directories.
  - Validation snapshot: `cmake --build src/SerialConfigurator/build --target check-valgrind`
    now passes 18/18; full repository `./runalltests.sh` passes all 5 gates.

- Extended host QA gates across all primary module CMake projects with
  JaszczurHAL-style dynamic/static analysis integration:
  - Valgrind memcheck configuration (`MEMORYCHECK_COMMAND*`) is now
    defined before `include(CTest)` in:
    - `src/ECU/CMakeLists.txt`
    - `src/Adjustometer/CMakeLists.txt`
    - `src/Clocks/CMakeLists.txt`
    - `src/OilAndSpeed/CMakeLists.txt`
    - `src/SerialConfigurator/CMakeLists.txt`
  - Added per-module custom targets:
    - `check-valgrind` -> `ctest -T memcheck --output-on-failure`
    - `check-clang-tidy` -> `run-clang-tidy -p <build-dir> -quiet`
  - clang-tidy scope is restricted to module-owned host-compilable sources
    (plus shared `src/common/scDefinitions` where used), avoiding broad
    analysis of external/vendor trees.
- Validation snapshot:
  - CMake target discovery confirms both custom targets exist for all five
    modules (`ECU`, `Adjustometer`, `Clocks`, `OilAndSpeed`,
    `SerialConfigurator`).
- Added root `runalltests.sh` (JaszczurHAL-inspired gate runner) to execute
  cross-module host QA in one command:
  - tool-presence gate,
  - host runtime tests (`cmake` + `ctest -LE static-analysis`) for
    ECU/Adjustometer/Clocks/OilAndSpeed/
    SerialConfigurator,
  - dedicated cppcheck gate (`check-cppcheck` for ECU),
  - `check-valgrind` for each module,
  - `check-clang-tidy` for each module,
  with CLI switches for parallelism and skipping selected gates.

## 2026-06-08

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

## 2026-05-26

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
  with libshumate when available and showing the live marker/status; its
  polling cadence was subsequently aligned to the ECU's 4-second update
  interval. Builds gracefully degrade to a placeholder when `shumate-1.0`
  is missing.
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
  uniformly to upstream and project sources alike - no waiver needed.
- ECU test override bumps `HAL_PWM_FREQ_MAX_CHANNELS` to 128 because
  each Unity `setUp()` calls `initSensors()` which allocates three
  frequency-PWM channels without releasing them; the default 8-slot
  pool would otherwise exhaust across multi-case test executables.
- Host regression status after migration: Adjustometer 3/3, Clocks 3/3,
  OilAndSpeed 3/3, ECU 17/17 - all green on clean rebuilds.

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
  the per-port HELLO budget was retuned (primary 1500 ms -> 400 ms,
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
