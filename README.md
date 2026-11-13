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

## Choose a task

| I want to... | Start here |
| --- | --- |
| Understand project readiness, limits and remaining work | [Project and hardware status](doc/STATUS.md) |
| Run the ECU and see measurements | [ECU first use](src/ECU/README.md) |
| View or compare saved measurements | [VP37 viewer](tools/vp37-viewer/README.md) |
| Build and upload firmware | [Builds and debugging](doc/BUILDING.md) |
| Understand modules and their connections | [Architecture](ARCHITECTURE.md) |
| Change settings or test outputs | [SerialConfigurator](src/SerialConfigurator/README.md) |

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
- [VP37 measurement viewer](tools/vp37-viewer/README.md)
- [Updating JaszczurHAL](DEPENDENCIES.md)
- [Serial Configurator protocol](src/common/scDefinitions/PROTOCOL.md)
- [MISRA scope and review policy](MISRA.md)
- [Daily systemd build](src/ECU/scripts/systemd/README.md)
- [Gallery and bench photos](doc/GALLERY.md)

See [project status](doc/STATUS.md) for hardware compatibility and validation limits.
