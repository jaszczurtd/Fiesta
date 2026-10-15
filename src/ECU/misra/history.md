# Historical MISRA screening notes

[Polski](history.pl.md) · [Current scope and policy](../../../MISRA.md)

These notes preserve earlier comparisons. They do not describe an unscanned
checkout. The September 30 entries record successive states on the same day;
the last recorded ECU-wide count was 861, not the earlier 1023. No exact
commit or archived output is attached here for that last count, so it cannot
serve as a reproducible current baseline. The 949 result belongs to September 12.
Use new scan artifacts and revision metadata for further comparisons.

## September 2026 comparisons

The 2026-09-30 screening with cppcheck 2.13.0 reports **1023 active findings**
across 31 rule IDs: 709 in `src/ECU`, 314 in shared `src/common` sources.

The previous snapshot, 1044 at commit `0b3e84f`, dropped by 21 when the VP37
code was separated from the rest of the ECU (callbacks, Adjustometer reader
inside VP37, lock-free snapshot, tuning setters). Compared by file and rule ID,
the findings of the Adjustometer reader moved with it from `sensors.c` to
`vp37_adjustometer.c`, the VP37 constants from `vp37.h` to `vp37_config.h`.
The new code adds two advisory rule 8.7 findings on
`VP37_snapshotBegin/End`, which are `TESTABLE_STATIC` like the eight in
`sensors.c` (the runner defines `UNIT_TEST`), one rule 15.5 finding for the
new early return in `VP37_init`, and two advisory rule 2.5 findings for macros
that only the bench telemetry uses (`VP37_PUBLICATION_POLL_US`,
`START_VP37_REPORT_WAIT_US`). The snapshot is shared between the cores only as
words accessed with atomics; turning its floats and the telemetry struct into
words uses unions, recorded as the advisory rule 19.2 deviation DR-007. Its
relaxed, acquire and release memory orders, and those of the trace hand-over
and the test layer, are recorded against Amendment 4 rule 21.25 as DR-008;
cppcheck 2.13 does not check Amendment 4.

The 2026-09-30 move of the VP37 module to `src/common/vp37` left the count at
**1023**: the screening runner, the cppcheck target and clang-tidy now cover
that directory, and compared by file and rule ID only the path prefix of the
module's findings changed. The rule 8.7 suppressions of DR-005 follow the new
paths.

Later on 2026-09-30 the VP37 module and the Adjustometer came under MISRA
requirements (step 4 of the module extraction): **zero active findings** in
`src/common/vp37`, the shared protocol headers and `src/Adjustometer`, held
there by gate 4 of `runalltests.sh` (`check_misra.sh --fail-paths`, and a
second run with `--project src/Adjustometer`). Mandatory: none were present.
Required and Advisory were fixed in code, with these recorded exceptions:
rule 15.5 (single exit) stays as deviation DR-009, the TESTABLE_STATIC scan
context as DR-010, the shared protocol register map as DR-011, the
configuration-dependent header macros as DR-012, the build-system macros in
module `hal_project_config.h` files as DR-013 and the generated module token
registry as DR-014; rule 21.25 remains DR-008.
The ECU-wide count is **861** (590 in `src/ECU`, 271 in shared
`src/common` sources); the drop from 1023 is the VP37 share of the cleanup,
and DR-014 later took the generated token registry (10 findings, one per
other module's token) out of the screening. The note does not give a separate final count after
that suppression.
The rest of `src/ECU` remains a screening snapshot outside the gate.

At commit `8cc5d2f` the same run gave 1046. The shared configurator session
(`sc_config_session_t` in `sc_command_handlers.c`, used by every firmware
module) removed one rule 15.5 and one rule 8.9 finding from `config.c`. It also
left six advisory rule 8.7 findings on the SC command service API, which the
host tests call directly; DR-006 in the deviation register records them as an
accepted deviation, like DR-005. Compared by file and rule ID, independently of
line numbers, nothing was added.

The previous snapshot (939 findings, 2026-09-22, commit `a5560a4`) was not
refreshed by the changes between `a5560a4` and `8cc5d2f`; the difference to
1046 comes from those changes, which were not screened one by one. Licensed
rule texts remain unavailable, so this is a regression check rather than a
compliance claim.

### Earlier comparison reference

Reference run on 2026-09-12 with cppcheck 2.13.0, without licensed rule texts.
This run uses the corrected project configuration and the JaszczurHAL atomic
model (see below), and records the earlier comparison baseline:

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

