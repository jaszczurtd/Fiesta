# MISRA-C

Repository-level safety / MISRA-C status, policy, and entry points for the
Fiesta project. The only module currently in MISRA-C migration scope is
[`src/ECU`](src/ECU/); `src/Clocks`, `src/OilAndSpeed`, and
`src/Adjustometer` are out of MISRA scope.

## ECU MISRA-C migration status

The project does not maintain a percentage-complete figure: the source tree has
no objective denominator from which such a number could be derived. Current
status is therefore expressed through concrete migrated areas and the
repeatable screening snapshot below. Formal MISRA compliance is not claimed.

Scope:

- `src/ECU` is in scope for MISRA-C migration,
- `src/Clocks`, `src/OilAndSpeed`, and `src/Adjustometer` are currently out of MISRA scope.

Completed areas include:

- class-to-struct migration for core ECU modules,
- aggregation of the main control instances in `ecu_context_t`, with
  supporting subsystems retaining explicit file-local state ownership,
- HAL C wrappers for PID and soft timers,
- `extern "C"` guards in public ECU headers,
- ECU source migration to `.c` files,
- the native Pico SDK build compiles ECU `.c` sources as C while the final
  firmware link remains mixed C/C++,
- state consolidation in ECU modules (`engineFuel`, `dtcManager`, `gps`, `sensors`, `can`, `start`, `obd-2`),
- explicit `HAL_TOOLS_*` config migration (legacy aliases retained in HAL),
- targeted runtime hardening (bounds checks, watchdog snapshot guard, mutex guards, regression tests).
- VP37 control uses explicit elapsed time and checked PID status; invalid steps
  disable the output stage. Bench tuning is applied on the controller core,
  while periodic logging formats a snapshot after releasing the control mutex.
- dual-core state synchronization pass in `src/ECU`: dedicated mutex for adjustometer snapshot, PCF8574 shadow-latch race fix, `dtcManager` state and KV persistence under a dedicated mutex; adjustometer reader API migrated from shared-pointer to out-parameter snapshot; `readHighValues()` change-detection cache removed (CAN helpers self-dedupe).
- warning quality gate for ECU host tests and native ECU firmware builds
  (`-Werror`).
- warning cleanups required by the quality gate (unused-parameter fixes in ECU and aligned external HAL dependency).
- defensive CAN updates currently applied in ECU: TX buffers are zero-initialized before send, RX path rejects invalid `NULL`/oversized frames, and the RPM publisher updates its delivery cache only after a successful transmission while using retry and heartbeat eligibility thresholds.
- project-local MISRA screening infrastructure for ECU: repeatable runner, CI artifact path, and deviation register bootstrap.
- screening configuration fix: the runner now forces `hal_project_config.h` into every translation unit, so the scan sees the same opt-in HAL modules as the firmware build.
- compiler-dependent atomics in Adjustometer now use the JaszczurHAL
  `HAL_ATOMIC_*` API, and the ECU runner loads the matching cppcheck model.
  This removes atomic-related analyzer artifacts without changing the current
  MISRA scope.

Pending areas:

- full C linkage path for required HAL/tool APIs,
- replacement of remaining C++ HAL and test dependencies if a full
  project-level C-only build is required,
- MISRA hardening pass (in progress): remaining casts/bounds/overflow cleanup in
  the Ford diagnostic services and other ECU modules, plus naming consistency
  and volatile/mutex review.

## Latest screening snapshot

Reference run on 2026-09-12 with cppcheck 2.13.0, without licensed rule texts.
This run uses the corrected project configuration and the JaszczurHAL atomic
model (see below), so it is the current comparison baseline:

- active findings: **949** across **33** rule IDs,
- `src/ECU` carries 719 of them, shared `src/common` sources the remaining 230,
- severity split is unavailable because no licensed Mandatory / Required /
  Advisory rule-text extract was supplied,
- the result is a triage/evidence snapshot, not a pass signal or compliance
  certificate.

Largest rule buckets: `misra-c2012-15.5` 323, `12.1` 168, `10.4` 86,
`2.5` 81, `17.7` 37. Largest files: `sc_command_handlers.c` 104,
`obd_ford_diag.c` 92, `dtcManager.c` 77, `sensors.c` 72, and `vp37.c` 72.
The OBD refactor reduced the directly affected OBD source/header findings from
486 to 143 without adding suppressions.

### Screening configuration correction

The build system reads `hal_project_config.h` and turns its `HAL_ENABLE_*`
lines into `-D` flags, while HAL headers pull the same file in through an
`__has_include` hook. cppcheck resolved neither, so every run before this one
screened an ECU with all opt-in modules switched off. Consequences that are now
gone: HAL declarations hidden behind their `#ifdef` guards were reported as
implicit function calls (rule 17.3), CAN and command-router constants could not
be resolved (`misra-config`), and the EEPROM layout guard in `dtcManager.c`
fired, aborting analysis of that file so it reported zero findings. The runner
now forces the project configuration into every translation unit. It also
selects the GNU-like branch of `hal_compiler.h` and loads the JaszczurHAL
cppcheck model for compiler atomic operations.

The atomic model removes the former rule 17.3 and `misra-config` artifacts from
HAL `hal_mutex_once.h`. One rule 17.3 finding remains in
`sc_command_handlers.c`; it comes from a feature-closure macro the analyzer
does not resolve in every configuration it explores.

The immediately preceding, like-for-like snapshot contained 1290 findings
across 33 rule IDs before the OBD refactor. Earlier reduced-configuration
snapshots were 1262 findings across 33 rule IDs (2026-09-09), 1026 across 33
(2026-07-10), and 787 across 25 (2026-04-21); those older runs are not directly
comparable. Use the generated `summary.txt` and `rule-counts.txt` artifacts when
comparing future runs.

ECU has a dedicated project-local runner under `src/ECU/misra/`, a deviation
register, and a manual artifact workflow (`.github/workflows/ecu-misra.yml`).

## MISRA documentation policy (mandatory)

For each MISRA-related change, update safety status in this file (`MISRA.md`)
in the same change set.

Project-specific working notes can be kept locally, but repository-level safety
status in this file must remain synchronized with code changes.

## ECU MISRA screening entry points

Local run:

```bash
cd src/ECU
bash misra/check_misra.sh --out misra/.results
```

Manual CI artifact workflow:

- `.github/workflows/ecu-misra.yml`

Notes:

- the repository does not ship MISRA rule-text extracts,
- severity split by Mandatory / Required / Advisory is only available when a licensed local rule-text file is provided to the runner.

See also:

- [`src/ECU/misra/README.md`](src/ECU/misra/README.md) - runner documentation, artifacts, and process rules,
- [`src/ECU/misra/deviation-register.md`](src/ECU/misra/deviation-register.md) - accepted deviations and tool false positives.
