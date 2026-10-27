# MISRA-C scope and review policy

[Polski](MISRA.pl.md) · [Project overview](README.md) · [Host checks](doc/TESTING.md)

Fiesta uses cppcheck's MISRA addon to find and review rule violations.
The project does not claim formal MISRA compliance or a percentage-complete
migration. A clean scan only covers the checked configuration and the rules
the analyzer implements.

## Scope and acceptance

| Code | Treatment |
|---|---|
| `src/common/vp37`, Adjustometer and selected shared headers | Gate 4 of `runalltests.sh` rejects active findings after recorded suppressions. |
| Other ECU code and shared sources included by its runner | Screening report for review; not a zero-findings gate. |
| Clocks, Fiesta_clock and VP37TestBench's own files | Outside MISRA scope. The shared VP37 code used by the bench remains in scope. |

The gate runs `check_misra.sh --fail-paths` for ECU and a second scan with
`--project src/Adjustometer`. Its path expressions in
[`runalltests.sh`](runalltests.sh) select VP37 and the related shared
`adjustometer_*`, `fiesta_*` and `vp37_drive_config` files. Registered
exceptions are in the [deviation register](src/ECU/misra/deviation-register.md).
The gate's required result is zero active findings in those paths; an old
successful run does not establish the result for the current checkout.

## Implementation and remaining work

ECU sources compile as C with Pico SDK, while the final link includes C++ HAL
code. Control instances are grouped in `ecu_context_t`; CAN, sensors, DTC,
RTC time, OBD and session code keep file-local state. Public ECU headers support
C linkage, and PID and soft timers have HAL C interfaces. Host tests and ECU
firmware use `-Werror`.

VP37 checks elapsed time and PID status and disables the output stage on
invalid steps. Tuning changes run on the controller core through checked
setters. Core 0 reads an atomic snapshot for telemetry. Adjustometer snapshots,
PCF8574 output state and DTC/KV access have explicit synchronization.
CAN transmission buffers are initialized, receive paths validate pointers
and lengths, and the RPM delivery cache advances only after successful sends.
Retries and heartbeat eligibility remain part of that publisher.

Remaining review includes casts, bounds and overflow in Ford diagnostics and
other ECU code, naming and volatile/mutex use. A fully C-only link would also
require replacing remaining C++ HAL and test dependencies. Existing C entry
points do not imply that the whole application links without C++.

## Run and compare screening

From the repository root:

```bash
cd src/ECU
bash misra/check_misra.sh --out misra/.results
```

Use the HAL-selected cppcheck build and preserve the output together with the
Fiesta/HAL commits, tool version, project selection, flags and suppression
files. Compare `summary.txt`, `rule-counts.txt` and `active-findings.txt` only
between equivalent configurations. The [runner guide](src/ECU/misra/README.md)
explains options and artifacts; [ecu-misra.yml](.github/workflows/ecu-misra.yml)
is the manual CI entry point.

The repository keeps [historical screening notes](src/ECU/misra/history.md)
separate from current policy. Those counts are dated observations, not a
baseline for an unscanned checkout. There is no single current total asserted
by this overview. A new comparison baseline needs its own archived output and
revision metadata.

## Review policy

Fix code before considering a suppression. Each active suppression must have
a matching, justified entry in the deviation register, including tool false
positives. Keep the register, suppression files and this scope description
consistent with every MISRA-related change. Retain closed entries as history.
Required or Mandatory findings need an explicit disposition before closure.

Licensed MISRA rule texts are not distributed here. A local licensed extract
can add the Mandatory / Required / Advisory breakdown to the report. Without
it, the runner's output remains evidence for review, not a compliance certificate.
