# Fiesta ECU (RP2040)

Embedded ECU application for Ford Fiesta, built on top of `JaszczurHAL`.

## Build and test (host/mock)

```bash
cmake -S . -B build_test
cmake --build build_test -j4
ctest --test-dir build_test --output-on-failure
```

## Current build state (2026-04-04)

- firmware build (`arduino-cli`) for `src/ECU` passes,
- host/mock tests pass: `10/10`,
- startup log now includes compile timestamp (`__DATE__` + `__TIME__`).

## MISRA-C migration status

As of **2026-04-04**, status is tracked on two levels:

- engineering/architecture alignment with MISRA-C intent: **~75-80%**,
- formal compliance readiness (tooling + evidence): **~45-50%**.

Scope note (important):
- this MISRA-C migration scope applies to `src/ECU` only (safety-critical module),
- `src/Clocks` and `src/OilAndSpeed` are outside current MISRA porting scope.
- MISRA backlog in `src/Clocks` and `src/OilAndSpeed` does not block ECU
  safety releases unless scope is explicitly expanded.

Important:
- this is an engineering progress estimate, not a certification result,
- exact compliance requires a full run of a dedicated MISRA checker and formal deviation log,
- current Arduino build path still compiles ECU `.c` files in transitional C++ mode.

What is already done:
- class-to-struct migration for ECU modules (C-style API),
- central ECU context (`ecu_context_t`) for module state ownership,
- HAL C wrappers for PID and soft timers (`hal_pid_controller_*`, `hal_soft_timer_*`),
- `extern "C"` guards added in public ECU headers,
- ECU source files renamed from `.cpp` to `.c`,
- module-local static runtime/persistent state aggregated into explicit module
  state structs (including `engineFuel`, `dtcManager`, `gps`, `sensors`,
  `can`, `start`, `obd-2`),
- migration to explicit `HAL_TOOLS_*` config macros (legacy aliases left in HAL for compatibility).
- runtime hardening updates:
  - fuel ring-buffer boundary fix in `engineFuel`,
  - watchdog crash-snapshot bounds guard and Turbo/VP37 cross-core mutex guards in `start`,
  - index validation in `sensors` global value accessors (`setGlobalValue`/`getGlobalValue`) with regression tests.

What is still pending:
- full C linkage path for all required HAL/tool APIs (current build still compiles
  ECU `.c` sources in transitional C++ mode),
- Step 4 hardening: explicit fixed-width casts, bounds checks, overflow guards,
  volatile/mutex review, naming consistency pass,
- formal MISRA evidence package (checker report + deviation register).
- higher-value tests for safety paths not yet host-covered (`start.c` dual-core
  watchdog/control flow and selected OBD/UDS negative paths).

## MISRA documentation policy

For every change related to MISRA migration:
- update repository root `README.md`,
- update `src/ECU/README.md` (this file) status section,
- update `src/ECU/doc/misra-context-provider.en.txt` in the same change set.
