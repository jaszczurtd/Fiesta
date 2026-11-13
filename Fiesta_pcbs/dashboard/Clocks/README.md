# Clocks PCB

[Polski](README.pl.md)

The 0.1-review carrier is a first layout for assembly and enclosure review. It measures 70 ×
75 mm and carries the dashboard controller, CAN, GPS and thermocouple modules.

## Before connecting

- Use USB or vehicle power, never both at once. The circuit has no power-source arbitration. Vehicle feeds require fusing and transient protection.
- CAN and GPS assume 12 mm clearance from the carrier to the module underside; parts below them must stay under 8 mm. Confirm the fit with the actual sockets and enclosure.
- Check cooling of U3/U4/U8 and the specified regulator capacitor type and ESR. A regulator current rating does not establish the assembled board rating.
- Follow the connector numbers and module address settings in the assembly notes before using the firmware guide.

## Assembly and startup

The [assembly reference](ASSEMBLY.md) contains schematics, previews, connectors, component
selection and manufacturing requirements. It uses the KiCad project names and numbering.

After checking the hardware, follow the [firmware guide](../../../src/Clocks/README.md).
