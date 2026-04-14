# VP37 Adjustometer

Actuator position transducer for the VP37 diesel injection pump, providing PID feedback to the engine control unit.

## How it works

The circuit uses the actuator position sensor coils built into the VP37 pump as the resonant element of a modified **Hartley oscillator**. The oscillation frequency varies in the range of **~22–37 kHz** depending on the actuator position (and thus fuel delivery).

The pulses from the oscillator are counted by an **RP2040** microcontroller (Raspberry Pi Pico), which:

### Oscillator hardware

The oscillator circuit consists of two stages:

1. **Hartley oscillator** - built around a **BC547B** (SMD equivalent: BC847B) NPN bipolar transistor. This is a general-purpose low-noise silicon transistor with high DC current gain (hFE 200–450), well suited for LC oscillator circuits in the tens-of-kHz range. It sustains oscillation using the pump's actuator coils as the resonant inductor.

2. **Pulse shaper** - built around a **2SK170GR** N-channel JFET. This is a low-noise, high-transconductance junction FET. Here it converts the sinusoidal oscillator output into clean rectangular pulses suitable for digital counting by the RP2040 GPIO interrupt. The "GR" grade denotes a mid-range IDSS (6–12 mA).

   The 2SK170 is discontinued and increasingly hard to source. Possible substitutes (match IDSS grade and pinout):

   | Part | Package | Notes |
   |------|---------|-------|
   | **2SK2145** | TO-92S | Toshiba drop-in replacement, very similar specs. |
   | **2N5457 / 2N5458** | TO-92 | ON Semi / Central Semi, widely available, slightly higher noise. |
   | **J113** | TO-92 | ON Semi, higher IDSS (~5–20 mA), may need bias adjustment. |
   | **MMBFJ310** | SOT-23 (SMD) | ON Semi, high-gm JFET, good SMD option. |
   | **SST310** | SOT-23 (SMD) | Microchip, equivalent to J310 in SMD. |
   | **MMBF5457 / MMBF5458** | SOT-23 (SMD) | SMD versions of 2N5457/5458. |
   | **SST5457** | SOT-23 (SMD) | Microchip SMD equivalent. |

### Signal processing

The RP2040:

1. Measures signal frequency in windows of 128 pulses (~3.5 ms at 37 kHz).
2. Determines the **baseline** - the reference frequency at the current rest position.
3. Computes **pulse** - frequency deviation from baseline (proportional to actuator displacement).
4. Applies **adaptive thermal compensation** to eliminate frequency drift caused by engine heating.
5. Exposes the result over the **I²C** bus to the engine control unit (ECU).

Additionally, the circuit reads:
- **Fuel temperature** from an NTC thermistor built into the VP37 pump.
- **Supply voltage** via a resistor divider (47 kΩ / 10 kΩ).

## Context - EDC15

This circuit is a **loose interpretation** of the specialised transducer inside the original Volkswagen EDC15 engine control unit. It is not a 1:1 copy for obvious reasons - there is no public documentation for that component, and the ICs themselves are not available for purchase. The general concept (LC resonance -> frequency -> feedback) is preserved, but the hardware/digital implementation, the thermal compensation algorithm, and I²C communication have been designed from scratch.

## Circuit schematic

The PCB layout and circuit schematic are located in the repository:

```
Fiesta_pcbs/vp37_adjustometer/
```

## I²C communication

- **Slave address:** `0x57`
- **Speed:** 400 kHz (Fast Mode)

### Register map

| Address | Size | Name | Description |
|---------|------|------|-------------|
| `0x00–0x01` | int16, big-endian | `PULSE` | Frequency deviation from baseline [Hz]. Positive = actuator moved toward higher fuel delivery. 0 = rest position. |
| `0x02` | uint8 | `VOLTAGE` | Supply voltage in tenths of a volt. E.g. 135 = 13.5 V. |
| `0x03` | uint8 | `FUEL_TEMP` | Fuel temperature in °C (0–255). |
| `0x04` | uint8 | `STATUS` | Module state: `0` = OK, `1` = signal lost, `2` = baseline calibration in progress. |

## Code structure

| File | Description |
|------|-------------|
| `Adjustometer.ino` | Arduino entry point - delegates to `start.c`. |
| `start.c / start.h` | Both RP2040 core initialisation, Core1 main loop (I²C + diagnostic logging). |
| `sensors.c / sensors.h` | Pulse-counting ISR (Core0), frequency measurement, baseline calibration, thermal compensation, ADC reads (temperature, voltage). |
| `config.h` | Main configuration constants - thresholds, coefficients, I²C register map. |
| `hardwareConfig.h` | Pin assignments, voltage divider parameters, NTC resistor values. |
| `hal_project_config.h` | JaszczurHAL library configuration (disabling unused modules). |
| `scripts/` | Developer tools: upload, serial monitor, IntelliSense refresh, board selector. |

The project uses the **JaszczurHAL** library as a hardware abstraction layer (GPIO, ADC, I²C slave, watchdog, timers).

### Core assignment

- **Core0:** GPIO ISR handler (counting pulses from the Hartley oscillator), watchdog.
- **Core1:** Main loop - ADC reads (temperature, voltage), I²C register updates, diagnostic logging.

## Thermal compensation

The circuit is **thermally compensated**. Engine heating causes oscillator frequency drift (coil inductance changes with temperature), which without compensation would produce a false actuator movement signal.

Compensation operates in two phases:

1. **Fallback phase** (ΔT < `ADAPTIVE_COMP_MIN_DT_C`): a fixed coefficient `THERMAL_COMP_HZ_PER_C_X10` is used. It defaults to **0** because the drift direction (positive or negative) varies between engine sessions - a non-zero fallback would be destructive when the actual drift sign is reversed.

2. **Adaptive phase** (ΔT ≥ `ADAPTIVE_COMP_MIN_DT_C`): the ISR computes the actual drift rate from observed data `(filteredHz − baseline) / ΔT` and smooths it with an EMA filter. This coefficient is then used for real-time pulse correction.

## Key configuration constants

### Frequency measurement (`sensors.c`)

| Constant | Value | Description | Tuning |
|----------|-------|-------------|--------|
| `ADJUSTOMETER_PULSE_WINDOW` | 128 | Number of pulses per measurement window. Larger = lower quantisation error but higher latency. At 37 kHz: 128 pulses ≈ 3.5 ms. | Increasing to 256 halves noise but doubles latency to ~7 ms. |
| `ADJUSTOMETER_EMA_SHIFT` | 3 | Frequency EMA filter: new sample weight = 1/2³ = 12.5%. | Increasing smooths more but slows response to fast changes. |

### Baseline calibration (`config.h`)

| Constant | Value | Description | Tuning |
|----------|-------|-------------|--------|
| `BASELINE_MIN_TIME_MS` | 80 | Minimum data collection time for baseline. | Too short -> noisy baseline. Too long -> delayed readiness. |
| `BASELINE_MAX_TIME_MS` | 250 | Timeout - if baseline hasn't converged, use the current estimate. | |
| `BASELINE_LOCK_TOLERANCE_HZ` | 12 | Maximum allowed difference between a sample and the estimate to count as stable. | Lowering forces better convergence but may fail to lock on a noisy signal. |
| `BASELINE_LOCK_WINDOWS` | 6 | Number of consecutive stable windows required to lock. | |

### Near-zero noise suppression (`config.h`)

| Constant | Value | Description | Tuning |
|----------|-------|-------------|--------|
| `ZERO_HOLD_ENTER_HZ` | 20 | Threshold to enter zero-hold (pulse -> 0). Must be above the noise floor (~15 Hz). | If p oscillates ±15 Hz at rest, raise this threshold. |
| `ZERO_HOLD_EXIT_HZ` | 25 | Threshold to exit zero-hold (consider movement as real). | Must be > ENTER, but not too large - otherwise the circuit becomes insensitive to small movements. |
| `ZERO_HOLD_RELEASE_WINDOWS` | 2 | Number of consecutive windows that must exceed EXIT to release zero-hold. | Increasing to 3–4 improves resilience to occasional spikes. |

### Thermal compensation (`config.h`)

| Constant | Value | Description | Tuning |
|----------|-------|-------------|--------|
| `THERMAL_COMP_HZ_PER_C_X10` | 0 | Fallback Hz/°C × 10 before adaptive activation. 0 = no compensation below the ΔT threshold. | Set non-zero only if the drift direction is consistent. |
| `THERMAL_COMP_EMA_SHIFT` | 8 | ISR temperature smoothing (τ ≈ 0.44 s). | Decreasing -> faster response, but ±1 °C ADC jitter leaks into compensation. |
| `ADAPTIVE_COMP_MIN_DT_C` | 4 | ΔT in °C required to activate adaptive compensation. | Too small -> noisy first estimate. Too large -> long time to activation. |
| `ADAPTIVE_COMP_EMA_SHIFT` | 6 | Adaptive coefficient smoothing (τ ≈ 64 windows). | Decreasing -> faster convergence, more noise. |
| `ADAPTIVE_COMP_MIN/MAX_COEFF_X10` | 5 / 200 | Allowed coefficient range: 0.5–20.0 Hz/°C. | Limits the impact of erroneous measurements. |

### Signal loss detection (`sensors.h`)

| Constant | Value | Description | Tuning |
|----------|-------|-------------|--------|
| `SIGNAL_LOSS_MULTIPLIER` | 3 | Dynamic timeout = period × multiplier. | |
| `SIGNAL_LOSS_MIN_US` | 5000 | Lower timeout bound (5 ms). Prevents false loss detection caused by ISR delays (USB). | Reducing below 2 ms causes false losses on Core0 with USB CDC. |
| `SIGNAL_LOSS_MAX_US` | 200000 | Upper timeout bound (200 ms). | |

## Supply voltage range

The circuit is designed for stable operation in the **8–25 V** range, covering typical 12 V automotive voltages including charging spikes. In practice, the circuit remains stable down to **5 V**, which should allow engine cranking even with a weak battery (voltage dips during starter engagement).

## Building

Requirements:
- `arduino-cli` with the `rp2040:rp2040` board package installed
- **JaszczurHAL** library in the sketchbook/libraries path

```bash
arduino-cli compile --fqbn rp2040:rp2040:rpipico \
  --libraries /path/to/libraries \
  --warnings all .
```

## License

Copyright (c) 2026 Marcin Jaszczur Kielesiński (jaszczurtd), jaszczurtd(at)tlen.pl

Permission is hereby granted, free of charge, to any person obtaining a copy of this software, hardware designs, and associated documentation files (the "Project"), to deal in the Project without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Project, subject to the following conditions:

**Attribution requirement:** All copies, modified versions, and redistributions of the Project - in whole or in part - must prominently include the following attribution in all source files, documentation, and any accompanying materials:

> Original author: **Marcin Jaszczur Kielesiński** (jaszczurtd), jaszczurtd(at)tlen.pl

This attribution must not be removed, obscured, or altered in any way.

THE PROJECT IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE PROJECT OR THE USE OR OTHER DEALINGS IN THE PROJECT.
