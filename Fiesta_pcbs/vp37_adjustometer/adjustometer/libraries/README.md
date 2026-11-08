# Adjustometer libraries

`RP2040-Zero.kicad_sym` preserves the symbol embedded in the Adjustometer
schematic. It matches the pin geometry and numbering of
`RP2040-Zero-KiCAD-main.zip`, archive commit
`b22a0c1afb350dda50ebb7cf2d9c5f3dd3e419ce`. That archive supplies the
[CERN-OHL-P-2.0 licence](CERN-OHL-P-2.0.txt).

Modified for Fiesta on 2026-11-08: expose the cached symbol as a project
library, set pin 3 (3V3) to `power_out` to represent the module regulator,
and assign the local THT footprint. Pin numbers and geometry are retained.

`Adjustometer.pretty/RP2040-Zero_THT.kicad_mod` is a new footprint drawn from
[Waveshare's mechanical drawing](https://www.waveshare.com/img/devkit/RP2040-Zero/RP2040-Zero-details-size.jpg),
using the symbol's existing pin numbering. It supports through-hole
headers; it does not provide pads for surface mounting the module.
The courtyard reserves 4 mm beyond the module's USB edge; check the plug
and enclosure clearance when placing it. The original archive footprint
is not used. This new footprint is also supplied under CERN-OHL-P-2.0.
