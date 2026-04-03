# Fiesta ECU (RP2040)

Embedded ECU application for Ford Fiesta, built on top of `JaszczurHAL`.

## Build and test (host/mock)

```bash
cmake -S . -B build_test
cmake --build build_test -j4
ctest --test-dir build_test --output-on-failure
```

## MISRA-C migration status

As of **2026-04-03**, the ECU codebase is estimated to be **~70% MISRA-C compatible**.

Important:
- this is an engineering progress estimate, not a certification result,
- exact compliance requires a full run of a dedicated MISRA checker and formal deviation log.

What is already done:
- class-to-struct migration for ECU modules (C-style API),
- central ECU context (`ecu_context_t`) for module state ownership,
- HAL C wrappers for PID and soft timers (`hal_pid_controller_*`, `hal_soft_timer_*`),
- `extern "C"` guards added in public ECU headers.

What is still pending:
- Step 3 completion: `.cpp -> .c` migration for selected modules,
- Step 4 hardening: explicit fixed-width casts, bounds checks, overflow guards,
  volatile/mutex review, naming consistency pass.

## MISRA documentation policy

For every change related to MISRA migration:
- update repository root `README.md`,
- update `src/ECU/README.md` (this file) status section,
- update `src/ECU/doc/misra-context-provider.en.txt` in the same change set.
