# Fiesta - System Architecture

This document describes the architecture of the Fiesta firmware ecosystem:
the modules it is composed of, what each module is responsible for, how the
modules talk to each other and to the rest of the vehicle, and what external
dependencies they rely on. It complements [`README.md`](README.md), which
focuses on setup, safety guidance, and build procedures.

---

## 1. System scope

Fiesta is **not** a single firmware binary. It is a multi-module electronic
stack for a Ford Fiesta 1.8 (M)TDDI with custom electronics replacing parts
of the OEM wiring. Each firmware module has its own binary, its own PCB
(see [`Fiesta_pcbs/`](Fiesta_pcbs/)), and communicates with the others over
well-defined physical buses (CAN, I²C). A **desktop companion**, the
Fiesta Serial Configurator, sits off-vehicle and talks to each firmware
module over USB CDC for diagnostics, calibration, and flashing - it is a
full member of the family (§5.5).

The system replaces and augments the following vehicle functions:

- engine control (fuel injection via VP37, boost control, glow plugs, fans,
  heater, heated windshield),
- diagnostics (OBD-II / UDS over CAN, mimics Ford EEC-V),
- driver-facing instrumentation (dashboard gauges, display, buzzer),
- auxiliary telemetry (oil pressure, wheel speed, exhaust gas temperatures,
  GPS speed and time),
- off-vehicle runtime configuration and re-flashing via the Serial
  Configurator, over a per-module USB CDC serial session.

High-level module map (active firmware + desktop companion):

```
┌────────────────────────────────────────────────────────────────────────┐
│                         Fiesta electronic stack                        │
├────────────────────────────────────────────────────────────────────────┤
│                                                                        │
│     ┌──────────────┐                           ┌──────────────┐        │
│     │              │◄──────────────────────────┤              │        │
│     │     ECU      │      CAN (main)           │    Clocks    │        │
│     │  (RP2040,    │──────────────────────────►┤  (RP2040,    │        │
│     │  core-0 +    │                           │   dashboard) │        │
│     │  core-1)     │                           │              │        │
│     │              │                           └──────────────┘        │
│     │              │                                                   │
│     │              │◄──────────────────────────┐                       │
│     │              │                           │                       │
│     │              │      CAN (main)      ┌──────────────┐             │
│     │              │                      │ OilAndSpeed  │             │
│     │              │─────────────────────►┤  (RP2040)    │             │
│     │              │                      └──────────────┘             │
│     │              │                                                   │
│     │              │◄──── I²C (0x57) ─────┐                            │
│     │              │                      │                            │
│     │              │                   ┌─────────────┐                 │
│     │              │                   │Adjustometer │                 │
│     │              │                   │  (RP2040,   │                 │
│     │              │                   │ I²C slave)  │                 │
│     │              │                   └─────────────┘                 │
│     │              │                                                   │
│     │              │──── CAN (OBD-2) ───► OBD-II diagnostic port       │
│     │              │                                                   │
│     │              │──── I²C (0x38) ────► PCF8574 (relay expander)     │
│     │              │                                                   │
│     │              │──── UART ──────────► GPS receiver                 │
│     │              │                                                   │
│     │              │──── SPI ───────────► SD card (logging, legacy)    │
│     │              │                                                   │
│     │              │──── PWM / ADC / GPIO ► sensors + actuators        │
│     └──────┬───────┘                                                   │
│            │                                                           │
│            │  USB CDC (text HELLO session on ECU/Clocks/OilAndSpeed)   │
│            │  Additional encrypted layer later for flashing/settings   │
│            │  changes                                                  │
│            │                                                           │
│     ┌──────▼──────────────────────────────────────────────┐            │
│     │                                                     │            │
│     │        Fiesta Serial Configurator                   │            │
│     │        (Linux primary, Windows 10/11 secondary,     │            │
│     │         GTK-4 GUI + platform-neutral core,          │            │
│     │         off-vehicle)                                 │            │
│     │                                                     │            │
│     └─────────────────────────────────────────────────────┘            │
│                                                                        │
│     (Clocks and OilAndSpeed expose the same USB CDC session pattern    │
│     as ECU. Adjustometer is currently outside the primary              │
│     configurator/flashing flow.)                                       │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Hardware platform

All four active modules target **RP2040** (ARM Cortex-M0+ dual-core, 133 MHz,
264 KB SRAM, 2 MB flash) packaged as a Raspberry Pi Pico. The reasons for
picking this part:

- cheap, widely available, two cores,
- 8 PIO state machines (used for the engine Hall sensor and the VP37
  resonance capture),
- flash-backed emulated EEPROM used for persistent state (DTCs, config).

That said, the system is not locked to this silicon - the HAL abstraction
is designed so that a port to a different MCU is a relatively contained
effort rather than a rewrite.

The legacy [`src/Fiesta_clock`](src/Fiesta_clock/) module targets AVR and is
no longer under active development; it is kept for historical reference.

PCB assets for every module live under [`Fiesta_pcbs/`](Fiesta_pcbs/),
including schematics (`.pdf`), layouts, and the `pinout.txt` / `wirings.txt`
connector maps that tie firmware pin assignments to the physical 104-pin
loom.

---

## 3. Module inventory

### Active firmware modules (`src/`)

| Module | Language | Role | MISRA scope |
|---|---|---|---|
| [`ECU`](src/ECU/) | C (+ `.ino` wrapper) | engine control, diagnostics, actuator orchestration | **in scope** |
| [`Clocks`](src/Clocks/) | C++ | dashboard / instrument cluster rendering | out of scope |
| [`OilAndSpeed`](src/OilAndSpeed/) | C++ | oil pressure and wheel speed telemetry, EGT acquisition | out of scope |
| [`Adjustometer`](src/Adjustometer/) | C | VP37 pump-coil resonance feedback (I²C slave) | out of scope |

### Desktop companion

| Component | Target platforms | Role |
|---|---|---|
| Fiesta Serial Configurator | Linux (primary, Debian-like), Windows 10/11 (secondary); mobile out of scope | per-module runtime parameter configuration, firmware flashing via BOOTSEL/UF2, diagnostic log capture |

The configurator is a full family member, not a development-time utility.

### Completed / archived

- [`src/Fiesta_clock`](src/Fiesta_clock/) - standalone AVR clock/temperature
  display, finished project.
- [`legacy/`](legacy/) - archival sources (`DPF_main`, `AdaptiveLights`,
  `Fading`). Not built, not tested, kept for migration reference.

---

## 4. Shared foundation

All active modules are built on the same foundation, so the per-module
descriptions below only describe what is specific to each.

### 4.1 JaszczurHAL

`JaszczurHAL` ([github.com/jaszczurtd/JaszczurHAL](https://github.com/jaszczurtd/JaszczurHAL))
is a separate repository, cloned into `<parent-of-repo-root>/libraries/JaszczurHAL`
by [`bootstrap.sh`](src/ECU/scripts/bootstrap.sh). It provides:

- a HAL abstraction layer (I²C, CAN, GPIO, PWM, timers, ADC) with an
  RP2040-Arduino-backed production implementation and a mock backend used
  for host tests,
- decoupling of module code from any specific MCU architecture,
- utilities: soft-timer table, PID controller, KV store backed by emulated
  EEPROM, logging macros,
- Arduino host stubs (`Arduino.h`, `SPI.h`, `SD.h`) so that the firmware
  sources can be compiled by a host C/C++ compiler (GCC on Linux) for unit
  tests without the Arduino toolchain.

The `.ino` files in each module are deliberately thin wrappers around
`setup()`, `loop()`, and `loop1()`; everything else is regular C/C++ behind
the HAL. This is why the project is not a conventional Arduino application
and **will not** compile out of the box in the Arduino IDE (see
[README § Build and development](README.md#build-and-development)).

### 4.2 canDefinitions

`canDefinitions` ([github.com/jaszczurtd/canDefinitions](https://github.com/jaszczurtd/canDefinitions))
is the single source of truth for CAN frame IDs, signal layouts, and
scaling. It is shared across ECU, Clocks, and OilAndSpeed so that they
agree on the wire format without duplicating header files. It is cloned
into `<parent-of-repo-root>/libraries/canDefinitions` by `bootstrap.sh`
and is treated by the bootstrap as a disposable build dependency (force
reset to the remote default branch on every run).

### 4.3 Per-module layout convention

Every active module follows the same file layout:

```
src/<Module>/
├── <Module>.ino           # Arduino entry point (setup/loop/loop1)
├── start.{c,cpp}/.h       # init, soft-timer table, watchdog hookup
├── hardwareConfig.h       # pin/address constants (single source of truth)
├── hal_project_config.h   # per-module HAL feature flags
├── config.{c,cpp}/.h      # persistent configuration (KV store)
├── can.{c,cpp}/.h         # CAN TX/RX adapters
├── <domain logic files>
├── CMakeLists.txt         # host-test build
├── tests/                 # host unit tests (compiled as C++)
├── build_test/            # CMake build output (git-ignored)
└── scripts/               # thin wrappers: arduino-build.sh, upload-uf2.sh,
                           # refresh-intellisense.sh, select-board.sh,
                           # serial-persistent.py, serial-monitor.{sh,py}
```

---

## 5. Module details

### 5.1 ECU - [`src/ECU`](src/ECU/)

**Role.** The ECU is the only safety-critical module. It owns engine
control, fault management, and the OBD-II interface. It is the
**MISRA-C target**; see [`MISRA.md`](MISRA.md) for the current alignment
figures, migration status, and screening entry points.

**Dual-core split.**
- `core-0` runs the main control loop: soft-timer table, CAN TX/RX, actuator
  updates, OBD-II service handlers. All runtime-critical work.
- `core-1` runs the time-critical engine control path - VP37 servicing and
  the rest of the tight-loop engine logic.

Cross-core state is protected by dedicated mutexes (adjustometer snapshot,
PCF8574 shadow latch, DTC manager + its KV persistence). See the "dual-core
state synchronization pass" bullet in [`MISRA.md`](MISRA.md) for the list of
covered structures.

**Central state.** All per-module state is consolidated into a single
`ecu_context_t` struct (see [`ecuContext.h`](src/ECU/ecuContext.h)). There
are no free-floating globals for business state - modules read and write
through this struct, which is a deliberate move to make MISRA-C auditing
and ownership analysis tractable.

**Responsibility map.**

| File | Responsibility |
|---|---|
| [`start.c`](src/ECU/start.c) | init sequence, soft-timer registration, watchdog startup, reboot-snapshot readback |
| [`sensors.c`](src/ECU/sensors.c) | HC4051 mux sweep, ADC sampling, PCF8574 driver, adjustometer I²C reads |
| [`can.c`](src/ECU/can.c) | main-CAN frame packing and dispatch; uses `canDefinitions` IDs |
| [`obd-2.c`](src/ECU/obd-2.c) | OBD-II / UDS service handlers on the OBD-2 CAN controller; largest single file, active MISRA hotspot |
| [`obd-2_mapping.c`](src/ECU/obd-2_mapping.c) | mapping from OBD PIDs to ECU signals |
| [`dtcManager.c`](src/ECU/dtcManager.c) | DTC set/clear, persistence via KV store, retrieval for OBD responses |
| [`rpm.c`](src/ECU/rpm.c) | engine RPM via Hall sensor captured by PIO state machine |
| [`vp37.c`](src/ECU/vp37.c) | VP37 injection pump control: PID loop using adjustometer feedback |
| [`turbo.c`](src/ECU/turbo.c) | turbo boost control (N75 solenoid, MAP-based) |
| [`engineFan.c`](src/ECU/engineFan.c) | fan relay control with hysteresis |
| [`engineHeater.c`](src/ECU/engineHeater.c) | block-heater low/high relays |
| [`engineFuel.c`](src/ECU/engineFuel.c) | amount of fuel measurement |
| [`glowPlugs.c`](src/ECU/glowPlugs.c) | glow plug relay + lamp timing |
| [`heatedWindshield.c`](src/ECU/heatedWindshield.c) | heated-window relays with button latch |
| [`gps.c`](src/ECU/gps.c) | NMEA parsing over UART, time/date publication |
| [`config.c`](src/ECU/config.c) | persistent configuration via KV store |
| [`engineMaps.c`](src/ECU/engineMaps.c) | look-up tables (boost map, fueling map) |

**Hardware interfaces** (from [`hardwareConfig.h`](src/ECU/hardwareConfig.h)):

- **I²C** (`PIN_SDA=0`, `PIN_SCL=1` @ 400 kHz):
  - Adjustometer slave at `0x57` (registers `0x00–0x04`, see §6.2),
  - PCF8574 relay expander at `0x38` (bits 0–7 map to glow plugs, fan,
    heater HI/LO, glow-plug lamp, heated window L/P, VP37 enable).
- **CAN0** (main vehicle bus): SPI-attached controller, CS=GPIO 17, INT=15.
- **CAN1** (OBD-2 port): SPI-attached controller, CS=GPIO 6, INT=14.
- **SPI** (MISO=16, MOSI=19, SCK=18): shared between CAN0, CAN1, and the SD
  card (CS=26).
- **ADC**: `ADC_SENSORS_PIN=27` fed by a HC4051 analog mux (select pins
  11/12/13) giving 6 analog inputs - coolant temp (ch 0), oil temp (ch 1),
  throttle position (ch 2), air temp (ch 3), fuel level (ch 4), manifold/boost
  pressure (ch 5). `ADC_VOLT_PIN=28` reads ECU supply voltage through a
  ~47 kΩ / 10 kΩ divider.
- **PIO state machines**:
  - `PIO_INTERRUPT_HALL=7` - engine Hall sensor (RPM),
  - `PIO_VP37_RPM=9`, `PIO_VP37_ANGLE=5` - VP37 injection control outputs,
  - `PIO_TURBO=10` - N75 solenoid PWM,
  - `PIO_DPF_LAMP=8` - DPF warning lamp.
- **PWM**: `PWM_WRITE_RESOLUTION=11` (2047 levels); frequencies configured
  per output (`VP37_PWM_FREQUENCY_HZ`, `TURBO_PWM_FREQUENCY_HZ`,
  `ANGLE_PWM_FREQUENCY_HZ`).
- **UART** (RX=22, TX=21): GPS receiver.
- **GPIO**: heated-windows switch input on pin 20, status LED on pin 25.
- **Persistent store**: `ECU_EEPROM_SIZE_BYTES` (currently is 2048) bytes of flash-backed emulated EEPROM
  (`ECU_EEPROM_SIZE_BYTES`), used for DTCs and configuration.

**Timing model.** On RP2040, ECU does not use an RTOS. Work is scheduled by a soft-timer
table installed in `start.c`: each entry is a `(period, callback)` pair
invoked from the main loop. In addition, both RP2040 cores are used, and the second core is handling engine-related (VP37 / turbo), time-critical tasks. Typical cadences are high-rate sensor reads (~10 ms), medium-rate reads (~100 ms), CAN publish cycles, and slower
per-second housekeeping. 

### 5.2 Clocks - [`src/Clocks`](src/Clocks/)

**Role.** Driver-facing instrument cluster. Listens to the main CAN bus
and drives (a) the physical speedometer/tachometer/oil gauges with square
waves, (b) a TFT LCD for auxiliary readouts, and (c) a buzzer for
warnings.

**Responsibility map.**

| File | Responsibility |
|---|---|
| [`Cluster.cpp`](src/Clocks/Cluster.cpp) | square-wave generation for the factory speedometer/tachometer inputs |
| [`Gauge.h`](src/Clocks/Gauge.h), [`simpleGauge.cpp`](src/Clocks/simpleGauge.cpp), [`tempGauge.cpp`](src/Clocks/tempGauge.cpp), [`pressureGauge.cpp`](src/Clocks/pressureGauge.cpp) | gauge abstractions (generic / temperature / pressure scaling) |
| [`TFTExtension.cpp`](src/Clocks/TFTExtension.cpp) | SPI TFT driver and rendering helpers |
| [`logic.cpp`](src/Clocks/logic.cpp) | state machine mapping CAN signals to display/gauge/buzzer state |
| [`buzzer.cpp`](src/Clocks/buzzer.cpp), [`buzzerStrategy.cpp`](src/Clocks/buzzerStrategy.cpp) | tone generation + warning pattern strategy |
| [`engineFuel.cpp`](src/Clocks/engineFuel.cpp) | fuel-level aggregation for the cluster |
| [`can.cpp`](src/Clocks/can.cpp) | CAN RX filtering (shares IDs with ECU via `canDefinitions`) |
| [`peripherials.cpp`](src/Clocks/peripherials.cpp) | GPIO/PWM init |

**Hardware interfaces** (from [`hardwareConfig.h`](src/Clocks/hardwareConfig.h)):

- **SPI** (MISO=0, MOSI=3, SCK=2) drives both the CAN controller (CS=1,
  INT=4) and the TFT display (CS=6, RST=7, DC=8).
- **PWM outputs**: speed output on pin 9, tacho on pin 10, oil on pin 11,
  backlight brightness on pin 5, buzzer on pin 14.
- **GPIO**: RGB status LED on pin 16.

**Direction of data.** Clocks is primarily a CAN **consumer**. It does not
produce signals the ECU needs for control.

### 5.3 OilAndSpeed - [`src/OilAndSpeed`](src/OilAndSpeed/)

**Role.** A peripheral telemetry module. Provides two signals the ECU does
not read directly - oil pressure (resistive 0..10 bar sender) and ABS
wheel speed (frequency on a GPIO line) - and hosts two MCP9600
thermocouple amplifiers for pre-DPF/KAT and mid-DPF exhaust gas temperatures.

**Responsibility map.**

| File | Responsibility |
|---|---|
| [`oilPressure.cpp`](src/OilAndSpeed/oilPressure.cpp) | ADC -> bar conversion for the resistive oil sender (10..180 Ω nominal) |
| [`speed.cpp`](src/OilAndSpeed/speed.cpp) | frequency-counter on ABS pulse line -> vehicle speed |
| [`can.cpp`](src/OilAndSpeed/can.cpp) | CAN TX of oil/speed/EGT frames (IDs from `canDefinitions`) |
| [`config.cpp`](src/OilAndSpeed/config.cpp) | persistent config |
| [`periperials.cpp`](src/OilAndSpeed/periperials.cpp) | GPIO/SPI/I²C init *(file name kept as-is in the source tree)* |
| `start.cpp` | init sequence |

**Hardware interfaces** (from [`hardwareConfig.h`](src/OilAndSpeed/hardwareConfig.h)):

- **ADC**: oil pressure sender on `A3`.
- **GPIO**: ABS frequency input on pin 14.
- **SPI** (MISO=0, MOSI=3, SCK=2) -> CAN controller (CS=1, INT=4). Same
  SPI pin layout as Clocks; not shared with any display.
- **I²C** (SDA=12, SCL=13) -> two MCP9600 thermocouple amplifiers at
  `0x60` (pre-DPF) and `0x67` (mid-DPF).
- **GPIO**: RGB status LED on pin 16.

**Direction of data.** OilAndSpeed is primarily a CAN **producer** feeding
both the ECU (for DTC/diagnostic use) and Clocks (for display).

### 5.4 Adjustometer - [`src/Adjustometer`](src/Adjustometer/)

**Role.** A dedicated feedback module for the VP37 injection pump. It is
electrically close to the ECU (commonly co-located on the ECU PCB) but
runs independent firmware because the measurement is timing-critical and
needs dedicated PIO resources.

It measures the pump control coil's resonance frequency with a Hartley
oscillator, subtracts a self-calibrated baseline, and exposes the result
(plus supply voltage, fuel temperature, and a status bitmask) to the ECU
as an **I²C slave** at address `0x57`.

**Responsibility map.**

| File | Responsibility |
|---|---|
| [`sensors.c`](src/Adjustometer/sensors.c) | oscillator capture via PIO, baseline calibration, signal-lost detection, voltage / fuel-temp read, status bit assembly |
| [`led.c`](src/Adjustometer/led.c) | RGB status LED patterns |
| `start.c` | init, I²C slave setup, soft-timer table |

**Hardware interfaces** (from [`hardwareConfig.h`](src/Adjustometer/hardwareConfig.h)):

- **PIO**: `PIO_INTERRUPT_HALL=2` captures the ~37 kHz oscillator.
- **I²C** (SDA=0, SCL=1 @ 400 kHz): **slave** side; the ECU is the master.
- **ADC**: `ADC_VOLT_PIN=29` (supply voltage, 47k/10k divider -> ≈18.8 V
  max), `ADC_FUEL_TEMP_PIN=28` (NTC fuel-temp sensor, `R_VP37_FUEL_A=2300`,
  `R_VP37_FUEL_B=3300`).
- **GPIO**: RGB LED on pin 16.

**I²C register map** (the ECU reads all five bytes in a single burst; see
the `ADJUSTOMETER_REG_*` constants in
[`src/ECU/hardwareConfig.h`](src/ECU/hardwareConfig.h)):

| Register | Type | Meaning |
|---|---|---|
| `0x00..0x01` | int16 BE | frequency deviation from baseline [Hz] |
| `0x02` | uint8 | supply voltage in 0.1 V units |
| `0x03` | uint8 | fuel temperature [°C] |
| `0x04` | uint8 | status bitmask (see below) |

Status bits (`ADJ_STATUS_*`):

- `0x01` - oscillation signal lost,
- `0x02` - fuel-temp sensor broken,
- `0x04` - baseline calibration pending,
- `0x08` - supply voltage out of range.

**Startup timing.** After power-on the oscillator needs to warm up, converge,
and be verified before the baseline is considered valid. The ECU waits up
to `ADJUSTOMETER_BASELINE_WAIT_MS = 8000 ms` for the `BASELINE_PENDING`
status bit to clear before trusting the frequency reading.

### 5.5 Fiesta Serial Configurator - desktop companion

**Role.** Off-vehicle desktop application used to discover Fiesta modules on
USB, inspect identity/metadata, and run read-only protocol queries over the
shared serial session. It replaces ad-hoc `arduino-cli` + manual `picocom`
probing with a single tool that enforces unambiguous target selection.
Writable configuration and full flashing orchestration remain phased follow-up
work behind authenticated protocol layers.

Implementation milestones and phase-closure updates are tracked in
[`CHANGELOG.md`](CHANGELOG.md).

**Target platforms.**
- Linux (Debian-like desktops) - primary target; source build and local
  execution are supported in-tree.
- Windows 10 / 11 - secondary target; architectural target is preserved, but
  current transport implementation is Linux/POSIX-first and still needs
  dedicated portability-layer work for production Windows parity.
- macOS - not a declared target but is essentially free given the Windows
  portability rules.
- Mobile - out of scope.

**GUI toolkit.** GTK-4 is the selected toolkit. The current implementation
uses C + GTK-4.

**Architectural rule.** The tool is split into:
- a **platform-neutral core library** that owns serial enumeration,
  transport framing, session state, authentication, parameter catalog,
  and flash orchestration. It is headless and reusable (CLI, tests, any
  future non-GTK shell);
- a **GTK-4 UI shell** that owns only presentation. It never opens serial
  ports directly and never parses frames.

No business logic may carry platform `#ifdef`s. OS-specific code lives in
five named seams: device enumeration, hot-plug detection, UF2 drive
discovery, config-file location, packaging (see the design doc §4.2).

**Contract with firmware modules.** The configurator depends on two
per-module invariants:
1. Every active firmware module runs a configurator session wired through
  `configSessionInit/Tick/Active/Id` (ECU, Clocks, OilAndSpeed; Adjustometer is out
   of the primary flow). The session answers the bootstrap handshake with
   the module's canonical identity, firmware version, build id, and
   device UID - sourced from compile-time `MODULE_NAME` / `FW_VERSION` /
   `BUILD_ID` plus `hal_get_device_uid_hex()`.
2. USB descriptor identity: `iSerialNumber` is populated by the arduino-pico
   core from `pico_get_unique_board_id()`;
   `iProduct` is customised per module to `Fiesta <ModuleName>` via
   `arduino-cli --build-property build.usb_product=...` in each module's
   `scripts/upload-uf2.sh` and in the shared `bootstrap.sh`.

The UID reported in the handshake and the USB `iSerialNumber` carry the same
64-bit flash unique id, giving the host two independent identification
paths that must agree.

**Rollout phases.** In order: Phase 1 runtime parameters foundation
(`ecu_params` with staging / apply / commit semantics backed by HAL KV),
Phase 2 transitional read-only text protocol (`SC_*`) across ECU/Clocks/
OilAndSpeed (with framed protocol planned as continuation), Phase 3
authenticated writes (HMAC / AEAD + sequence numbers), Phase 4 multi-module
flashing orchestration
(`ENTER_BOOTLOADER`, UF2 copy, post-flash identity re-check), Phase 5
hardening (lockout policy, key rotation, audit logs).

---

## 6. Communication

### 6.1 CAN buses

There are two physically separate CAN buses, both attached to the ECU:

| Bus | Attached to | Purpose | ECU controller |
|---|---|---|---|
| CAN0 "main" | ECU, Clocks, OilAndSpeed | inter-module signalling | SPI, CS=17, INT=15 |
| CAN1 "OBD-2" | ECU, OBD-II diagnostic port | external diagnostics | SPI, CS=6, INT=14 |

Frame IDs and signal layouts live in the shared `canDefinitions` library so
that all participants agree without copy-pasting constants.

Bus roles at a glance:

```
ECU ──┬── publishes: engine state, boost, fuel, DTCs, GPS time
      │
      └── consumes: oil pressure, wheel speed, EGT, dashboard requests

Clocks ──── consumes: RPM, speed, temperatures, pressures
        ──── publishes: (display-side only)

OilAndSpeed ── publishes: oil pressure, wheel speed, EGT pre/mid-DPF
             ── consumes: (minimal - heartbeat / context)

OBD-2 bus ── ECU responds to UDS / OBD-II service requests from
             whatever diagnostic tool is plugged into the port
```

### 6.2 I²C (on the ECU bus)

The ECU is the I²C master on `PIN_SDA=0 / PIN_SCL=1` @ 400 kHz and talks
to two slaves:

| Slave | Address | Role |
|---|---|---|
| Adjustometer (`src/Adjustometer`) | `0x57` | VP37 feedback - 5-byte register block (§5.4) |
| PCF8574 | `0x38` | 8-bit relay expander (glow plugs, fan, heater HI/LO, glow-plug lamp, heated window L/P, VP37 enable) |

OilAndSpeed runs its **own** I²C bus (pins 12/13) for the MCP9600 amplifiers (100 Khz)
- it is not electrically shared with the ECU's bus.

### 6.3 Other interfaces

- **SPI** - shared on the ECU between CAN0, CAN1, and the SD card; uses
  per-device chip-selects. Clocks and OilAndSpeed each run their own SPI
  bus for their own CAN controllers and displays.
- **UART** - ECU-only, NMEA GPS input on RX=22, TX=21.
- **PIO** - used where pin-level timing matters: engine Hall (ECU),
  VP37 injection outputs (ECU), turbo solenoid (ECU), DPF lamp (ECU),
  VP37 oscillator capture (Adjustometer).

### 6.4 Desktop configurator channel (USB CDC)

Every RP2040-based firmware module in the **primary configurator flow**
(ECU, Clocks, OilAndSpeed) exposes a **configurator session** over its
native USB CDC port. This is the transport the Fiesta Serial Configurator
(§5.5) uses off-vehicle. Implementation is shared through JaszczurHAL's
`hal_serial_session_*` helper - each firmware module only owns a thin
wrapper (`configSessionInit/Tick/Active/Id`) and static identity strings.
Adjustometer remains outside the primary serial-configurator/flashing flow.

Channel responsibilities:

- carry the module bootstrap handshake (identity + firmware metadata +
  device UID) on first contact,
- carry transitional read-only `SC_*` queries used today by the desktop
  companion (`SC_GET_META`, `SC_GET_VALUES`, `SC_GET_PARAM_LIST`,
  `SC_GET_PARAM`),
- coexist with the existing debug log output on the same CDC stream,
- later carry authenticated configuration and flashing traffic.

Host-side identification path (two independent layers that must agree):

- USB descriptor layer: `iSerialNumber` carries the RP2040 flash unique id
  (populated automatically by the arduino-pico core), `iProduct` carries
  `Fiesta <ModuleName>` (set at build time via `arduino-cli --build-property
  build.usb_product=...`). On Linux this surfaces as
  `/dev/serial/by-id/usb-<mfr>_<product>_<UID>-if00`; on Windows,
  `usbser.sys` sticky-binds the COM# to that iSerialNumber.
- Application layer: the module reports the same UID inside the handshake
  response, so the host can cross-check that the opened port actually
  belongs to the physical board it expects.

Evolution path (rollout details in §5.5):

- Current bootstrap/read-only level is deliberately unauthenticated and
  constrained to discovery + metadata/values reads before trusted operations
  are introduced.
- Sensitive operations (runtime writes, commit, bootloader entry) live on
  top of an authenticated framed protocol added in later phases; they are
  out of scope for the current text channel.
- CLI mode is a first-class shell over the same core library and is the
  default flashing entrypoint for VS Code tasks.
- CLI and GUI flashing flows must enforce the same fail-closed preflight
  gates (identity handshake, artifact/module compatibility, unambiguous
  target selection) before any bootloader transition or image copy.

Wire-level framed-protocol specifics, auth model details, and full writable
command catalog remain planned follow-up work.

---

## 7. External interfaces to the vehicle

This is what connects the modules to the car itself (as opposed to each
other). See [`Fiesta_pcbs/pinout.txt`](Fiesta_pcbs/pinout.txt) for the
authoritative 104-pin ECU connector map.

### 7.1 Sensors (vehicle -> ECU/OilAndSpeed)

- Coolant temperature (NTC, via HC4051 mux),
- Oil temperature (NTC, via HC4051 mux),
- Intake air temperature (NTC, via HC4051 mux),
- Fuel level (resistive, via HC4051 mux),
- Throttle / driver demand (analog 0–5 V, via HC4051 mux),
- Manifold / boost pressure (analog, via HC4051 mux),
- Engine RPM (Hall sensor -> PIO),
- Heated-windows button (GPIO),
- ECU supply voltage (divider -> ADC 28 on ECU, ADC 29 on Adjustometer),
- Oil pressure (resistive, ADC on OilAndSpeed),
- Wheel speed (ABS pulse, frequency input on OilAndSpeed),
- Pre-DPF + mid-DPF EGT (MCP9600 thermocouple amps on OilAndSpeed).

### 7.2 Actuators (ECU -> vehicle)

- Glow plug relay + indicator lamp (PCF8574 bits 0 / 4),
- Fuel pump (PWM),
- VP37 injection pump (PWM + enable relay via PCF8574 bit 7),
- Turbo boost solenoid / N75 (PWM on PIO pin 10),
- Engine cooling fan relay (PCF8574 bit 1),
- Block heater HI / LO relays (PCF8574 bits 2 / 3),
- Heated windshield relays L / P (PCF8574 bits 5 / 6),
- DPF warning lamp (PIO pin 8),
- MCU Status LED on the ECU board (pin 25).

### 7.3 Driver interface (modules -> driver)

- Speedometer, tachometer, oil gauge - driven by Clocks with PWM square
  waves on pins 9 / 10 / 11 (the analog gauges are Ford OEM / mechanical-style 
  units expecting a frequency input),
- TFT display - driven by Clocks over SPI,
- Buzzer - driven by Clocks on pin 14,
- Heated-windows pushbutton - read by ECU on pin 20.

### 7.4 Diagnostic interface

- OBD-II port connected to the ECU's CAN1 controller. The ECU implements
  OBD-II / UDS service handlers in [`src/ECU/obd-2.c`](src/ECU/obd-2.c); and presents itself as an ECC-V Ford Fiesta 1.8 DI ECU.
  the PID -> internal signal mapping lives in
  [`src/ECU/obd-2_mapping.c`](src/ECU/obd-2_mapping.c).

### 7.5 Auxiliary

- GPS receiver on the ECU UART provides date/time; parsed NMEA is
  republished as a CAN frame.
- Micro-SD card on the ECU SPI bus used for logging (legacy functionality).

---

## 8. Persistence

All persistent state is stored in flash via JaszczurHAL's KV store,
backed by the RP2040 emulated EEPROM (`ECU_EEPROM_SIZE_BYTES` bytes on the ECU).
 The persistent domains are:

- **DTCs** - written by `dtcManager.c`, guarded by a dedicated mutex so
  that core-1 snapshots cannot race core-0 writes,
- **Configuration** - written by each module's `config.{c,cpp}`.

There is no file system on the SD card used by firmware for state - the SD
is for logging only (legacy).

---

## 9. External dependencies

### 9.1 Source-level

| Dependency | Role | Cloned by `bootstrap.sh` into |
|---|---|---|
| `JaszczurHAL` | HAL + utilities + Arduino stubs | `$LIB_DIR/JaszczurHAL` |
| `canDefinitions` | shared CAN frame definitions | `$LIB_DIR/canDefinitions` |
| `rp2040:rp2040` core (earlephilhower/arduino-pico) | RP2040 Arduino core | arduino-cli user dirs |

`$LIB_DIR` defaults to `<parent-of-repo-root>/libraries`, which matches the
path [`src/ECU/CMakeLists.txt`](src/ECU/CMakeLists.txt) expects.

### 9.2 Tooling

`git`, `build-essential`, `cmake`, `python3`, `curl`, `cppcheck` (with the
MISRA addon shipped by Debian's `cppcheck` package), and `arduino-cli`. Full
install procedure in [README § One-shot setup](README.md#one-shot-setup-debian-like-linux--wsl).

---

## 10. Build and CI/CD architecture

Three build paths exist today:

- **Host tests** - per-module `CMakeLists.txt` builds a Unity-based test
  binary compiled as C++ with the HAL mock backend. Fast to run locally;
  no hardware required.
- **Firmware build** - `arduino-cli` compiles each module into a `.uf2`
  file using the `rp2040:rp2040` core. Deployed over USB with the module
  in BOOTSEL mode via `scripts/upload-uf2.sh`. The module-local scripts are
  thin wrappers over shared implementations in `src/common/scripts/`. The
  upload/build paths also pass `build.usb_manufacturer` /
  `build.usb_product` per module so that each module surfaces under a
  distinct USB iProduct string on the host.
- **Desktop configurator build/test** - `src/SerialConfigurator` is built
  with CMake/GTK4 and tested with CTest via
  [`scripts/desktop-build.sh`](src/SerialConfigurator/scripts/desktop-build.sh)
  (`build`, `run`, `test`, `clean`). CI is in
  [`.github/workflows/serial-configurator-tests.yml`](.github/workflows/serial-configurator-tests.yml).
  Repository scope currently covers source build/test; release packaging paths
  are not yet standardized in-tree.

The ECU firmware build additionally enforces `-Werror` on the Arduino path
as a warning quality gate.

### 10.1 GitHub Actions workflows ([`.github/workflows/`](.github/workflows/))

| Workflow | Trigger | What it does |
|---|---|---|
| [`ecu-tests.yml`](.github/workflows/ecu-tests.yml) | push/PR on `src/ECU/**` | clones deps, configures CMake, runs ctest for ECU |
| [`ecu-cppcheck.yml`](.github/workflows/ecu-cppcheck.yml) | push/PR | runs cppcheck against the baseline in [`src/ECU/cppcheck-baseline.log`](src/ECU/cppcheck-baseline.log), fails if new findings appear |
| [`ecu-misra.yml`](.github/workflows/ecu-misra.yml) | manual | runs [`src/ECU/misra/check_misra.sh`](src/ECU/misra/) and uploads a MISRA findings artifact |
| [`clocks-tests.yml`](.github/workflows/clocks-tests.yml) | push/PR on `src/Clocks/**` | ctest for Clocks |
| [`oilandspeed-tests.yml`](.github/workflows/oilandspeed-tests.yml) | push/PR on `src/OilAndSpeed/**` | ctest for OilAndSpeed |
| [`adjustometer-tests.yml`](.github/workflows/adjustometer-tests.yml) | push/PR on `src/Adjustometer/**` | ctest for Adjustometer |
| [`serial-configurator-tests.yml`](.github/workflows/serial-configurator-tests.yml) | push/PR on `src/SerialConfigurator/**` | configures and builds the GTK4 desktop app, then runs CTest (`serial-configurator-core-tests`, `serial-configurator-core-api-tests`, `serial-configurator-core-protocol-tests`) |

### 10.2 Unattended daily build

[`src/ECU/scripts/systemd/`](src/ECU/scripts/systemd/) ships a user-scope
systemd service + timer that runs `bootstrap.sh` daily on a Raspberry Pi
and emails a PASS/FAIL status summary. This is the slow-cycle integration
signal - it exercises the whole tree, including firmware compilation for
all four modules, once per day. Setup notes in
[`src/ECU/scripts/systemd/README.md`](src/ECU/scripts/systemd/README.md).

### 10.3 Bootstrap entry point

[`src/ECU/scripts/bootstrap.sh`](src/ECU/scripts/bootstrap.sh) is the
single idempotent project entry point that sets up a fresh machine end-to-end:
system packages -> arduino-cli + rp2040 core -> cloning/refreshing the two
external library repos -> host tests for every module that has a
`CMakeLists.txt` -> firmware `.uf2` build for every module. Env overrides:
`LIB_DIR`, `ARDUINO_CLI`, `ALLOW_ROOT`, `SKIP_APT`, `SKIP_TESTS`,
`SKIP_BUILD`.

---

## 11. Directory structure

```
Fiesta/
├── README.md                    # project overview
├── MISRA.md                     # MISRA-C status, policy, entry points (authoritative)
├── CHANGELOG.md                 # per-module build/test/CI status log
├── ARCHITECTURE.md              # this file
├── LICENSE
├── .github/
│   └── workflows/               # CI jobs (see §10.1)
├── src/
│   ├── ECU/                     # safety-critical engine control (MISRA scope)
│   ├── Clocks/                  # dashboard / instrument cluster
│   ├── OilAndSpeed/             # oil + ABS speed + EGT telemetry
│   ├── Adjustometer/            # VP37 feedback (I²C slave)
│   ├── SerialConfigurator/      # GTK4 desktop companion (detect/configure/flash groundwork)
│   └── Fiesta_clock/            # AVR-based standalone clock (archived)
├── Fiesta_pcbs/                 # schematics, PCB layouts, connector maps
│   ├── ecu/                     # ecuv1 + ecuv2
│   ├── dashboard/ clock/ oil_and_speed/
│   ├── vp37_adjustometer/ lamp_dimmer/ air_conditioning/
│   ├── rpipico/
│   ├── pinout.txt               # 104-pin ECU connector mapping
│   └── wirings.txt              # color-coded loom notes
├── materials/                   # reference docs, datasheets, examples, photos
└── legacy/                      # archived modules (DPF_main, AdaptiveLights, Fading)
```

---

## 12. What is *not* covered here

- Individual CAN frame IDs and signal layouts - see `canDefinitions`.
- JaszczurHAL internals - see the HAL repository.
- MISRA-C rule-by-rule status - see the MISRA screening artifact from
  `.github/workflows/ecu-misra.yml` and the deviation register under
  `src/ECU/misra/`.
- Complete ECU pinout with wire colors - see
  [`Fiesta_pcbs/pinout.txt`](Fiesta_pcbs/pinout.txt) and
  [`Fiesta_pcbs/wirings.txt`](Fiesta_pcbs/wirings.txt).
- Module-level safety status / progress - see the README.
- Internal Serial Configurator execution notes are intentionally not linked
  from repository-level documentation.

When in doubt about architecture, read the code; the `hardwareConfig.h`,
`start.{c,cpp}`, and `can.{c,cpp}` files in each module are the best
entry points.
