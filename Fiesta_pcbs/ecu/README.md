# ECU PCB 3

[Polski](README.pl.md)

This is the new two-layer ECU layout for the Ford EEC-V housing. It requires firmware adaptation before use.

## Before connecting

- Current ECU assemblies describe the older wiring. PCB 3 puts Adjustometer on I²C0 (GP0/GP1), the expander on I²C1 (GP2/GP3) and the injector input on GP4.
- TC4427 drive is non-inverting: LOW turns the MOSFET off. Adapt output polarity and check startup/reset behaviour before connecting a load.
- Electrical and thermal validation remains necessary. CAN modules retain the existing 3.3 V arrangement, outside the TJA1050 specified supply range. This requires prototype checks.
- Use the connector map and assembly notes for power wiring, current-sharing contacts and component ratings. The required outer copper is 70 µm; the layout does not establish a measured continuous-current rating.

## Assembly and startup

The [assembly reference](ASSEMBLY.md) contains schematics, previews, connectors, component
selection and manufacturing requirements. It uses the KiCad project names and numbering.

After checking the hardware, follow the [firmware guide](../../src/ECU/README.md).
