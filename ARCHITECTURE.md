# Fiesta system architecture

[Polski](ARCHITECTURE.pl.md) · [Project overview](README.md)

Fiesta replaces and extends parts of the electronics in a Ford Fiesta 1.8
(M)TDDI. Five firmware modules, each on its own PCB, share the car's work: the
ECU runs the engine, the others drive the dashboard, measure what the ECU does
not, keep time, and read the injection pump position. A desktop application,
the Fiesta Serial Configurator, connects to the modules over USB to read and
change their settings and to flash new firmware.

A separate VP37TestBench firmware runs the pump on a test stand. This document
explains how the modules fit together. See [setup and builds](doc/BUILDING.md)
for development commands and the module guides for control details.

## System overview

```text
                 CAN0 (main)
   ┌───────────┐ ◄──────────► Clocks          dashboard gauges, TFT, buzzer
   │           │ ◄──────────  OilAndSpeed     oil pressure, wheel speed, EGT
   │    ECU    │ ◄──────────  Fiesta_clock    RTC time, 1 Hz
   │  RP2040,  │
   │ two cores │ ◄── I²C ───  Adjustometer    VP37 pump position (0x57)
   │           │ ─── I²C ───► PCF8574         relays (0x38)
   │           │ ◄─ PIO UART  GPS receiver
   │           │ ─── CAN1 ──► OBD-II port
   └─────┬─────┘ ◄─ ADC, GPIO, PWM ─► sensors and actuators
         │
         │ USB CDC, off-vehicle
         ▼
   Fiesta Serial Configurator (Linux, GTK-4)
     also connects to Clocks, OilAndSpeed and Fiesta_clock
```

The ECU is the hub. It is the only module that controls the engine. The
MISRA-C requirements cover it, the shared VP37 module and the Adjustometer;
the other modules either feed the ECU data or display what it publishes.

## Hardware platform

All modules run on RP2040 boards: Raspberry Pi Pico, RP2040-Plus, or
RP2040-Zero. Each module describes its board, devices and pins in its own
`device_tree.json`; the build generates `jh_hardware.h` from it and the code
reads its `JH_HW_*` macros directly.
RP2040 was chosen because it is cheap and
easy to get, has two cores, flexible GPIO interrupts and PWM, and flash that
can emulate EEPROM for DTCs and settings. The firmware talks to the hardware
through JaszczurHAL. A move to another MCU also needs a matching HAL backend,
assembly description, timing and memory checks, and hardware validation.

Schematics, layouts, and connector maps for every board are in
[`Fiesta_pcbs/`](Fiesta_pcbs/). [`pinout.txt`](Fiesta_pcbs/pinout.txt) maps
the 104-pin ECU connector and [`wirings.txt`](Fiesta_pcbs/wirings.txt) lists
the loom colours.

## Modules

| Module | Language | Role | MISRA |
|---|---|---|---|
| [`ECU`](src/ECU/) | C | engine control, diagnostics, actuators | required; `src/common/vp37` gated at zero findings, the rest a screening snapshot |
| [`Clocks`](src/Clocks/) | C++ | instrument cluster | no |
| [`OilAndSpeed`](src/OilAndSpeed/) | C++ | oil pressure, wheel speed, exhaust gas temperature | no |
| [`Fiesta_clock`](src/Fiesta_clock/) (`RTC_Clock`) | C | real-time clock, time broadcast on CAN | no |
| [`Adjustometer`](src/Adjustometer/) | C | VP37 pump position feedback, I²C slave | required; gated at zero findings |
| [`VP37TestBench`](src/VP37TestBench/) | C, C++ | VP37 pump test stand: the shared drive module on bench hardware | the shared `src/common/vp37` code is gated; the bench's own files are not scanned |
| [`SerialConfigurator`](src/SerialConfigurator/) | C, GTK-4 | desktop configuration and flashing | no |

The list itself lives in [`modules.json`](modules.json): each module's
kind, token, SerialConfigurator slot and host-test build. Adding a module
starts there. [`scripts/fiesta_modules.py`](scripts/fiesta_modules.py) checks
the list and generates the token header in `src/common/scDefinitions`, the
SerialConfigurator module table and a shell fragment; the VS Code generator,
`runalltests.sh`, `runmefirst.sh`, the bootstrap and SerialConfigurator read
those instead of keeping their own lists. The SerialConfigurator slot index is
explicit because the saved flash paths are keyed by it. CI workflow lists stay
literal, and a test checks them against the registry.

[`legacy/`](legacy/) keeps the retired `DPF_main`, `AdaptiveLights`, and
`Fading` sources for reference. They are not built or tested.

### ECU

ECU controls the VP37 injection pump, N75 boost valve, glow plugs, fan,
block heater and heated windshield. It stores DTCs and answers OBD-II requests
as a Ford Fiesta 1.8 DI EEC-V ECU.

Core 0 handles timers, CAN, diagnostics, relays, storage and USB sessions;
core 1 runs the VP37 control cycle, RPM interrupt and turbo control. There is
no RTOS. The control core publishes atomic snapshots for core 0, while I²C
output-latch access and DTC storage use mutexes. Flash writes stay on core 0.

The [ECU guide](src/ECU/README.md) describes synchronization, position demand,
current feedback, compensation, telemetry, RAM use and the BENCH image. The
[Adjustometer guide](src/Adjustometer/README.md) describes position measurement.

ECU shares a 400 kHz I²C bus between Adjustometer (`0x57`) and the PCF8574A
relay expander (`0x38`). PCF8574A is rated for 100 kHz: this wiring operates
outside its specification until the expander is replaced or moved to a
separate bus. The local binding limits the configured bus to 400 kHz.

### Clocks

Clocks drives the instrument cluster. It listens on the main CAN bus and
produces square waves for the factory speedometer, tachometer, and oil gauge,
draws extra readouts on a TFT display, and sounds the buzzer. Apart from its
display brightness, sent every 50 ms, it only consumes data; the ECU needs
nothing from it.

| File | Responsibility |
|---|---|
| [`Cluster.cpp`](src/Clocks/Cluster.cpp) | square waves for the factory gauges |
| [`Gauge.h`](src/Clocks/Gauge.h), [`simpleGauge.cpp`](src/Clocks/simpleGauge.cpp), [`tempGauge.cpp`](src/Clocks/tempGauge.cpp), [`pressureGauge.cpp`](src/Clocks/pressureGauge.cpp) | gauge scaling |
| [`TFTExtension.cpp`](src/Clocks/TFTExtension.cpp) | TFT drawing |
| [`logic.cpp`](src/Clocks/logic.cpp) | turns CAN signals into gauge, display, and buzzer state |
| [`buzzer.cpp`](src/Clocks/buzzer.cpp), [`buzzerStrategy.cpp`](src/Clocks/buzzerStrategy.cpp) | tones and warning patterns |
| [`can.cpp`](src/Clocks/can.cpp) | CAN reception and the brightness frame; the MCP2515 filters admit only Fiesta IDs |

The CAN controller and the TFT share one SPI bus.

### OilAndSpeed

OilAndSpeed measures what the ECU does not read itself: oil pressure from a
resistive sender, vehicle speed from the ABS pulse line, and exhaust gas
temperature before and inside the DPF through two MCP9600 amplifiers on its
own I²C bus. It sends the results on CAN for the ECU and Clocks: oil pressure
and speed every 50 ms, exhaust temperatures every 250 ms and after each
thermocouple reading. The same frames tell the receivers the module is present.

| File | Responsibility |
|---|---|
| [`oilPressure.cpp`](src/OilAndSpeed/oilPressure.cpp) | ADC reading to bar |
| [`speed.cpp`](src/OilAndSpeed/speed.cpp) | ABS pulse frequency to speed |
| [`can.cpp`](src/OilAndSpeed/can.cpp) | oil, speed, and EGT frames |
| [`config.cpp`](src/OilAndSpeed/config.cpp) | configurator session, read-only sampling intervals |

### Adjustometer

Adjustometer measures the VP37 actuator position through the frequency of a
Hartley oscillator connected to the pump sensor coils, roughly 22-37 kHz.
JaszczurHAL captures blocks of 32 periods through RP2040 PIO and DMA.
The measurement combines four blocks into a 128-period window that slides by
32 periods and averages the raw windows over one VP37 drive period, so the PWM
ripple cancels without a filter lag. The exported deviation from a calibrated
baseline is measured in Hz; ECU calibration maps it to actuator position.

Core 0 drains capture data and publishes feedback. Core 1 handles auxiliary
ADC readings, diagnostics, LED and USB logging. Adjustometer has its own
RP2040 and usually sits on the ECU board.

The I²C slave at `0x57` exposes three blocks:

| Registers | Meaning |
|---|---|
| `0x00..0x04` | Legacy pulse, supply voltage, fuel temperature and status. |
| `0x05..0x16` | Frequency, baseline, signed deviation and chip-temperature diagnostics. |
| `0x17..0x34` | Versioned 30-byte control feedback with sample number, timestamp and age. |

The active VP37 controller requires the control-feedback block. ECU checks
frame coherence and freshness; old firmware exposing only the legacy block
cannot provide this feedback. HAL freezes the register map for each I²C read.
Status bits report signal loss (`0x01`), a broken fuel sensor (`0x02`), pending
baseline (`0x04`) and supply outside its valid range (`0x08`).

Startup includes 500 ms of warm-up, baseline convergence (80 ms minimum,
250 ms force-lock), and 1000 ms of drift verification. ECU waits up to
`VP37_ADJUSTOMETER_BASELINE_WAIT_MS` (8 s) before calibrating travel. Measurement,
startup and reset details are in the
[Adjustometer README](src/Adjustometer/README.md).
Adjustometer does not take part in the configurator protocol.

### VP37TestBench

VP37TestBench runs the shared VP37 drive module (`src/common/vp37`) on the
pump test stand: its own RP2040 with an ST7796S display, a power stage of the
same type as the ECU one (parts and values from
[`fiesta-vp37-power-stage.json`](src/common/vp37/fiesta-vp37-power-stage.json),
pins from its own `device_tree.json`, read through
[`vp37_power_stage.h`](src/common/vp37/vp37_power_stage.h)) and the
Adjustometer on the same I²C protocol. The bench adapter
([`vp37_bench_adapter.c`](src/VP37TestBench/vp37_bench_adapter.c)) provides
the board services: the two PWM channels written active low like on the ECU,
the drive enable on a plain GPIO instead of the ECU's PCF8574, and the
Adjustometer transfer without a bus mutex, because the bench I²C has one
user. Like the ECU (through
[`ecu_hal_config.h`](src/ECU/ecu_hal_config.h)), it includes
[`vp37_hal_config.h`](src/common/vp37/vp37_hal_config.h) from its
`hal_project_config.h`: each I²C phase is bounded to 2 ms, so a hung bus
cannot hold a control step past the 20 ms feedback cutoff, and the module
stops the build when the bound is missing. The demand comes from a potentiometer on the auxiliary scan input.
Core 0 runs the timers, the display and debug output; core 1 starts the pump
and runs `VP37_process()`. The screen redraws a value only when it changes,
and a corner dot shows the Adjustometer connection the way Clocks shows its
CAN devices: steady green while frames flow, blinking red while they do not
(with the pump stopped, core 1 probes the status register once a second). Without a pump attached the start ends after the
baseline timeout and the bench keeps running, which is also its bring-up
smoke test. VP37TestBench does not take part in the configurator protocol.

### Fiesta_clock

Fiesta_clock keeps calendar time in a PCF8563 and sends it once a second as
`CAN_ID_RTC_UPDATE` (`0x130`). It stays silent while the RTC reports lost
integrity. The configurator can read and set the date and time; a commit is
rejected unless the whole date is valid, leap years included. The module also
shows the time, temperatures from two DS18B20 sensors, and the supply voltage
on a 128×32 SSD1306 OLED driven by JaszczurHAL. The OLED and RTC share the
I²C bus described in `device_tree.json`. HAL writes text directly to panel
RAM, without a framebuffer. The clock retains its original 6×8 font
(including Polish ś), scales 1 and 2, and panel settings, including contrast
0x3f. Idle mode suspends the panel while retaining controller RAM. An ignition
change reapplies the panel
configuration. The greeting is cleared on the panel after the initial
temperature readings.

### Serial Configurator

The configurator is a desktop application for Linux, written in C with GTK-4.
It finds Fiesta modules on USB, shows their identity, reads and writes their
parameters, runs the functional tests of an ECU test build, and flashes
firmware after checking the build manifest. The CLI provides discovery,
settings, GPS, functional tests and authenticated bootloader entry; the
complete UF2 copy and reconnection flow is provided by the GUI.

It has two layers. The core library handles serial ports, the protocol,
authentication, parameters, and flashing, and has no GUI code. The GTK shell
only presents; it never opens a port or parses a frame. Platform-specific
code is limited to device enumeration, hot-plug, finding the UF2 drive,
config file location, and packaging. Linux is the working platform. Windows
10/11 is the intended second one, but the serial transport is still POSIX
only.

Commands, authentication, and signature status are described in the
[Serial Configurator README](src/SerialConfigurator/README.md).

## Shared code

- **[JaszczurHAL](https://github.com/jaszczurtd/JaszczurHAL)** is a separate
  repository pinned as a submodule in `src/JaszczurHAL`. Fiesta records the
  exact HAL commit; `runmefirst.sh` initializes that revision. It
  provides the hardware layer (GPIO, ADC, PWM, I²C, SPI, CAN, timers), soft
  timers, PID, a key-value store on emulated EEPROM, logging, and a mock
  backend that lets the module code build and run in host tests. Each module
  implements its `app_start()` and `app_task0()` entry points, plus
  `app_task1()` when it uses the second core.
- **[`canDefinitions`](src/common/canDefinitions/canDefinitions.h)** holds
  every CAN ID, frame layout, and scaling used between modules.
- **[`scDefinitions`](src/common/scDefinitions/)** holds the configurator
  protocol shared by the firmware modules and the desktop application. The
  protocol is described in
  [`PROTOCOL.md`](src/common/scDefinitions/PROTOCOL.md). Firmware modules run
  their configurator session through `sc_config_session_t`
  ([`sc_command_handlers.h`](src/common/scDefinitions/sc_command_handlers.h)):
  it starts the serial session, the SC command service and the adapter
  between them, so a module's `config` only lists its parameters, values and
  callbacks.

Most modules use the same layout. `Fiesta_clock` keeps older file names
(`main.c`, `RTC.c`) but the same shared pieces. `VP37TestBench` enters
through the HAL app-entry hooks in `logic.cpp` instead of `start.c` and has
no CAN or configurator session.

```text
src/<Module>/
├── start.{c,cpp}/.h       # entry points, soft-timer table, watchdog
├── device_tree.json       # board, devices, pins and addresses
├── hardwareConfig.h       # electrical constants and wiring checks
├── hal_project_config.h   # enabled HAL features
├── config.{c,cpp}/.h      # settings and the configurator session
├── can.{c,cpp}/.h         # CAN frames
├── <domain logic files>
├── CMakeLists.txt         # host tests
├── tests/                 # host unit tests, compiled as C++
└── .vscode/               # jh-vscode manifest and tasks
```

## Communication

### CAN

The ECU sits on two separate CAN buses:

| Bus | Members | Purpose |
|---|---|---|
| CAN0 "main" | ECU, Clocks, OilAndSpeed, Fiesta_clock | traffic between modules |
| CAN1 "OBD-2" | ECU, OBD-II port | external diagnostic tools |

On CAN0 the ECU publishes engine state, RPM, throttle, boost, fuel, and GPS
position and time. It reads oil pressure, wheel speed, and EGT from
OilAndSpeed and the DPF state from frame `0x124`. DTCs never go out on CAN0;
diagnostic tools read them over CAN1. Clocks mostly listens: its only frame
carries the display brightness (`0x126`), and OilAndSpeed takes it as a sign
that the cluster is present. OilAndSpeed transmits and reads a few ECU values.
Fiesta_clock only transmits its RTC time (`0x130`). On CAN1 the ECU answers
OBD-II and UDS requests from whatever tool is connected. A bus-off controller or a refused
transmit raises U0073; its failure type byte (`DTC_DETAIL_*`) names the frame
that failed, and the UDS DTC record carries it as the third DTC byte.

### I²C

The ECU is the master on its bus, with Adjustometer at `0x57` and the PCF8574
relay expander at `0x38`. OilAndSpeed and Fiesta_clock run separate I²C buses
for their own sensors.

### USB

ECU, Clocks, OilAndSpeed, and Fiesta_clock each show up as a separate USB
device named `Fiesta <Module>` with the board's unique ID as serial number.
The configurator uses these names to find the right module even when several
boards are connected. The protocol on that link, the identity rules, and the
flashing sequence are in
[`PROTOCOL.md`](src/common/scDefinitions/PROTOCOL.md).

## Vehicle interfaces

| Direction | Signals |
|---|---|
| Sensors to ECU | coolant, oil, and intake air temperature; fuel level; throttle; manifold pressure; engine RPM (Hall); heated-window button; supply voltage |
| Sensors to OilAndSpeed | oil pressure, ABS wheel speed, EGT before and inside the DPF |
| ECU to vehicle | VP37 pump (PWM and enable relay), N75 boost solenoid, glow plugs and their lamp, fan, block heater high/low, heated windshield left/right, DPF lamp |
| Clocks to driver | speedometer, tachometer, and oil gauge (frequency inputs), TFT display, buzzer |
| Diagnostics | OBD-II port on CAN1 |
| Auxiliary | GPS date and time on the ECU software serial line, republished on CAN; RTC time from Fiesta_clock |

## Persistence

Modules that store settings keep them in the JaszczurHAL key-value store on
flash-backed EEPROM (`ECU_EEPROM_SIZE_BYTES` on the ECU). The ECU also stores
its DTCs there; a mutex keeps core-1 reads from racing core-0 writes.

## Builds and CI

[Host tests and CI](doc/TESTING.md) describe the module selection and quality
checks. Fiesta_clock has firmware builds and a host test of its display;
VP37TestBench has
local builds and host tests but opts out of firmware CI. SerialConfigurator
has a separate desktop test project.

[The build guide](doc/BUILDING.md) explains assembly and variant paths,
manifest preparation, upload and SWD debugging. Plain `jh-vscode build`
produces firmware; the Fiesta bootstrap adds and verifies its manifest.
The [systemd runner](src/ECU/scripts/systemd/README.md) can run setup, tests
and builds daily and email the result.

## Repository layout

```text
Fiesta/
├── README.md                    # project overview
├── ARCHITECTURE.md              # this file
├── MISRA.md                     # MISRA-C status and policy
├── doc/                         # builds, tests, gallery
├── modules.json                 # module registry
├── scripts/                     # registry, VS Code and duplicate-gate tools, git hooks
├── .github/workflows/           # CI
├── src/
│   ├── ECU/ Clocks/ OilAndSpeed/ Fiesta_clock/ Adjustometer/ VP37TestBench/
│   ├── JaszczurHAL/             # pinned HAL submodule
│   ├── SerialConfigurator/      # desktop application and CLI
│   └── common/
│       ├── canDefinitions/      # CAN IDs and frame layouts
│       ├── cmake/               # CMake helpers shared by module builds
│       ├── scDefinitions/       # configurator protocol
│       ├── scripts/             # manifest, UF2, and module-name helpers
│       ├── tests/               # host-test helpers shared by modules
│       └── vp37/                # the VP37 drive module and its tests
├── Fiesta_pcbs/                 # schematics, layouts, connector maps
├── materials/                   # datasheets, reference documents, photos
└── legacy/                      # retired modules
```

## Where to look next

- Frame IDs and layouts:
  [`canDefinitions.h`](src/common/canDefinitions/canDefinitions.h).
- Pins and addresses: each module's `device_tree.json`.
- Start-up order and timing: each module's `start.{c,cpp}`.
- Connector pinout and wire colours:
  [`Fiesta_pcbs/pinout.txt`](Fiesta_pcbs/pinout.txt),
  [`Fiesta_pcbs/wirings.txt`](Fiesta_pcbs/wirings.txt).
- MISRA rule status: the `ecu-misra.yml` report and
  [`src/ECU/misra/`](src/ECU/misra/).
- JaszczurHAL internals: the JaszczurHAL repository.
