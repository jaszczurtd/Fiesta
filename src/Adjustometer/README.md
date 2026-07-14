# VP37 Adjustometer

Actuator position transducer for the VP37 diesel injection pump, providing feedback to the engine control unit.

This README describes the current state of the sources in `src/Adjustometer`.
If this text ever diverges from the code, the source of truth is:

- `sensors.c / sensors.h`
- `start.c`
- `led.c`
- `config.h`
- `hardwareConfig.h`
- `../common/adjustometer_protocol.h`
- `firmware_entry.h`
- `tests/test_sensors.cpp`
- `tests/test_sensors_internal.cpp`
- `tests/test_led.cpp`

## How it works

The circuit uses the actuator position sensor coils built into the VP37 pump as the resonant element of a modified Hartley oscillator. The oscillation frequency varies roughly in the **22-37 kHz** range depending on actuator position.

The pulses from the oscillator are counted by an RP2040 microcontroller, which currently performs:

1. Frequency measurement in windows of 128 pulses.
2. Baseline convergence with tracking, lock criteria, and post-lock verification.
3. Pulse derivation as filtered frequency deviation from baseline.
4. Near-zero suppression with zero-hold hysteresis.
5. Signal-loss detection.
6. Exposure of processed values over I2C to the ECU.

Additionally, the module reads:

- fuel temperature from the NTC thermistor built into the VP37 pump,
- supply voltage via a resistor divider.

Important current-state note:

- the current sources do **not** implement active thermal compensation of the pulse signal,
- fuel temperature is still sampled, filtered, reported over I2C, and used for diagnostics,
- the ECU now uses that fuel-temperature value as a proxy for actuator-coil
  temperature and expands only the positive VP37 control headroom when the
  pump is warm,
- raw oscillator frequency, baseline, signed frequency displacement, and
  RP2040 die temperature are available in an optional versioned telemetry
  extension.

### Oscillator hardware

The oscillator circuit consists of two stages:

1. Hartley oscillator built around a **BC547B** (SMD equivalent: BC847B) NPN transistor.
2. Pulse shaper built around a **2SK170GR** N-channel JFET, converting the sinusoidal oscillator output into clean digital pulses for the RP2040 GPIO interrupt.

The 2SK170 is discontinued and increasingly hard to source. Possible substitutes (match IDSS grade and pinout):

| Part | Package | Notes |
|------|---------|-------|
| **2SK2145** | TO-92S | Toshiba drop-in replacement, very similar specs. |
| **2N5457 / 2N5458** | TO-92 | Widely available, slightly higher noise. |
| **J113** | TO-92 | Higher IDSS, may need bias adjustment. |
| **MMBFJ310** | SOT-23 | Good SMD option. |
| **SST310** | SOT-23 | SMD equivalent to J310. |
| **MMBF5457 / MMBF5458** | SOT-23 | SMD versions of 2N5457/5458. |
| **SST5457** | SOT-23 | Microchip SMD equivalent. |

## Context - EDC15

This circuit is a loose interpretation of the specialised position-feedback concept used around VP37/EDC15. It is not a 1:1 copy of the OEM solution.

What is preserved conceptually:

- pump-coil-based resonant sensing,
- frequency-derived feedback,
- external ECU consumption of a processed feedback signal.

What is different in this project:

- the implementation is built around an external RP2040 module,
- feedback is exported digitally over I2C,
- the current firmware exports a non-negative pulse magnitude,
- thermal compensation is not performed inside Adjustometer; actuator-drive
  compensation belongs to the ECU control loop.

## Circuit schematic

The PCB layout and circuit schematic are located in:

```text
Fiesta_pcbs/vp37_adjustometer/
```

## I2C communication

- slave address: `0x57`
- speed: 400 kHz

### Register map

| Address | Size | Name | Description |
|---------|------|------|-------------|
| `0x00-0x01` | int16, big-endian | `PULSE` | Current firmware exports the absolute magnitude of frequency deviation from baseline in Hz. In practice this is non-negative, even though the register format remains signed `int16` for ECU compatibility. |
| `0x02` | uint8 | `VOLTAGE` | Supply voltage in tenths of a volt. Example: `135 = 13.5 V`. |
| `0x03` | uint8 | `FUEL_TEMP` | Fuel temperature in whole degrees C, `0-255`. |
| `0x04` | uint8 | `STATUS` | Status bitmask. `0x00 = all OK`. |
| `0x05` | uint8 | `EXT_VERSION` | Optional telemetry-extension version. Current value: `1`. |
| `0x06` | uint8 | `EXT_SEQ_BEGIN` | Snapshot sequence; valid when equal to `EXT_SEQ_END` and even. |
| `0x07` | uint8 | `EXT_FLAGS` | Validity bits for signal, baseline, and RP2040 temperature. |
| `0x08-0x0B` | uint32, big-endian | `SIGNAL_HZ` | Filtered absolute oscillator frequency in hertz. |
| `0x0C-0x0F` | uint32, big-endian | `BASELINE_HZ` | Locked oscillator baseline in hertz. |
| `0x10-0x13` | int32, big-endian | `SIGNED_DELTA_HZ` | Signed `SIGNAL_HZ - BASELINE_HZ`, without `abs()` or zero-hold. |
| `0x14-0x15` | int16, big-endian | `CHIP_TEMP_DECI_C` | Approximate RP2040 die temperature in `0.1 C` units. |
| `0x16` | uint8 | `EXT_SEQ_END` | End sequence for coherent-snapshot validation. |

Registers `0x00-0x04` retain their original layout. The ECU reads the
extension separately and treats it as optional diagnostic data, so the VP37
control path remains compatible with Adjustometer firmware that implements
only the legacy block.

The extension is refreshed every `10 ms` after baseline readiness. The
RP2040 die-temperature sample is refreshed every `250 ms`. During an update,
`EXT_SEQ_BEGIN` is odd. A reader accepts a snapshot only when
`EXT_SEQ_BEGIN == EXT_SEQ_END` and the matching value is even. This prevents a
partially replaced multi-byte value from being interpreted as a coherent
sample.

`EXT_FLAGS` uses the following bits:

| Bit | Mask | Meaning |
|-----|------|---------|
| 0 | `0x01` | `SIGNAL_HZ` is valid. |
| 1 | `0x02` | `BASELINE_HZ` and `SIGNED_DELTA_HZ` are valid. |
| 2 | `0x04` | `CHIP_TEMP_DECI_C` is valid. |

### ECU consumption and diagnostic logs

The ECU keeps the two protocol paths separate:

- the normal VP37 controller still reads only `0x00-0x04` and uses `PULSE` as
  its feedback value,
- the `0x05-0x16` extension is read separately every `500 ms` by the VP37
  diagnostic path,
- extension read/version/sequence failures do not change legacy `commOk`, the
  legacy communication-error counter, or the PWM controller input.

ECU diagnostics are emitted as two lines. The first describes controller
state; the second mirrors the Adjustometer measurements:

```text
ECU: VP37 thr:... des:... adj:... V:... t:... pwm:... err:... ff:... corr:... lim+:... sat+:... nom:...
ECU: VP37 ADJ p:... f:...Hz d:... v:... ft:... tc:... s:... bl:... ext:... fl:0x..
```

`ext:1` means a fresh version-1 snapshot passed the coherence checks.
`fl:0x07` means signal, baseline/signed delta, and RP2040 temperature are all
valid. The voltage field remains encoded in `0.1 V`; for example `v:144`
means `14.4 V`.

The ECU currently uses fuel temperature for actuator-drive compensation, not
for modifying `SIGNAL_HZ`, `BASELINE_HZ`, `SIGNED_DELTA_HZ`, or legacy
`PULSE`. Above the `22 °C` reference, it estimates the increase in copper-coil
resistance and expands only the positive PID correction limit from the cold
`220` PWM counts up to a hard maximum of `340`. The negative limit remains
`-220`. Invalid temperature/status data or lost Adjustometer communication
falls back to the original cold limit. Voltage compensation remains a
separate `12 V / measured voltage` scaling step in the ECU.

#### STATUS register bitmask

| Bit | Mask | Name | Description |
|-----|------|------|-------------|
| 0 | `0x01` | `SIGNAL_LOST` | No oscillation detected. |
| 1 | `0x02` | `FUEL_TEMP_BROKEN` | Fuel temperature sensor reads as broken / implausible. |
| 2 | `0x04` | `BASELINE_PENDING` | Baseline calibration has not yet completed. |
| 3 | `0x08` | `VOLTAGE_BAD` | Supply voltage out of range (`< 8.0 V` or `> 15.0 V`). |

Multiple bits can be set simultaneously.

### LED indicator

The LED behavior is source-driven by `led.c` and currently works as follows:

- `SIGNAL_LOST` overrides everything else with red blinking at 4 Hz.
- If there is no signal-loss condition and no active faults, the LED is steady green at half brightness.
- Otherwise the LED cycles every 500 ms through a sequence built from active conditions in this order:
  - purple if `FUEL_TEMP_BROKEN` is active,
  - yellow if `VOLTAGE_BAD` is active,
  - red if no I2C transaction has been seen for 2 s,
  - green as the final heartbeat color.

Examples:

- fuel-temp fault only: `purple -> green`
- voltage fault only: `yellow -> green`
- no I2C only: `red -> green`
- combined faults: additive sequence, for example `purple -> yellow -> red -> green`

## Current source-based functionality

### Core split

- `firmware_entry.h` declares the module-owned `initialization()` / `looper()`
  contract. JaszczurHAL generates the temporary RP2040 `.ino` adapter in the
  build directory; there is no source-owned `Adjustometer.ino`.
- Core0 initialises sensors and hosts the GPIO interrupt used for pulse counting.
- Core1 updates the I2C registers, updates the LED state machine, and handles diagnostic logging.

### Frequency and pulse path

The current `sensors.c` implementation does this:

- counts oscillator edges on every falling GPIO interrupt,
- computes frequency every 128 pulses,
- filters frequency with an integer EMA (`ADJUSTOMETER_EMA_SHIFT = 3`),
- converges baseline using tracking and stability windows,
- verifies the captured baseline for `1000 ms` and restarts convergence if slow drift exceeds `500 Hz`,
- computes pulse relative to baseline,
- suppresses small near-zero residuals using zero-hold hysteresis,
- returns `abs(pulse)` from `getAdjustometerPulses()`.

This means the current exported `PULSE` value is a magnitude-oriented signal, not a signed bidirectional displacement.

### Baseline readiness

Current baseline logic is controlled by these constants:

- `ADJUSTOMETER_BASELINE_MIN_TIME_MS = 80`
- `ADJUSTOMETER_BASELINE_MAX_TIME_MS = 250`
- `ADJUSTOMETER_BASELINE_LOCK_TOLERANCE_HZ = 12`
- `ADJUSTOMETER_BASELINE_LOCK_WINDOWS = 6`
- `ADJUSTOMETER_BASELINE_VERIFY_MS = 1000`
- `ADJUSTOMETER_BASELINE_VERIFY_DRIFT_HZ = 500`

Note:

- `ADJUSTOMETER_WARMUP_MS` exists in `config.h` as a documented design constant,
- but the current runtime path is governed by baseline convergence and verification logic rather than by an explicit startup delay call in the live source.

### Zero-hold and signal loss

Current near-zero suppression uses:

- `ADJUSTOMETER_ZERO_HOLD_ENTER_HZ = 40`
- `ADJUSTOMETER_ZERO_HOLD_EXIT_HZ = 50`
- `ADJUSTOMETER_ZERO_HOLD_RELEASE_WINDOWS = 2`

Current signal-loss detection uses a dynamic timeout:

- `timeout = period * 3`,
- clamped to `10 ms .. 200 ms`.

If signal loss is detected, `getAdjustometerPulses()` returns `0` and the `SIGNAL_LOST` status bit is set.

### Fuel temperature and voltage

Fuel temperature and supply voltage are read on Core1 and filtered with a simple ADC EMA:

- `ADC_EMA_SHIFT = 3`

Current purpose of fuel-temperature measurement:

- expose current pump fuel temperature over I2C,
- detect a broken temperature sensor,
- provide the ECU with the current proxy used for positive VP37 thermal
  headroom.

Current purpose of supply-voltage measurement:

- expose the module supply voltage over I2C,
- flag out-of-range voltage with `VOLTAGE_BAD`.

## Code structure

| File | Description |
|------|-------------|
| `firmware_entry.h` | Module entry contract consumed by the generated JaszczurHAL adapter. |
| `start.c / start.h` | RP2040 core initialisation, Core1 loop, I2C register publishing, LED updates. |
| `sensors.c / sensors.h` | Pulse ISR, frequency measurement, baseline logic, zero-hold, ADC reads, status generation. |
| `led.c / led.h` | LED state machine and fault indication logic. |
| `config.h` | Runtime constants for baseline, verification, zero-hold, telemetry cadence, and ADC filtering. |
| `../common/adjustometer_protocol.h` | Shared, versioned I2C register map used by Adjustometer and ECU. |
| `hardwareConfig.h` | Pin assignments, divider ratios, NTC constants. |
| `tests/test_sensors.cpp` | Host tests for baseline, pulse path, status bits, zero-hold, signal loss. |
| `tests/test_sensors_internal.cpp` + `sensors_internal_bridge.cpp` | Internal-state tests for convergence, verification, EMA, and zero-hold transitions. |
| `tests/test_led.cpp` | Host tests for LED behavior. |

The project uses **JaszczurHAL** as the hardware abstraction layer.

## Tests

The Adjustometer module has its own host-side test suite (CMake/CTest path,
typically built under `build_test/`):

- `test_sensors`
- `test_led`
- `test_sensors_internal`

These tests validate the current source behavior for pulse measurement,
signed displacement, baseline, status bits, and LED signaling. ECU integration
tests additionally cover legacy-only compatibility, extension decoding,
unknown versions, and torn-snapshot rejection.

## Key configuration constants

### Frequency measurement

| Constant | Value | Meaning |
|----------|-------|---------|
| `ADJUSTOMETER_PULSE_WINDOW` | `128` | Pulses accumulated per frequency window. |
| `ADJUSTOMETER_EMA_SHIFT` | `3` | EMA smoothing for the measured oscillator frequency. |

### Baseline and verification

| Constant | Value | Meaning |
|----------|-------|---------|
| `ADJUSTOMETER_BASELINE_MIN_TIME_MS` | `80` | Minimum convergence time before lock is allowed. |
| `ADJUSTOMETER_BASELINE_MAX_TIME_MS` | `250` | Maximum convergence time before force-lock to estimate. |
| `ADJUSTOMETER_BASELINE_LOCK_TOLERANCE_HZ` | `12` | Stability threshold for lock counting. |
| `ADJUSTOMETER_BASELINE_LOCK_WINDOWS` | `6` | Required stable windows to lock. |
| `ADJUSTOMETER_BASELINE_VERIFY_MS` | `1000` | Post-lock verification time. |
| `ADJUSTOMETER_BASELINE_VERIFY_DRIFT_HZ` | `500` | Drift threshold that restarts convergence. |

### Zero-hold

| Constant | Value | Meaning |
|----------|-------|---------|
| `ADJUSTOMETER_ZERO_HOLD_ENTER_HZ` | `40` | Enter near-zero hold. |
| `ADJUSTOMETER_ZERO_HOLD_EXIT_HZ` | `50` | Leave near-zero hold. |
| `ADJUSTOMETER_ZERO_HOLD_RELEASE_WINDOWS` | `2` | Consecutive windows required to release hold. |

### Signal loss

| Constant | Value | Meaning |
|----------|-------|---------|
| `ADJUSTOMETER_SIGNAL_LOSS_MULTIPLIER` | `3` | Dynamic timeout multiplier. |
| `ADJUSTOMETER_SIGNAL_LOSS_MIN_US` | `10000` | Lower clamp for signal-loss timeout. |
| `ADJUSTOMETER_SIGNAL_LOSS_MAX_US` | `200000` | Upper clamp for signal-loss timeout. |

## Supply-voltage measurement note

The I2C voltage register is encoded as `0.1 V` units up to `255 = 25.5 V`, but the current hardware divider in `hardwareConfig.h` is `47k / 10k`, which gives a practical ADC input ceiling of about `18.8 V` at `3.3 V` full scale.

The current status logic treats voltage outside `8.0 .. 15.0 V` as bad.

## Building

Requirements:
- `arduino-cli` with the `rp2040:rp2040` board package installed
- **JaszczurHAL** at `<parent-of-Fiesta>/libraries/JaszczurHAL`, matching the
  repository-wide layout used by CMake and `jh-vscode`

```bash
cd src/Adjustometer
JH=../../../libraries/JaszczurHAL/vscode/entry/jh-vscode
"$JH" build --project "$PWD"
"$JH" build-debug --project "$PWD"
"$JH" upload --project "$PWD"
"$JH" upload-uf2 --project "$PWD"
"$JH" refresh-intellisense --project "$PWD"
```

Notes:

- `jh-vscode upload --project "$PWD"` is the same path used by the VS Code upload task / `Ctrl+Shift+2`.
- `jh-vscode upload-uf2 --project "$PWD"` is the BOOTSEL mass-storage path.
- `jh-vscode monitor --project "$PWD"` is the same path used by the VS Code monitor task / `Ctrl+Shift+3`.
- `Ctrl+Shift+9` updates `jaszczurhal.uploadPort` in `.vscode/settings.json`; identity-guarded upload still verifies the selected `/dev/serial/by-id` target before flashing.
- Fiesta-specific manifest validation remains in `src/common/scripts/`, which also applies the module USB identity (`Jaszczur` / `Fiesta Adjustometer`) consistently across build paths.

## License

Copyright (c) 2026 Marcin Jaszczur Kielesiński (jaszczurtd), jaszczurtd(at)tlen.pl

Permission is hereby granted, free of charge, to any person obtaining a copy of this software, hardware designs, and associated documentation files (the "Project"), to deal in the Project without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Project, subject to the following conditions:

**Attribution requirement:** All copies, modified versions, and redistributions of the Project - in whole or in part - must prominently include the following attribution in all source files, documentation, and any accompanying materials:

> Original author: **Marcin Jaszczur Kielesiński** (jaszczurtd), jaszczurtd(at)tlen.pl

This attribution must not be removed, obscured, or altered in any way.

THE PROJECT IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE PROJECT OR THE USE OR OTHER DEALINGS IN THE PROJECT.
