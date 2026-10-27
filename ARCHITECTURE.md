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
   │    ECU    │ ◄──────────  Fiesta_clock    RTC time in UTC, 1 Hz
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
| [`ECU`](src/ECU/) | C | engine control, diagnostics, actuators | required; `src/common/vp37` gated at zero findings, the rest a screening snapshot |
| [`Clocks`](src/Clocks/) | C++ | instrument cluster; oil pressure, wheel speed, exhaust gas temperature, GPS | no |
| [`Fiesta_clock`](src/Fiesta_clock/) (`RTC_Clock`) | C | real-time clock set from GPS UTC, time broadcast on CAN | no |
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

Clocks drives the instrument cluster and measures what the ECU does not read
itself. It produces square waves for the factory speedometer, tachometer, and
oil gauge, switches the oil and charge lamps, draws extra readouts on a TFT
display, and sounds the buzzer. It reads oil pressure from a resistive sender,
vehicle speed from the ABS pulse line, and exhaust gas temperature before and
inside the DPF through two MCP9600 amplifiers on its own I²C bus.

The two RP2040 cores split the work. Core 0 receives and sends CAN, measures
the ABS speed, and drives the gauges, lamps, and buzzer. Core 1 draws the TFT,
reads the oil sender and both thermocouples, and serves the configurator
session. The cores exchange copies of their state under a short lock, so
drawing or a slow I²C read does not hold up a CAN frame.

Clocks sends its display brightness, oil pressure, and ABS speed every 50 ms
and both exhaust temperatures every 250 ms. Each measurement carries a
validity flag. A missing thermocouple or a shorted or open oil sender goes
out with its flag cleared, so receivers do not read it as zero.

The EGT readout on the TFT shows the temperature before the DPF. It takes
turns every 5 s with the temperature inside the DPF only while a connected
DPF module reports a regeneration in frame `0x124`.

Core 1 polls GPS through PIO/software serial: receiver TX connects to GP27
and receiver RX to GP28, at 9600 baud. Clocks starts normally without the
receiver or a fix.

Coordinates remain `double` until conversion to microdegrees for `SC_GET_GPS`
and CAN. They survive loss of ECU communication. The ECU sends the old GPS
fields of frame `0x129` as zero, and Clocks does not read them. Polling runs every pass; GPS changes
and validity transitions trigger a new snapshot. Display refresh reuses
the converted speed.

The last position expires after 4 s. UTC comes from one RMC sentence with a
fix and expires after 2 s. Clocks keeps UTC for CAN and SC and publishes it
with seconds and validity in `0x131` every second.

The GPS icon on the TFT has a green dot with a fix and a red one blinking
once a second without it. The receiver sends NMEA also without a fix; when
no sentence with a valid checksum has arrived for 3 s, Clocks treats it as
missing. The speed then reads N/A, as for a missing thermocouple, and the dot
blinks twice as fast.

Once a second Clocks also sends the position with GPS speed (`0x12D`) and the
position with the date and time to the minute in UTC (`0x12E`). Its device
tree refers to the GPS binding in `src/common/bindings/`.

The charge lamp follows engine RPM and battery voltage from the ECU. It stays
lit while the engine is stopped or those values are stale, and goes out once
the voltage confirms charging after the start. It lights again when charging
stops and blinks at 2 Hz on overvoltage.
The limits and confirmation times are in [`config.h`](src/Clocks/config.h);
the TFT voltage gauge keeps its own range.

| File | Responsibility |
|---|---|
| [`Cluster.cpp`](src/Clocks/Cluster.cpp) | square waves for the factory gauges |
| [`Gauge.h`](src/Clocks/Gauge.h), [`simpleGauge.cpp`](src/Clocks/simpleGauge.cpp), [`tempGauge.cpp`](src/Clocks/tempGauge.cpp), [`pressureGauge.cpp`](src/Clocks/pressureGauge.cpp) | gauge scaling |
| [`TFTExtension.cpp`](src/Clocks/TFTExtension.cpp) | TFT drawing |
| [`logic.cpp`](src/Clocks/logic.cpp) | start-up and loops of both cores; turns CAN signals into gauge, display, and buzzer state |
| [`clocksState.cpp`](src/Clocks/clocksState.cpp) | state copies passed between the cores |
| [`speed.cpp`](src/Clocks/speed.cpp) | ABS pulse frequency to speed |
| [`oilPressure.cpp`](src/Clocks/oilPressure.cpp) | ADC reading to bar, sender short and open detection |
| [`egt.cpp`](src/Clocks/egt.cpp) | both MCP9600 readings; a missing converter is looked for again |
| [`gps.cpp`](src/Clocks/gps.cpp) | local GPS, fresh UTC and GPS frame encoding |
| [`chargeLamp.cpp`](src/Clocks/chargeLamp.cpp) | charge lamp state from RPM and voltage |
| [`buzzer.cpp`](src/Clocks/buzzer.cpp), [`buzzerStrategy.cpp`](src/Clocks/buzzerStrategy.cpp) | tones and warning patterns |
| [`can.cpp`](src/Clocks/can.cpp) | CAN reception and the brightness, oil and speed, EGT, GPS position and GPS UTC frames; the MCP2515 filters admit only Fiesta IDs |

The CAN controller and the TFT share one SPI bus. The display writes in
bands of `HAL_DISPLAY_SPI_MAX_TRANSFER_BYTES`, set in
[`hal_project_config.h`](src/Clocks/hal_project_config.h), so CAN gets the
bus between two bands.

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

Fiesta_clock keeps UTC in a PCF8563 and sends it once a second as
`CAN_ID_RTC_UPDATE` (`0x130`). The frame has integrity 1 only for trusted UTC;
otherwise its date and time bytes are zero. A marker in the HAL key-value store
records that this firmware has set the RTC to UTC. Until then the RTC holds the
local time written by older firmware: the display shows it unchanged, and the
frame reports no integrity. The marker is stored only after the RTC reads back
the new UTC. If the read-back or the marker fails and the flash answers that
the marker is missing, the clock writes the same moment back as local time, so
the display and the next start read it right. If the flash gives no answer,
the marker may be there: the RTC keeps UTC, shown right but not trusted, and
the clock confirms the marker every 30 s. A start that cannot read the flash
shows the RTC as UTC without trust, as most clocks have the marker by then.

The clock sets the RTC from the GPS UTC frame of Clocks (`0x131`), the only ID
its MCP2515 filters accept. The first valid frame sets an RTC without trusted
UTC, also one whose registers hold no valid date. A running RTC is corrected
only when it is more than 2 s off, at most once in 10 minutes; after an RTC
error, a failed read included, or a flash error the next attempt waits 30 s.
Every write is read back and checked. Frames read after a main-loop pause longer
than 500 ms, or while the buttons or the configurator edit the time, are
dropped. Without GPS or CAN the RTC keeps running on its own.

The display, the buttons and the configurator use CET/CEST, converted with
`hal_time`. Setting the time converts it to UTC. A local time skipped by the
spring change counts as CET; the repeated autumn hour counts as its first,
CEST pass. The weekday follows the date, so the buttons do not set it. The
configurator can read and set the date and time; a commit is rejected unless
the whole date is valid, leap years included.

The module also shows the time, temperatures from two DS18B20 sensors, and the
supply voltage on a 128×32 SSD1306 OLED driven by JaszczurHAL. The OLED and RTC share the
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
| CAN0 "main" | ECU, Clocks, Fiesta_clock | traffic between modules |
| CAN1 "OBD-2" | ECU, OBD-II port | external diagnostic tools |

On CAN0 the ECU publishes engine state, RPM, throttle, boost, and fuel. It
reads oil pressure, wheel speed, and EGT from Clocks, the DPF state from frame
`0x124`, and the RTC time of Fiesta_clock (`0x130`), which dates new DTCs. DTCs never go out on CAN0;
diagnostic tools read them over CAN1. Clocks sends its display brightness
(`0x126`), oil pressure with ABS speed (`0x12A`), and both exhaust
temperatures (`0x12F`), GPS position and time (`0x12D`, `0x12E`), and GPS
UTC (`0x131`). Fiesta_clock transmits RTC time in
UTC (`0x130`) and reads only the GPS UTC of Clocks. On CAN1 the ECU answers
OBD-II and UDS requests from whatever tool is connected. The exhaust and DPF
temperature PIDs and the oil pressure DID answer with a negative response
(`0x22`, conditions not correct) while Clocks marks that value invalid or its
frames stop. A bus-off controller or a refused
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
| Auxiliary | GPS position and UTC in Clocks; RTC time in UTC from Fiesta_clock |

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
