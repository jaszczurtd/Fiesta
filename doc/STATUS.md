# Project status and next steps

[Polski](STATUS.pl.md) · [Home](../README.md)

Fiesta is a working prototype of a distributed vehicle-control system.
ECU and VP37 development uses an engine on a separate steel-frame stand.
Clocks and ECU supervisory functions have been used in the car without VP37
actuation. The complete installation, including VP37, has not been validated
in a road-going car.

## What is available

| Area | Current scope | Where to start |
| --- | --- | --- |
| ECU and VP37 | Position control, calibration, diagnostics and bench measurements. The optional start/idle supervisor is disabled by default; driver demand feeds the position loop directly. | [ECU setup](../src/ECU/README.md) |
| Live and saved measurements | ECU BENCH build/upload, serial capture, live charts and offline comparisons. | [Viewer](../tools/vp37-viewer/README.md) |
| Dashboard | Factory gauges, TFT readings, warnings and output tests. | [Clocks](../src/Clocks/README.md) |
| Time and temperature | RTC, GPS time synchronization, CAN publication and a local display. | [Fiesta_clock](../src/Fiesta_clock/README.md) |
| Position sensing | Adjustometer frequency measurement and I²C feedback. | [Adjustometer](../src/Adjustometer/README.md) |
| Configuration | Linux GUI and CLI for discovery, settings and functional tests; firmware upload in the GUI. | [SerialConfigurator](../src/SerialConfigurator/README.md) |
| DPF | Inactive configurator entry; no firmware, discovery or upload support. | [System architecture](../ARCHITECTURE.md) |

## Hardware and firmware

A PCB design in the repository does not by itself establish firmware compatibility.
Check the board revision as well as the processor assembly before uploading.

| Hardware | Firmware relationship | Remaining work or constraint |
| --- | --- | --- |
| Existing ECU wiring with Waveshare RP2350-Plus or RP2040-Plus | Current `rp2350-plus` and `rp2040-plus` assemblies. Adjustometer and expander share I²C0. | The PCF8574A runs at the configured 400 kHz despite its 100 kHz rating. See [ECU interfaces](../src/ECU/guides/BENCH.md#bench-image-and-hardware). |
| [ECU PCB 3](../Fiesta_pcbs/ecu/README.md) | New layout separates Adjustometer and expander buses and uses non-inverting TC4427 drive. | Adapt bus assignments and output polarity, then validate on the manufactured board. Current firmware must not be assumed to match it. |
| [Fiesta_clock PCB](../Fiesta_pcbs/clock/README.md) | New board uses DS3231 Mini; firmware defaults to PCF8563 and offers a DS3231 variant. | Select the RTC variant and adapt/check the new voltage divider conversion. |
| [Clocks PCB](../Fiesta_pcbs/dashboard/Clocks/README.md) | Dashboard hardware and its assembly instructions. | Use USB or vehicle power, never both. |
| [Adjustometer PCB](../Fiesta_pcbs/vp37_adjustometer/adjustometer/README.md) | Position sensor used by the ECU and separate VP37TestBench. | Check both power rails, common ground and baseline startup order. |
| Separate VP37TestBench | Dedicated project with its own display and power-stage enable wiring. | Use its [module guide](../src/VP37TestBench/README.md); do not substitute it for the ECU BENCH variant. |

## Next work and its prerequisites

The remaining integration work is to align new boards with their firmware,
validate them electrically, and then validate the complete engine-control
installation. Control-loop tuning needs measurements on the mechanical
system. A successful build or a host test does not complete those steps.
These are dependencies, not a release schedule or a claim that the vehicle
installation is ready.

For development now, use the [build guide](BUILDING.md), the
[host-test guide](TESTING.md) and the module guide matching the hardware.
For understanding the system, start with [architecture](../ARCHITECTURE.md)
and then follow the relevant module link.

## Validation limits

The project makes no ISO 26262, AUTOSAR or formal MISRA compliance claim.
[MISRA screening](../MISRA.md) covers ECU, shared VP37 code and Adjustometer.
Host tests use HAL mocks and do not establish electrical or mechanical behaviour.
There is an engine test stand, but no automated HIL rig. RP2040 and RP2350
are not AEC-Q100 qualified.
