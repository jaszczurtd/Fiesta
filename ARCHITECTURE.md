# Fiesta system architecture

[Polski](ARCHITECTURE.pl.md) · [Project overview](README.md)

Fiesta replaces and extends parts of the electronics in a Ford Fiesta 1.8
(M)TDDI. Four firmware modules, each on its own PCB, share the car's work: the
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
   ┌───────────┐ ◄──────────► Clocks          gauges, TFT, oil, speed, EGT, GPS
   │    ECU    │ ◄──────────  Fiesta_clock    RTC time in UTC, air temperatures
   │  RP2350,  │
   │ two cores │ ◄── I²C ───  Adjustometer    VP37 pump position (0x57)
   │           │ ─── I²C ───► PCF8574         relays (0x38)
   │           │ ─── CAN1 ──► OBD-II port
   └─────┬─────┘ ◄─ ADC, GPIO, PWM ─► sensors and actuators
         │
         │ USB CDC, off-vehicle
         ▼
   Fiesta Serial Configurator (Linux, GTK-4)
     also connects to Clocks and Fiesta_clock
```

The ECU is the hub. It is the only module that controls the engine. The
MISRA-C requirements cover it, the shared VP37 module and the Adjustometer;
the other modules either feed the ECU data or display what it publishes.

See [project status](doc/STATUS.md) for board/firmware compatibility and remaining integration work.

## Hardware platform

ECU runs on a Waveshare RP2350-Plus. Its PCB also takes the RP2040-Plus,
which has the same header layout; `device_tree.json` keeps both as assemblies
with identical wiring, and `rp2350-plus` is the default. The other modules run
on RP2040 boards: Raspberry Pi Pico or RP2040-Zero. Each module describes its
board, devices and pins in its own `device_tree.json`; the build generates
`jh_hardware.h` from it and the code reads its `JH_HW_*` macros directly.
RP2040 and RP2350 were chosen because they are cheap and easy to get, have two
cores, flexible GPIO interrupts and PWM, and flash that can emulate EEPROM for
DTCs and settings. RP2350 gives the ECU faster cores with a floating-point
unit and more SRAM. The RP2040 ADC correction table applies only to RP2040
builds; RP2350 ADC codes are used as read. The firmware talks to the hardware
through JaszczurHAL. A move to another MCU also needs a matching HAL backend,
assembly description, timing and memory checks, and hardware validation.

MCP2515 initialization uses each node's generated configuration, including
SPI bus, chip select, bitrate and oscillator frequency. Fiesta_clock also
uses the input `pull` settings; GPIO polarity comes from `activeLow`.

Schematics, layouts, and connector maps for every board are in
[`Fiesta_pcbs/`](Fiesta_pcbs/). [`pinout.txt`](Fiesta_pcbs/pinout.txt) maps
the 104-pin ECU connector and [`wirings.txt`](Fiesta_pcbs/wirings.txt) lists
the loom colours.

## Modules

| Module | Language | Role | MISRA |
|---|---|---|---|
| [`ECU`](src/ECU/README.md) | C | engine control, diagnostics, actuators | required; `src/common/vp37` gated at zero findings, the rest a screening snapshot |
| [`Clocks`](src/Clocks/README.md) | C++ | instrument cluster; oil pressure, wheel speed, exhaust gas temperature, GPS | no |
| [`Fiesta_clock`](src/Fiesta_clock/README.md) (`RTC_Clock`) | C | real-time clock set from GPS UTC; time and air temperatures on CAN | no |
| [`Adjustometer`](src/Adjustometer/README.md) | C | VP37 pump position feedback, I²C slave | required; gated at zero findings |
| [`VP37TestBench`](src/VP37TestBench/README.md) | C, C++ | VP37 pump test stand: the shared drive module on bench hardware | the shared `src/common/vp37` code is gated; the bench's own files are not scanned |
| [`SerialConfigurator`](src/SerialConfigurator/README.md) | C, GTK-4 | desktop configuration and flashing | no |

The list itself lives in [`modules.json`](modules.json): each module's kind, token,
SerialConfigurator slot and host-test build. The slot also says whether the module gets a
Tests sub-tab (`tests`) and whether its test status carries an actuator position
(`testsDrive`). Adding a module starts there.
[`scripts/fiesta_modules.py`](scripts/fiesta_modules.py) checks the list and generates the
token header in `src/common/scDefinitions`, the SerialConfigurator module table and a shell
fragment; the VS Code generator, `runalltests.sh`, `runmefirst.sh`, the bootstrap and
SerialConfigurator read those instead of keeping their own lists. The SerialConfigurator slot
index is explicit because the saved flash paths are keyed by it. CI workflow lists stay
literal, and a test checks them against the registry.

[`legacy/`](legacy/) keeps the retired `DPF_main`, `AdaptiveLights`, and
`Fading` sources for reference. They are not built or tested.

### ECU

Core 0 handles communication, diagnostics, storage and auxiliary outputs. Core 1 runs VP37
control, RPM and turbo. There is no RTOS. The [ECU guide](src/ECU/README.md) leads to setup,
control behaviour and synchronization details.

### Clocks

Drives the factory gauges, lamps, TFT and buzzer. It measures oil pressure, ABS speed and
EGT, receives GPS, and obtains engine data from the ECU over CAN. The [Clocks
guide](src/Clocks/README.md) covers readings, data loss, output tests and execution.

### Adjustometer

Measures actuator position through oscillator frequency and publishes it over I²C at `0x57`.
ECU requires fresh, coherent feedback and waits up to 8 s for the baseline before calibrating
travel (`VP37_ADJUSTOMETER_BASELINE_WAIT_MS`). The [sensor guide](src/Adjustometer/README.md)
covers startup, LED and registers. It does not support the configurator protocol.

### VP37TestBench

A separate bench board uses the shared VP37 controller with its own hardware adapter and
display. Its [guide](src/VP37TestBench/README.md) explains the distinction from ECU BENCH.

### Fiesta_clock

Keeps RTC time, synchronizes it with GPS time from Clocks, and publishes UTC and temperatures
over CAN. Display and time editing use CET/CEST. The [clock
guide](src/Fiesta_clock/README.md) covers RTC variants, readings and recovery.

### Serial Configurator

The configurator is a desktop application for Linux, written in C with GTK-4.
It finds Fiesta modules on USB, shows their identity, reads and writes their
parameters, runs the functional tests of an ECU test build and of Clocks,
and flashes firmware after checking the build manifest. The CLI provides discovery,
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
| CAN0 "main" | ECU, Clocks, Fiesta_clock | traffic between modules |
| CAN1 "OBD-2" | ECU, OBD-II port | external diagnostic tools |

On CAN0 the ECU publishes engine state, RPM, throttle, boost, fuel level, and
fuel temperature. Frame `0x12B` marks the fuel temperature with a valid bit and
carries 0 without it. It
reads oil pressure, wheel speed, and EGT from Clocks, the DPF state from frame
`0x124`, and the RTC time of Fiesta_clock (`0x130`), which dates new DTCs. DTCs never go out on CAN0;
diagnostic tools read them over CAN1. Clocks sends its display brightness
(`0x126`), oil pressure with ABS speed (`0x12A`), and both exhaust
temperatures (`0x12F`), GPS position and time (`0x12D`, `0x12E`), and GPS
UTC (`0x131`). Fiesta_clock transmits RTC time in
UTC (`0x130`) and the air temperatures (`0x132`), and reads only the GPS UTC
of Clocks. On CAN1 the ECU answers
OBD-II and UDS requests from whatever tool is connected. The exhaust and DPF
temperature PIDs and the oil pressure DID answer with a negative response
(`0x22`, conditions not correct) while Clocks marks that value invalid or its
frames stop. The fuel temperature DID `DD02` answers the same way while the
Adjustometer reports a fuel sensor fault or stops answering. A sensor fault that
every good Adjustometer frame reports for `ECU_FUEL_TEMP_FAULT_CONFIRM_MS`
(`src/ECU/config.h`) raises U190A; the first frame without it clears the active
code. A bus-off controller or a refused
transmit raises U0073; its failure type byte (`DTC_DETAIL_*`) names the frame
that failed, and the UDS DTC record carries it as the third DTC byte.

### I²C

The ECU is the master on its bus, with Adjustometer at `0x57` and the PCF8574
relay expander at `0x38`. Clocks and Fiesta_clock run separate I²C buses
for their own sensors.

### USB

ECU, Clocks, and Fiesta_clock each show up as a separate USB
device named `Fiesta <Module>` with the board's unique ID as serial number.
The configurator uses these names to find the right module even when several
boards are connected. The protocol on that link, the identity rules, and the
flashing sequence are in
[`PROTOCOL.md`](src/common/scDefinitions/PROTOCOL.md).

## Vehicle interfaces

| Direction | Signals |
|---|---|
| Sensors to ECU | coolant, oil, and intake air temperature; fuel level; throttle; manifold pressure; engine RPM (Hall); heated-window button; supply voltage |
| Sensors to Clocks | oil pressure, ABS wheel speed, EGT before and inside the DPF, GPS |
| ECU to vehicle | VP37 pump (PWM and enable relay), N75 boost solenoid, glow plugs and their lamp, fan, block heater high/low, heated windshield left/right, DPF lamp |
| Clocks to driver | speedometer, tachometer, and oil gauge (frequency inputs), oil and charge lamps, TFT display, buzzer |
| Diagnostics | OBD-II port on CAN1 |
| Auxiliary | GPS position and UTC in Clocks; RTC time in UTC and air temperatures from Fiesta_clock |

## Persistence

Modules that store settings keep them in the JaszczurHAL key-value store on
flash-backed EEPROM (`ECU_EEPROM_SIZE_BYTES` on the ECU). The ECU also stores
its DTCs there; a mutex keeps core-1 reads from racing core-0 writes.
Fiesta_clock stores only the marker that its RTC holds UTC.

## Builds and CI

[Host tests and CI](doc/TESTING.md) describe the module selection and quality
checks. Fiesta_clock has firmware builds and host tests of its display and
time handling; VP37TestBench has local builds and host tests but opts out of
firmware CI. SerialConfigurator has a separate desktop test project.

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
│   ├── ECU/ Clocks/ Fiesta_clock/ Adjustometer/ VP37TestBench/
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
