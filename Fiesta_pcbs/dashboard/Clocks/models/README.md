# Assembly envelopes

These VRML models visualize the first Clocks layout. They use millimetre
dimensions converted to KiCad's 0.1-inch VRML units. They do not reproduce
every component, connector opening, underside protrusion or mounting hole.

| Model | Body | Underside above carrier |
| --- | --- | --- |
| MCP2515-raised12mm | 40 × 27 mm module PCB | 12 mm, proposed |
| GPS-raised12mm | Measured 25 × 35 mm module PCB | 12 mm, proposed |
| MCP9600-envelope | Adafruit #4101, 24.13 × 20.32 mm | 8.5 mm, proposed |
| Buzzer-D11.5-H9 | Measured Ø11.5 × 9 mm, leads 8 mm apart | On carrier |

The RP2040-Zero envelope is reused from `Fiesta_pcbs/clock/models`, with
8.5 mm underside clearance. Standard parts use the installed KiCad models.
Confirm actual socket heights and module underside clearance before assembly.
The carrier-only preview hides module bodies to expose the parts below them;
it does not describe a different electrical variant.
