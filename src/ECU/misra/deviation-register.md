# ECU MISRA Deviation Register

This register tracks active MISRA deviations, validated tool false positives, and intentionally deferred findings for `src/ECU`.

Rules:

- Every active suppression in `suppressions.txt` must have a matching row here.
- Prefer fixing code before documenting a deviation.
- Required or Mandatory findings must not be treated as closed without an explicit rationale and disposition.
- When a deviation is removed, keep the historical row and mark it as closed instead of deleting it silently.

Column meanings:

- `ID`: local tracking identifier.
- `Rule`: MISRA rule ID, or `n/a` for process/bootstrap entries.
- `Scope`: affected file or subsystem.
- `Type`: `tool-fp`, `accepted-deviation`, `pending-fix`, or `bootstrap`.
- `Status`: `open`, `planned`, `closed`, or `none`.
- `Rationale`: short justification.
- `Suppression`: matching entry in `suppressions.txt`, or `none`.

Current register:

Rows below refer to the 2026-09-11 reference run, the first one in which the
scan sees the same opt-in HAL modules as the firmware build. Findings recorded
against earlier runs are not carried over: those runs screened a reduced ECU.

| ID | Rule | Scope | Type | Status | Rationale | Suppression |
| --- | --- | --- | --- | --- | --- | --- |
| DR-000 | n/a | `src/ECU/misra/*` | bootstrap | closed | Register initialized. Superseded by the rows below. | none |
| DR-001 | 17.3 | `src/common/scDefinitions/sc_command_handlers.c:584` | tool-fp | open | `HAL_COMMAND_SOURCE_MASK` lives in `hal_command_router.h` behind `HAL_ENABLE_COMMAND_ROUTER`, which the HAL feature closure derives from `HAL_ENABLE_SERIAL_COMMANDS`. cppcheck does not resolve that closure in every configuration it explores, so the macro use reads as an implicit function call. The firmware build compiles the same line without a diagnostic. | none |
| DR-002 | 17.3 | JaszczurHAL `src/hal/core/hal_mutex_once.h` | tool-fp | open | `__atomic_compare_exchange_n` and the `__ATOMIC_*` orders are GNU builtins unknown to cppcheck; the same cause produces two `misra-config` errors. Outside `src/ECU`, so filtered from active findings; recorded to stop the next reader re-investigating. | none |
| DR-003 | 21.6, 11.3, 10.4, 10.8, 15.7 | `src/common/scDefinitions/*.c` | pending-fix | planned | Twelve Required findings in sources compiled into the ECU, Clocks and OilAndSpeed firmware: `<stdio.h>` for `snprintf`, two pointer casts in the parameter-descriptor offset mechanism, and mechanical type and `else` cleanups. Disposition waits on the open decision whether `src/common/scDefinitions` belongs in MISRA scope. | none |
| DR-004 | 17.7 | `src/common/scDefinitions/sc_param_handlers.c` | pending-fix | planned | Eight ignored return values. The `snprintf` ones hide silent response truncation and deserve a real fix; the `memcpy` ones return the destination pointer and are noise. Same scope decision as DR-003. | none |
