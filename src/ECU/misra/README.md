# ECU MISRA Screening

[Polski](README.pl.md) · [Scope and policy](../../../MISRA.md)

This runner screens ECU or Adjustometer with the cppcheck build selected by HAL.

It produces findings for review and records accepted deviations. Licensed
MISRA text extracts are supplied locally and are not distributed here.

## Files

- `check_misra.sh`
  - local runner around `cppcheck` and its `misra.py` addon; it runs the
    cppcheck build pinned by JaszczurHAL (`scripts/cppcheck.sh` in the HAL
    checkout), because findings and the addon change between cppcheck
    releases; `--cppcheck` or `CPPCHECK_BIN` picks another executable,
  - loads JaszczurHAL's compiler-atomic model when the sibling HAL checkout is
    available,
  - writes reusable artifacts to `misra/.results/` by default.
- `suppressions.txt`
  - suppressions for validated tool false positives or accepted deviations,
  - every real suppression must be mirrored in `deviation-register.md`.
- `deviation-register.md`
  - project-local register for accepted deviations, tool false positives, and pending findings.

## Run the scan

```bash
cd src/ECU
bash misra/check_misra.sh --out misra/.results
```

## Licensed rule texts

- The repository does not ship MISRA rule texts.
- If you already have a licensed local Appendix A extract, point the runner at it with `--rule-texts /absolute/path/to/file`.
- As a convenience, the runner also auto-detects `src/ECU/misra/rule-texts.local.txt` if you keep a private local copy there. That filename is ignored by Git.

## Artifacts

- `.results/results.txt`
  - full raw cppcheck output,
- `.results/active-findings.txt`
  - MISRA-only findings filtered from the raw output,
- `.results/rule-counts.txt`
  - per-rule count for active MISRA findings,
- `.results/error_count.txt`
  - total count of active MISRA findings,
- `.results/summary.txt`
  - concise scan summary,
- `.results/severity-counts.txt`
  - severity split only when licensed rule texts are provided.

## Review

- Prefer fixing code over adding suppressions.
- Use suppressions only for validated tool false positives or explicitly accepted deviations.
- Keep `deviation-register.md` synchronized with `suppressions.txt`.
- Treat this runner as evidence support for the ECU MISRA migration, not as proof of compliance on its own.

## Configuration and comparisons

The runner forces `hal_project_config.h` into each translation unit, selects
the GNU-like branch of `hal_compiler.h` and loads HAL's compiler-atomic model.
These settings let cppcheck see opt-in features and atomic operations.
`--project` resolves its directory relative to the current working directory.
From the repository root, `--project src/Adjustometer` selects Adjustometer;
from `src/ECU`, use `--project ../Adjustometer`. `--fail-paths` makes active
findings in matching paths fail the run.
Use `bash misra/check_misra.sh --help` for the complete options.

Keep output with the Fiesta/HAL revisions, tool version and scan options.
[Historical notes](history.md) explain why older scans with features disabled
cannot be compared directly. The current scope and acceptance criteria live
only in the [project policy](../../../MISRA.md).
