# Adjustometer PCB

[Polski](README.pl.md)

A 45 × 36 mm prototype position-sensor carrier for VP37, with RP2040-Zero and a Hartley
oscillator. The firmware guide explains baseline acquisition and feedback validity.

## Before connecting

- Supply both regulated 5 V and 12 V with a common ground. U1 supplies only the 8 V oscillator rail.
- Disconnect external 5 V before powering RP2040-Zero through USB: USB VBUS and its 5 V header share a net.
- J1 pins 1-5 map to pump harness pins 1, 2, 3, 4 and 7. J2 pins 1-5 are 5 V, GND, SDA, SCL and 12 V. Confirm orientation in the assembly notes.
- Reset with the actuator drive off and the mechanism settled. Wait for a valid baseline, then reset the ECU.

## Assembly and startup

The [assembly reference](ASSEMBLY.md) contains schematics, previews, connectors, component
selection and manufacturing requirements. It uses the KiCad project names and numbering.

After checking the hardware, follow the [firmware guide](../../../src/Adjustometer/README.md).
