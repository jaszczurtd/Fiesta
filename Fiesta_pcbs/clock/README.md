# Fiesta_clock PCB

[Polski](README.pl.md)

A 70 × 70 mm carrier for RP2040-Zero, CAN and DS3231 Mini. Firmware defaults to a different
RTC, so select the board variant deliberately.

## Before connecting

- Use USB or vehicle power, never both at once.
- Select the DS3231 firmware variant for this board. The default firmware selects PCF8563.
- R9/R10 are 47 kΩ / 10 kΩ; the firmware voltage conversion still needs the corresponding update.
- Check regulator temperature, supply stability, RTC header orientation and battery arrangement on the prototype.

## Assembly and startup

The [assembly reference](ASSEMBLY.md) contains schematics, previews, connectors, component
selection and manufacturing requirements. It uses the KiCad project names and numbering.

After checking the hardware, follow the [firmware guide](../../src/Fiesta_clock/README.md).
