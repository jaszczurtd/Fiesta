# Adjustometer as a plug-in module

Use `Adjustometer:Adjustometer_Module_Rev1_3` on the host schematic. Its
assigned footprint is
`Adjustometer:Adjustometer_Module_45x36mm_Rev1_3`. Both represent the complete
45 x 36 mm carrier in `../adjustometer.kicad_pcb`, revision 1.3, with five-pin
J1 and J2. They are supplied under [CERN-OHL-P-2.0](CERN-OHL-P-2.0.txt).

## Add to another KiCad project

In **Preferences -> Manage Symbol Libraries -> Project Specific Libraries**,
add `Adjustometer.kicad_sym` with the nickname `Adjustometer`. In the
corresponding footprint library manager, add `Adjustometer.pretty` with
the same nickname. Use paths relative to the host project.

For the ECU project at `Fiesta_pcbs/ecu/ECU/`, the paths are:

```text
${KIPRJMOD}/../../vp37_adjustometer/adjustometer/libraries/Adjustometer.kicad_sym
${KIPRJMOD}/../../vp37_adjustometer/adjustometer/libraries/Adjustometer.pretty
```

Place the symbol, connect its pins, then run **Update PCB from Schematic**
(`F8`). Place the footprint on the front of the host PCB. Keep the USB side
accessible and run ERC and DRC after routing. The ECU project is not changed
by this library.

## Pin mapping

The module has ten electrical pads. Pads 1-5 mate with carrier J1;
pads 6-10 mate with carrier J2. All three GND pins must connect to host GND.
Coordinates below are in millimetres, viewed from above, with the carrier's
upper-left corner at (0, 0), X to the right and Y down. No mirror is needed
when the module sits above the host with its component side facing up.

| Module pad | Carrier contact | Signal | X | Y |
| --- | --- | --- | ---: | ---: |
| 1 | J1.1 | Sensor winding A | 9.00 | 32.60 |
| 2 | J1.2 | Sensor winding B | 11.54 | 32.60 |
| 3 | J1.3 | GND | 14.08 | 32.60 |
| 4 | J1.4 | GND | 16.62 | 32.60 |
| 5 | J1.5 | Fuel temperature, pump harness pin 7 | 19.16 | 32.60 |
| 6 | J2.1 | Regulated +5 V input | 41.50 | 12.00 |
| 7 | J2.2 | GND | 41.50 | 14.54 |
| 8 | J2.3 | SDA, 3.3 V I2C | 41.50 | 17.08 |
| 9 | J2.4 | SCL, 3.3 V I2C | 41.50 | 19.62 |
| 10 | J2.5 | +12 V input | 41.50 | 22.16 |

The module needs both supply rails. I2C pull-ups are already present on the
module and connect to 3.3 V; do not pull the bus up to 5 V. Disconnect the
external 5 V supply before powering the module through USB, because its
5 V input and USB VBUS share a net. See the [carrier power and assembly
notes](../README.md) before connecting it.

## Mounting

Fit the carrier's J1 and J2 male headers **from the underside**, with their
pins pointing toward two 1x05, 2.54 mm female sockets on the host PCB.
This is a plug-in assembly variant; the carrier PCB copper and hole
positions stay the same. Its other components remain on top. Choose the
socket/header combination and M3 spacers together so that the carrier sits
parallel to the host, without loading the contacts or touching solder joints.
Order the two host sockets and four spacers separately; they are not separate
schematic symbols or BOM entries.

The host footprint uses 1.7 mm pads with 1.0 mm plated drills. Check the
chosen socket tails against these holes before ordering. Four unnumbered
3.2 mm NPTH holes have centres at (3.5, 3.5), (41.5, 3.5), (3.5, 32.5) and
(41.5, 32.5), giving a 38 x 29 mm mounting pattern. Rule areas reserve a
3.2 mm radius around the screw centres on the outer copper layers for
tracks, vias and zones. They do not prohibit pads: keep other pads out of
these areas when modifying the host layout.

`F.Fab` carries the true 45 x 36 mm board outline, socket outlines and USB
access envelope. `F.SilkS` identifies the module, orientation and contacts.
`F.CrtYd` reserves the carrier with a 0.5 mm margin and extends 4 mm beyond
the USB edge. The footprint contains no `Edge.Cuts` geometry. Host routing
may pass beneath the module; keep other components outside its courtyard.

There is no assembled 3D model or specified stack height. Check socket
height, spacer length, solder-joint clearance, USB plug and button access
with the actual parts. Changing the carrier's outline, connector positions
or hole pattern requires a new matching module footprint revision.
