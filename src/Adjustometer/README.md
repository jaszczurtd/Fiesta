# VP37 Adjustometer

[Polski](README.pl.md) · [Architecture](../../ARCHITECTURE.md)

Actuator position transducer for the VP37 diesel injection pump, providing
frequency-derived feedback to the ECU over I2C.

## Power and startup order

1. Check the [board connections](../../Fiesta_pcbs/vp37_adjustometer/adjustometer/README.md). Both 5 V and 12 V are required, with a common ground; the 8 V regulator supplies only the oscillator. Disconnect external 5 V before powering RP2040-Zero through USB.
2. Turn off the ECU actuator drive and wait for the mechanism to settle. Start or reset Adjustometer.
3. Wait for a valid baseline near zero. Then reset the ECU to start travel calibration.

Opening or closing USB CDC may reset a board; use the same order afterwards. Signal loss
publishes zero, so zero alone does not prove valid feedback. Check status and freshness too.

## Read the LED

Signal loss takes precedence with red blinking at 4 Hz. With no fault the LED
is steady green at half brightness. Otherwise it advances every 500 ms through
active conditions: purple for fuel temperature, yellow for voltage, red after
2 seconds without an I2C transaction, followed by a green heartbeat.

## Firmware build

Prepare the environment with the [build guide](../../doc/BUILDING.md), then open
`src/Adjustometer`. `Project: Build` builds firmware; `Project: Upload` checks module
identity and uploads it. For a board already in BOOTSEL, use `Project: Upload (UF2 /
BOOTSEL)` instead.

When changing PWM frequency, build matching ECU and Adjustometer images. Prepare a
SerialConfigurator manifest separately as described in the build guide. The serial monitor
uses USB CDC; observe the reset order above.

## Measurement

The pump sensor coils form the resonant element of a modified Hartley oscillator.
Frequency varies roughly within 22-37 kHz with actuator position. Hardware
period capture in JaszczurHAL supplies blocks of 32 complete periods; the
Adjustometer combines four blocks into a 128-period window. RP2040 captures
timestamps through PIO and DMA, without a GPIO interrupt for each edge. The
exported `PULSE` is the absolute filtered deviation from the locked baseline,
with near-zero hysteresis. It is a position observable in Hz; calibration and
actuator-drive compensation belong to the ECU.

After baseline acquisition the window slides by 32 periods
(`ADJUSTOMETER_SLIDING_WINDOW`, default 1) and the published frequency is the
time-weighted mean of the raw windows over exactly one VP37 drive period
(`ADJUSTOMETER_PWM_FILTER_US`, by default one period of `VP37_PWM_FREQUENCY_HZ`
from `src/common/vp37_drive_config.h`, shared with the ECU). Averaging over one
drive period cancels the ripple the PWM drive imprints on the actuator without
the lag of a recursive filter: a step settles within one period and a ramp lags
by half a period, 2.5 ms at the default 200 Hz. A drive-frequency override has to be
given to both firmware builds. `ADJUSTOMETER_PWM_FILTER_US=0` restores the
fractional EMA of the sliding window (weight 71/1024, zero release after eight
short updates). Baseline acquisition keeps the original cadence and integer
EMA.
`ADJUSTOMETER_FEEDBACK_MIN_PUBLISH_MS` optionally spaces fast I2C publications;
its default is zero, and status changes are published immediately. The HAL snapshot
keeps each I2C read coherent while new measurements are published.

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
consecutive windows with the same sign. Signal loss uses the latest completed capture block and three periods of the
filtered frequency, clamped to 10-200 ms. Capture errors discard the partial
window and invalidate feedback; overflow or a peripheral fault causes capture
to restart after 100 ms. A locked baseline is retained; interrupted startup
verification starts again. A lost signal produces zero pulse
and sets `SIGNAL_LOST`; consumers must inspect status and freshness.

## I2C and auxiliary sensors

The slave address is `0x57`; ECU uses 400 kHz. Feedback and diagnostic blocks
are published atomically. `HAL_ENABLE_I2C_SLAVE_SNAPSHOT` freezes the map for
each read; sequence markers and ECU freshness checks remain in use. Wire definitions are shared in
[adjustometer_protocol.h](../common/adjustometer_protocol.h).

| Registers | Purpose |
| --- | --- |
| `0x00..0x04` | Reserved; no measurements. |
| `0x05..0x12` | Version-2 coherent diagnostics: filtered frequency, baseline, RP2040 temperature and validity flags. |
| `0x13..0x16` | Reserved. |
| `0x17..0x34` | Coherent control feedback, including raw frequency and measurement freshness. |

ECU and VP37TestBench read version-1 control feedback at its fixed address
`0x17`. The five-byte reader is no longer supported. Diagnostic readers must
use version 2 and derive signed deviation as filtered frequency minus baseline
from the same read. A different diagnostic version is rejected without
affecting control feedback.

The reader gives each sample a 20 ms age budget. It includes the reported
publication age, the complete I2C read and time elapsed on the ECU clock.
Repeated reads of the same sample cannot renew the budget; a later publication
reporting greater age can shorten it. The wire `ageUs` remains the age at
publication. Unsynchronized clocks cannot reveal the delay between publication
and the first read, so this check does not measure the complete end-to-end latency.

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

## Core responsibilities

Core 0 initializes sensors, drains hardware capture every 1 ms and publishes
fast feedback. Core 1 reads auxiliary ADC inputs every 10 ms, publishes
the diagnostic extension, and handles LED and USB logging. Chip temperature and
logs update every 250 ms. USB writes use a zero timeout and can drop text when
the host does not receive it; fast publication runs independently.

## Sources and tests

| Files | Responsibility |
| --- | --- |
| `sensors.c / sensors.h` | Frequency, EMA, baseline, zero hold, ADC and coherent in-memory measurement. |
| `telemetry.c / telemetry.h` | Publication of coherent feedback and diagnostic I2C blocks. |
| `start.c / start.h` | Portable app entry and core scheduling. |
| `led.c / led.h` | Status indication. |
| `device_tree.json` | Board, pins, the ECU bus and the status LED. |
| `config.h`, `hardwareConfig.h` | Timing, thresholds and analog circuit parameters. |
| `../common/adjustometer_feedback.h` | Shared feedback representation and wire encoding. |

Host tests cover sensor behavior, baseline drift, zero hold, signal loss, LED
states and readers interleaving with register publication. ECU tests cover
frame decoding, freshness, timestamp wrap, retries and control shutdown.

```bash
cmake -S src/Adjustometer -B src/Adjustometer/build_test
cmake --build src/Adjustometer/build_test --parallel
ctest --test-dir src/Adjustometer/build_test --output-on-failure
```

## License

Copyright (c) 2026 Marcin Jaszczur Kielesiński (jaszczurtd), jaszczurtd(at)tlen.pl

Permission is hereby granted, free of charge, to any person obtaining a copy of this
software, hardware designs, and associated documentation files (the "Project"), to deal in
the Project without restriction, including without limitation the rights to use, copy,
modify, merge, publish, distribute, sublicense, and/or sell copies of the Project, subject to
the following conditions:

**Attribution requirement:** All copies, modified versions, and redistributions of the
Project - in whole or in part - must prominently include the following attribution in all
source files, documentation, and any accompanying materials:

> Original author: **Marcin Jaszczur Kielesiński** (jaszczurtd), jaszczurtd(at)tlen.pl

This attribution must not be removed, obscured, or altered in any way.

THE PROJECT IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE PROJECT OR THE USE OR OTHER DEALINGS IN THE PROJECT.
