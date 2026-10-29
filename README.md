# Fiesta

[Polski](README.pl.md)

Firmware and electronics for a Ford Fiesta 1.8 diesel with a custom engine
combining 1.8D indirect-injection and 1.8TDDI direct-injection parts and a VP37 pump.
[JaszczurHAL](https://github.com/jaszczurtd/JaszczurHAL) supplies the hardware
interfaces and development tools. Author: Marcin "Jaszczur" Kielesiński.

The project is a proof of concept. The complete system has not been validated
as an integrated installation in a road-going car. ECU and VP37 trials use a
separate engine on a steel frame. Clocks and the ECU's supervisory functions
have been used in the car without VP37 actuation.

## Modules

| Module | Purpose |
|---|---|
| [ECU](src/ECU/README.md) | Engine, VP37 and boost control, relay outputs, diagnostics and DTC storage. |
| [Clocks](src/Clocks/) | Factory gauges, TFT readouts and warnings; oil pressure, vehicle speed and exhaust temperatures. |
| [Adjustometer](src/Adjustometer/README.md) | VP37 position measurement, delivered to the ECU over I²C. |
| [Fiesta_clock](src/Fiesta_clock/) | RTC time and air temperatures on CAN, local display. |
| [VP37TestBench](src/VP37TestBench/) | The shared VP37 controller on a separate pump test stand. |
| [SerialConfigurator](src/SerialConfigurator/README.md) | Linux GUI and CLI for USB discovery, settings and ECU bench tests; the GUI also flashes firmware. |

DPF is an inactive entry in SerialConfigurator. It has no firmware, discovery
or flashing support.

## Getting started

```bash
git clone --recurse-submodules https://github.com/jaszczurtd/Fiesta.git
cd Fiesta
```

Read [setup and firmware development](doc/BUILDING.md) before running
`runmefirst.sh`: it installs dependencies and clears module build directories.
After updating an existing checkout, use `./scripts/init_hal_submodule.sh`
to select the HAL revision recorded by Fiesta; it stops if HAL has local changes.

## Documentation

- [Architecture and communication](ARCHITECTURE.md)
- [Builds, upload and debugging](doc/BUILDING.md)
- [Host tests and CI](doc/TESTING.md)
- [Updating JaszczurHAL](DEPENDENCIES.md)
- [Serial Configurator protocol](src/common/scDefinitions/PROTOCOL.md)
- [MISRA scope and review policy](MISRA.md)
- [Daily systemd build](src/ECU/scripts/systemd/README.md)
- [Gallery and bench photos](doc/GALLERY.md)

## Validation scope

The project does not claim ISO 26262, AUTOSAR or formal MISRA compliance.
MISRA screening covers ECU, shared VP37 code and Adjustometer; the latter two
have a gate rejecting active findings outside recorded deviations. Host tests
use HAL mocks and do not establish electrical or mechanical behaviour. The
project has an engine test stand, but no automated HIL rig.

Neither RP2040 nor RP2350 is AEC-Q100 qualified. Engine speed uses GPIO edge interrupts;
Adjustometer uses PIO/DMA period capture. Hardware trials remain necessary
when changing the control path. Test and CI results describe their checked
scope; they do not replace review or validation of the whole vehicle.
