# ECU MISRA Screening

This directory contains the project-local MISRA screening entry points for `src/ECU`.

It is intentionally a screening layer, not a claim of formal MISRA compliance.
The goal is to make rule-by-rule review, deviation tracking, and CI artifacts repeatable without vendor-copying third-party project glue or shipping licensed MISRA text extracts in the repository.

Contents:

- `check_misra.sh`
  - local runner around `cppcheck` and its `misra.py` addon,
  - writes reusable artifacts to `misra/.results/` by default.
- `suppressions.txt`
  - local suppressions for validated tool noise only,
  - every real suppression must be mirrored in `deviation-register.md`.
- `deviation-register.md`
  - project-local register for accepted deviations, tool false positives, and pending findings.

Default usage:

```bash
cd src/ECU
bash misra/check_misra.sh --out misra/.results
```

Optional licensed rule-text support:

- The repository does not ship MISRA rule texts.
- If you already have a licensed local Appendix A extract, point the runner at it with `--rule-texts /absolute/path/to/file`.
- As a convenience, the runner also auto-detects `src/ECU/misra/rule-texts.local.txt` if you keep a private local copy there. That filename is ignored by Git.

Generated artifacts:

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

Process rules:

- Prefer fixing code over adding suppressions.
- Use suppressions only for validated tool false positives or explicitly accepted deviations.
- Keep `deviation-register.md` synchronized with `suppressions.txt`.
- Treat this runner as evidence support for the ECU MISRA migration, not as proof of compliance on its own.

Latest snapshot (2026-04-21):

- active findings: `787`,
- top open buckets:
  - `misra-c2012-2.5`: `165`,
  - `misra-c2012-15.5`: `132`,
  - `misra-c2012-8.4`: `109`,
  - `misra-c2012-10.4`: `95`.

Recent progress:

- `obd-2.c` was cleaned up for mixed essential-type comparisons (rule 10.4),
- UDS/KWP request-handling regression coverage was expanded in `test_obd2.cpp`,
- last pass reduced overall active findings and significantly lowered rule 10.4 findings.
