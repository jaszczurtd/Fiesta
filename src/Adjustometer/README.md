# VP37 Adjustometer

Actuator position transducer for the VP37 diesel injection pump, providing
frequency-derived feedback to the ECU over I2C.

## Measurement

The pump sensor coils form the resonant element of a modified Hartley oscillator.
Frequency varies roughly within 22-37 kHz with actuator position. The RP2040
counts falling GPIO edges, measures frequency over 128 pulses, and applies an
integer EMA. The exported `PULSE` is the absolute filtered deviation from the
locked baseline, with near-zero hysteresis. It is a position observable in Hz;
calibration and actuator-drive compensation belong to the ECU.

This follows the resonant-sensing idea used around VP37/EDC15, with an external
RP2040 and digital I2C feedback. It does not reproduce the OEM electronics or
provide fuel quantity in mg/stroke. Fuel temperature and supply voltage are
reported independently; they do not modify the measured frequency or baseline.
PCB and circuit files are in `Fiesta_pcbs/vp37_adjustometer/`.

The current ECU requires the versioned
30-byte feedback block at `0x17..0x34`. Both modules must support it.

## Startup and signal validity

Startup waits `ADJUSTOMETER_WARMUP_MS` (500 ms) before enabling measurement.
Baseline convergence uses an 80 ms minimum, a 250 ms force-lock time, and six
stable windows within 12 Hz. A further 1000 ms verification restarts convergence
if drift exceeds 500 Hz. Wait for readiness before calibrating ECU travel.

Near-zero hold enters within 40 Hz and releases beyond 50 Hz after two
consecutive windows with the same sign. Signal loss uses three periods of the
filtered frequency, clamped to 10-200 ms. A lost signal produces zero pulse
and sets `SIGNAL_LOST`; consumers must inspect status and freshness.

Reset Adjustometer with ECU actuator drive off and the mechanism settled.
Wait for its baseline to become valid near zero, then reset ECU. Opening or
closing USB CDC may reset a board; follow the same sequence for Adjustometer.

## I2C and auxiliary sensors

The slave address is `0x57`; ECU uses 400 kHz. Wire definitions are shared in
[adjustometer_protocol.h](../common/adjustometer_protocol.h).

| Registers | Purpose |
| --- | --- |
| `0x00..0x04` | Original pulse (signed int16, big-endian), voltage, fuel temperature and status layout, retained for older readers. |
| `0x05..0x16` | Optional coherent diagnostics: filtered frequency, baseline, signed delta, RP2040 temperature and validity flags. |
| `0x17..0x34` | Coherent control feedback, including raw frequency and measurement freshness. |

Voltage is encoded in 0.1 V and fuel temperature in whole degrees C. The
`47k / 10k` divider limits ADC measurement to about 18.8 V at 3.3 V full scale,
although the wire field can represent 25.5 V. ADC readings use a separate EMA
with new-sample weight 1/8.

| Status mask | Meaning |
| --- | --- |
| `0x01` | `SIGNAL_LOST`: oscillator edges missing. |
| `0x02` | `FUEL_TEMP_BROKEN`: implausible NTC reading. |
| `0x04` | `BASELINE_PENDING`: convergence or verification incomplete. |
| `0x08` | `VOLTAGE_BAD`: voltage outside 8-15 V. |

Bits combine. Diagnostic `EXT_FLAGS` bits 0, 1 and 2 mean valid signal,
baseline/signed delta and chip temperature, respectively. ECU `VP37 ADJ`
`ext:1 fl:0x07` means the extension read passed coherence checks and all three
measurements are valid. It is sampled independently of control feedback.

## Core split and LED

Core 0 initializes sensors, handles Hall interrupts and publishes fast feedback
and its legacy mirror. Core 1 reads auxiliary ADC inputs every 10 ms, publishes
the diagnostic extension, and handles LED and USB logging. Chip temperature and
logs update every 250 ms. USB writes use a zero timeout and can drop text when
the host does not receive it; fast publication runs independently.

Signal loss takes precedence with red blinking at 4 Hz. With no fault the LED
is steady green at half brightness. Otherwise it advances every 500 ms through
active conditions: purple for fuel temperature, yellow for voltage, red after
2 seconds without an I2C transaction, followed by a green heartbeat.

## Sources and tests

| Files | Responsibility |
| --- | --- |
| `sensors.c / sensors.h` | Frequency, EMA, baseline, zero hold, ADC and coherent in-memory measurement. |
| `telemetry.c / telemetry.h` | Publication of coherent I2C blocks and legacy mirror. |
| `start.c / start.h` | Portable app entry and core scheduling. |
| `led.c / led.h` | Status indication. |
| `config.h`, `hardwareConfig.h` | Timing, thresholds, pins and analog circuit parameters. |
| `../common/adjustometer_feedback.h` | Shared feedback representation and wire encoding. |

Host tests cover sensor behavior, baseline drift, zero hold, signal loss, LED
states and readers interleaving with register publication. ECU tests cover
frame decoding, freshness, timestamp wrap, retries and control shutdown.

```bash
cmake -S src/Adjustometer -B src/Adjustometer/build_test
cmake --build src/Adjustometer/build_test --parallel
ctest --test-dir src/Adjustometer/build_test --output-on-failure
```

## Firmware build

Prepare the Arm toolchain and pinned Pico SDK with `runmefirst.sh`.
JaszczurHAL belongs in `<parent-of-Fiesta>/libraries/JaszczurHAL`.

```bash
cd src/Adjustometer
JH=../../../libraries/JaszczurHAL/vscode/entry/jh-vscode
"$JH" build --project "$PWD"
"$JH" build-debug --project "$PWD"
"$JH" upload --project "$PWD"
"$JH" upload-uf2 --project "$PWD"
"$JH" refresh-intellisense --project "$PWD"
```

`upload` matches the VS Code task (`Ctrl+Shift+2`); `upload-uf2` uses BOOTSEL.
`monitor` opens USB CDC (`Ctrl+Shift+3`). Port selection (`Ctrl+Shift+9`) updates
local settings; upload still verifies the module identity. Fiesta manifest and
USB-identity handling live in `src/common/scripts/`.

## License

Copyright (c) 2026 Marcin Jaszczur Kielesiński (jaszczurtd), jaszczurtd(at)tlen.pl

Permission is hereby granted, free of charge, to any person obtaining a copy of this software, hardware designs, and associated documentation files (the "Project"), to deal in the Project without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Project, subject to the following conditions:

**Attribution requirement:** All copies, modified versions, and redistributions of the Project - in whole or in part - must prominently include the following attribution in all source files, documentation, and any accompanying materials:

> Original author: **Marcin Jaszczur Kielesiński** (jaszczurtd), jaszczurtd(at)tlen.pl

This attribution must not be removed, obscured, or altered in any way.

THE PROJECT IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE PROJECT OR THE USE OR OTHER DEALINGS IN THE PROJECT.
