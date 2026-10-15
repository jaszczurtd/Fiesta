# MISRA deviation register

[Polski](deviation-register.pl.md) · [Scope and policy](../../../MISRA.md)

This register records accepted deviations, tool false positives and deferred
findings for ECU, shared VP37 and Adjustometer. Entries retain their decision
history; an open entry is not a new scan result.

## Rules

Every active suppression must have an entry here. Prefer a code fix, and
record an explicit rationale and disposition before closing a Required or
Mandatory finding. Keep removed deviations with status `closed`.

`tool-fp` means a validated false positive, `tool-context` a scan-configuration
limitation, `accepted-deviation` an accepted exception and `pending-fix` work
yet to resolve. `bootstrap` records initialization of the register. Status is
`open`, `planned`, `closed` or `none`. `n/a` means no rule applies.

The register starts with the September 12, 2026 scan using the project
configuration and HAL atomic model; later entries state their own decision
dates. Earlier reduced-configuration findings were not carried over.

## Index

| ID | Rule | Type | Status |
|---|---|---|---|
| [DR-000](#dr-000) | n/a | bootstrap | closed |
| [DR-001](#dr-001) | 17.3 | tool-fp | open |
| [DR-002](#dr-002) | 17.3 | tool-fp | closed |
| [DR-003](#dr-003) | 21.6, 11.3, 10.4, 10.8, 15.7 | pending-fix | planned |
| [DR-004](#dr-004) | 17.7 | pending-fix | planned |
| [DR-005](#dr-005) | 8.7 | accepted-deviation | open |
| [DR-006](#dr-006) | 8.7 | accepted-deviation | open |
| [DR-007](#dr-007) | 19.2 | accepted-deviation | open |
| [DR-008](#dr-008) | 21.25 (Amendment 4) | accepted-deviation | open |
| [DR-009](#dr-009) | 15.5 | accepted-deviation | open |
| [DR-010](#dr-010) | 8.7 | tool-fp | open |
| [DR-011](#dr-011) | 2.5 | accepted-deviation | open |
| [DR-012](#dr-012) | 2.5 | tool-context | open |
| [DR-013](#dr-013) | 2.5 | tool-context | open |
| [DR-014](#dr-014) | 2.5 | tool-context | open |

## DR-000

Rule: **n/a**. Type: `bootstrap`. Status: `closed`.

Scope: `src/ECU/misra/*`.

Suppression: none.

Register initialized. Superseded by the rows below.

## DR-001

Rule: **17.3**. Type: `tool-fp`. Status: `open`.

Scope: `src/common/scDefinitions/sc_command_handlers.c`: use of `HAL_COMMAND_SOURCE_MASK`.

Suppression: none.

`HAL_COMMAND_SOURCE_MASK` lives in `hal_command_router.h` behind
`HAL_ENABLE_COMMAND_ROUTER`, which the HAL feature closure derives from
`HAL_ENABLE_SERIAL_COMMANDS`. cppcheck does not resolve that closure in every
configuration it explores, so the macro use reads as an implicit function call. The
firmware build compiles the same line without a diagnostic.

## DR-002

Rule: **17.3**. Type: `tool-fp`. Status: `closed`.

Scope: JaszczurHAL `src/hal/core/hal_mutex_once.h`.

Suppression: none.

JaszczurHAL now routes compiler atomics through `hal_compiler.h`, and its cppcheck model
covers atomic loads and compare-exchange conditions. The former rule 17.3 and
`misra-config` artifacts no longer occur.

## DR-003

Rule: **21.6, 11.3, 10.4, 10.8, 15.7**. Type: `pending-fix`. Status: `planned`.

Scope: `src/common/scDefinitions/*.c`.

Suppression: none.

Twelve Required findings in sources compiled into the ECU, Clocks and OilAndSpeed
firmware: `<stdio.h>` for `snprintf`, two pointer casts in the parameter-descriptor
offset mechanism, and mechanical type and `else` cleanups. Disposition waits on the open
decision whether `src/common/scDefinitions` belongs in MISRA scope.

## DR-004

Rule: **17.7**. Type: `pending-fix`. Status: `planned`.

Scope: `src/common/scDefinitions/sc_param_handlers.c`.

Suppression: none.

Eight ignored return values. The `snprintf` ones hide silent response truncation and
deserve a real fix; the `memcpy` ones return the destination pointer and are noise. Same
scope decision as DR-003.

## DR-005

Rule: **8.7**. Type: `accepted-deviation`. Status: `open`.

Scope: `src/ECU/engineMaps.c` and the VP37 module in `src/common/vp37`: `vp37.c`, `vp37_feedback.c`, `vp37_compensation.c`, `vp37_control.c`, `vp37_telemetry.c`, `vp37_current.c`.

Suppression: `misra-c2012-8.7:*vp37/vp37.c`, `misra-c2012-8.7:*vp37/vp37_current.c`.

Advisory. The engine maps are data with external linkage consumed by one module each by
design, so one table can only be edited in one place. The VP37 module is split into
thematic translation units whose step functions have a single caller in another unit;
folding them back into the caller's file would undo the split. The screening runs over
`src/ECU/*.c` and `src/common/vp37/*.c` only, so functions consumed by the host tests
also read as referenced from one unit; a symbol a file defines without using it itself,
as the maps and the split's step functions are, is not reported by this checker at all,
so only the two units that still call their own external functions carry a suppression.
Decided 2026-09-16.

## DR-006

Rule: **8.7**. Type: `accepted-deviation`. Status: `open`.

Scope: `src/common/scDefinitions/sc_command_handlers.c`: the SC command service API (`sc_command_service_init`, `sc_command_service_router`, `sc_command_format_serial_response`, `sc_command_allow_inactive_reboot`, `sc_command_reply_legacy_unknown`, `sc_command_service_process_deferred`).

Suppression: `misra-c2012-8.7:*sc_command_handlers.c`.

Advisory. Firmware modules start their configurator session through
`sc_config_session_t`, defined in the same unit, so the screening, which does not read
the host tests, sees these functions referenced from one translation unit. They keep
external linkage because `tests/test_sc_command_handlers.cpp` drives the service
directly with its own routers and invalid configurations; internal linkage would take
those tests away. Same situation as DR-005. The open question of DR-003 and DR-004,
whether `src/common/scDefinitions` belongs in MISRA scope, is not decided by this row;
the suppression covers rule 8.7 in this one file only. Decided 2026-09-29.

## DR-007

Rule: **19.2**. Type: `accepted-deviation`. Status: `open`.

Scope: `src/common/vp37/vp37_snapshot.c`: the float bit patterns of the published VP37 status and the word view of the telemetry staging buffer.

Suppression: `misra-c2012-19.2:*vp37_snapshot.c`.

Advisory. The control core shares its state with the other core only as `uint32_t` words
accessed with atomics, which removes the data race of a plain copy (memory orders:
DR-008).

A float field and a struct have to become words and back; C offers `memcpy`, which
breaks Required rule 21.15 (incompatible pointer types), or a union, whose member reads
reinterpret the stored representation as C11 6.5.2.3 defines. A `_Static_assert` next to
the float conversion stops the build unless `float` and `uint32_t` have the same size.
The status words carry no padding: they are built from the four fields, not from a
struct.

The telemetry words do carry the padding bytes of `VP37Telemetry`, and their content
does not matter. The staging union has static storage, so every byte is zero before the
first publication (C11 6.7.9p10); after member stores the padding bytes take unspecified
values (6.2.6.1p6), which C11 counts among indeterminate values (3.19.2).

The words are read as `uint32_t`, an exact-width type with no padding bits and so no
trap representation (7.20.1.1), so such a read yields some valid value and is not
undefined behaviour. The reader writes those bytes only into the padding of its own
`VP37Telemetry`, which no code reads, because members are accessed by name. The unions
are local to this unit and the host tests read every value back exactly, including under
ThreadSanitizer. Decided 2026-09-30.

## DR-008

Rule: **21.25 (Amendment 4)**. Type: `accepted-deviation`. Status: `open`.

Scope: JaszczurHAL `HAL_ATOMIC_*` with relaxed, acquire, release or acquire-release order in `src/common/vp37/vp37_snapshot.c` (publication of the VP37 snapshot), `src/common/vp37/vp37_telemetry.c` (hand-over of the bench trace buffer) and `src/ECU/tests_helpers.c` (console line queue, configurator request, contact time and session flag, test parameters).

Suppression: none.

The rule asks for sequentially consistent order in the `<stdatomic.h>` synchronization
functions. These units use the GCC `__atomic` builtins behind the HAL macros, not the
library functions, but the same concern applies, so the choice is recorded here.

Snapshot: a sequence lock with one writer, as in Boehm, "Can seqlocks get along with
programming language memory models?" (2012): the writer stores the odd counter, issues a
release fence, stores the data words relaxed and publishes the even counter with
release; the reader loads the counter with acquire, the data words relaxed, issues an
acquire fence and reloads the counter. A copy is accepted only when both counter reads
are the same even value, so it holds one whole publication.

Sequentially consistent data words would put barriers around each of about 310 word
stores in every 5 ms control step on the Cortex-M0+, which has no store-release
instruction, without changing the result.

Trace buffer and test-layer values: one core hands a buffer or a value to the other
(message passing); each side's plain accesses come before its release store and after
the other side's acquire load, which is exactly what acquire/release orders. No
algorithm in these units depends on the order of a store and a later load of a different
variable, the only case sequential consistency adds.

Evidence: the host tests of the snapshot run under ThreadSanitizer and with interleaved
publications, and a plain copy in place of the atomic words is reported as a race; they
run on x86, whose hardware ordering is stronger than required, so the ordering on the
RP2040 rests on the C11 model above, which the compiler implements with `dmb`. cppcheck
2.13 does not check Amendment 4 rules; this row records the decision. Decided
2026-09-30.

## DR-009

Rule: **15.5**. Type: `accepted-deviation`. Status: `open`.

Scope: `src/common/vp37/*.c` and `src/Adjustometer/*.c`.

Suppression: `misra-c2012-15.5:*vp37/*.c`, Adjustometer: `misra-c2012-15.5` in its own suppressions.

Advisory (single point of exit). Both code bases use guard clauses: a function checks
one refusal condition, reports it and returns, and the remaining body stays unindented.
Folding 60+ functions onto one exit would trade that shape for nested conditionals or
result flags across the control loop right before the hardware gate, without changing
behaviour. The module's shared inline headers (`adjustometer_feedback.h`,
`fiesta_sensor_helpers.h`) follow the same style. Chosen by the user 2026-09-30 when the
other Advisory rules were fixed.

## DR-010

Rule: **8.7**. Type: `tool-fp`. Status: `open`.

Scope: `src/common/vp37/vp37_snapshot.c`: `VP37_snapshotBegin`, `VP37_snapshotEnd`.

Suppression: `misra-c2012-8.7:*vp37/vp37_snapshot.c`.

The screening defines UNIT_TEST, which expands TESTABLE_STATIC to nothing so the host
tests can drive a torn copy deterministically; every firmware build compiles the two
functions static. The finding describes the scan configuration, not the firmware. Same
mechanism as the TESTABLE_STATIC symbols in `sensors.c`. Decided 2026-09-30.

## DR-011

Rule: **2.5**. Type: `accepted-deviation`. Status: `open`.

Scope: `src/common/adjustometer_protocol.h`.

Suppression: `misra-c2012-2.5:*adjustometer_protocol.h` in both scans.

Advisory (unused macro). The header is the register map of the ECU-Adjustometer
protocol: each side uses its half, so each scan sees the other side's registers, flags
and counts as unused, and the map stays complete on purpose (`ADJUSTOMETER_REG_PULSE_LO`
documents the register at 0x01 even though readers address the block by its first
register). Decided 2026-09-30.

## DR-012

Rule: **2.5**. Type: `tool-context`. Status: `open`.

Scope: `src/common/vp37/vp37_config.h`, `src/common/vp37/vp37_current.h`: the bench telemetry intervals, poll period and trace depth.

Suppression: `misra-c2012-2.5:*vp37/*.h` in both scans, inline `misra-c2012-2.5`.

Advisory (unused macro). The module's headers define macros for the bench build (the
screening runs the car configuration, where the bench telemetry is compiled out), for
other translation units (the ECU adapter reads the power-stage pins) and for both sides
of an `#if` on `VP37_PWM_FREQUENCY_HZ`; cppcheck reports such macros unused per explored
configuration, and which header line carries the report shifts with the include graph.
The suppression therefore covers rule 2.5 for the module's headers as files; inline
suppressions additionally mark the known bench-only definitions. The dead-macro sweep of
this step stays in force for the `.c` files. Decided 2026-09-30.

## DR-013

Rule: **2.5**. Type: `tool-context`. Status: `open`.

Scope: Module `hal_project_config.h` files: macros consumed by the HAL build system.

Suppression: inline `cppcheck-suppress misra-c2012-2.5 ; DR-013`.

Advisory (unused macro). `HAL_ENABLE_*` selections, stack sizes and entry-mode switches
are read by `hal_config.h` and the CMake feature resolution, not by the module's own C
code, so the scan of the module sees them unused. Marked inline where a scan reaches
them (`src/Adjustometer/hal_project_config.h`,
`src/VP37TestBench/hal_project_config.h`). Introduced with the step-4 scans; the
register row was added 2026-09-30 when the gate audit found the tags without it.

## DR-014

Rule: **2.5**. Type: `tool-context`. Status: `open`.

Scope: `src/common/scDefinitions/sc_fiesta_module_tokens.h`.

Suppression: `misra-c2012-2.5:*sc_fiesta_module_tokens.h` in both scans.

Advisory (unused macro). The generated token registry defines one `SC_MODULE_TOKEN_*`
per module in `modules.json`; each firmware uses only its own token, so every scan sees
the other modules' tokens as unused. The registry stays complete because
`fiesta_modules.py --check` verifies it against the module list. Found by the 2026-09-30
gate as +1 after the `VP37TB` entry; suppressed per file in both scans.
