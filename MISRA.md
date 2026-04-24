# MISRA-C

Repository-level safety / MISRA-C status, policy, and entry points for the
Fiesta project. The only module currently in MISRA-C migration scope is
[`src/ECU`](src/ECU/); `src/Clocks`, `src/OilAndSpeed`, and
`src/Adjustometer` are out of MISRA scope.

## ECU MISRA-C migration status

Two-level estimate:

- engineering/architecture alignment: **~80-85%**,
- formal compliance readiness (tooling + evidence): **~55-60%**.

Scope:

- `src/ECU` is in scope for MISRA-C migration,
- `src/Clocks`, `src/OilAndSpeed`, and `src/Adjustometer` are currently out of MISRA scope.

Completed areas include:

- class-to-struct migration for core ECU modules,
- central state ownership (`ecu_context_t`),
- HAL C wrappers for PID and soft timers,
- `extern "C"` guards in public ECU headers,
- ECU source migration to `.c` files,
- Arduino build path compiles ECU `.c` sources as C while final firmware link remains mixed C/C++,
- state consolidation in ECU modules (`engineFuel`, `dtcManager`, `gps`, `sensors`, `can`, `start`, `obd-2`),
- explicit `HAL_TOOLS_*` config migration (legacy aliases retained in HAL),
- targeted runtime hardening (bounds checks, watchdog snapshot guard, mutex guards, regression tests).
- dual-core state synchronization pass in `src/ECU`: dedicated mutex for adjustometer snapshot, PCF8574 shadow-latch race fix, `dtcManager` state and KV persistence under a dedicated mutex; adjustometer reader API migrated from shared-pointer to out-parameter snapshot; `readHighValues()` change-detection cache removed (CAN helpers self-dedupe).
- warning quality gate for ECU host tests and Arduino ECU build paths (`-Werror`).
- warning cleanups required by the quality gate (unused-parameter fixes in ECU and aligned external HAL dependency).
- defensive CAN updates currently applied in ECU: TX buffers are zero-initialized before send, RX path rejects invalid `NULL`/oversized frames.
- project-local MISRA screening infrastructure for ECU: repeatable runner, CI artifact path, and deviation register bootstrap.

Pending areas:

- full C linkage path for required HAL/tool APIs,
- replacement of C++ dependencies (Arduino core/HAL/test path) if full project-level C-only build is required,
- MISRA hardening pass (in progress): remaining casts/bounds/overflow cleanup outside `obd-2.c`, plus naming consistency and volatile/mutex review across ECU modules.

## Latest screening snapshot

- Latest ECU project-local MISRA screening snapshot (2026-04-21) reports 787 active findings across 25 rule IDs, so the runner should be treated as a triage/evidence path, not a pass signal.
- Recent ECU hardening reduced rule 10.4 findings to 95 (mainly `obd-2.c` essential-type cleanup with regression-test coverage updates).
- ECU now has a dedicated project-local MISRA screening runner under `src/ECU/misra/`, a deviation register scaffold, and a manual artifact workflow (`.github/workflows/ecu-misra.yml`).

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

- [`src/ECU/misra/README.md`](src/ECU/misra/README.md) — runner documentation, artifacts, and process rules,
- [`src/ECU/misra/deviation-register.md`](src/ECU/misra/deviation-register.md) — accepted deviations and tool false positives.
