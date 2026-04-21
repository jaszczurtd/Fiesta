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

| ID | Rule | Scope | Type | Status | Rationale | Suppression |
| --- | --- | --- | --- | --- | --- | --- |
| DR-000 | n/a | `src/ECU/misra/*` | bootstrap | none | Register initialized. No active documented deviations or suppressions yet. | none |
