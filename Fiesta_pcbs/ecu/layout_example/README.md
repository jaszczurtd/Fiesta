# EEC-V ECU placement example

Open `ECU-layout-example.kicad_pcb` in KiCad 9 to inspect the component
placement, connector numbering and example power routing. This is a separate,
partial board; the main ECU schematic and PCB remain in `../ECU/`.
It is not a fabrication release or a complete ECU.

[Top preview](ECU-layout-preview.png), [3D view](ECU-layout-3d.png),
[front copper](ECU-layout-top.svg) and [back copper](ECU-layout-bottom.svg)
show the saved board. Both copper plots use the same top-view coordinates;
the back plot is not mirrored. The Ford connector and R1 have no solid 3D
models. Their holes and footprints remain visible; R1 has a marked body
envelope and separate wire pads.

## Included circuits

| Parts | Placement and connection |
| --- | --- |
| J50 | Ford EEC-V connector with physical pad numbers 1-104 |
| J50 pins 71, 92, 97 | Connected to `12V_PWR`, shared with the selected power circuits |
| J50 pins 24, 51, 76, 77 | Connected to GND on both copper layers |
| H1-H6 | Chassis contacts, connected to GND |
| Q4, D21, U2 | Left edge: turbo MOSFET, power diode and TC4427 driver |
| Q20, Q16, D25, U1 | Right edge: VP37 and timing MOSFETs, dual diode and TC4427 driver |
| R50-R52, R55-R57, C21, C40 | Gate resistors, pull-downs and ceramic decoupling from the ECU schematic |
| D3, D22, D24, D37 | Protection diodes from the selected circuits |
| R1 | 0.22 ohm / 50 W aluminium resistor; pad 1 is GND, pad 2 connects to Q20 source |

The selected components retain their references, values, footprints and pad
nets from the ECU schematic. R1 connects to its PCB pads through short wires;
its two mounting holes carry no electrical connection. The rest of the ECU,
including the MCU, load connectors, bulk capacitors and input protection,
is omitted. The remaining Ford connector pads have no assigned nets in this
example. Driver inputs consequently have no signal source on this board.

The example routes the connections between the included components. Trace
widths demonstrate placement and routing; current capacity, voltage drop,
thermal performance and switching behaviour still need a complete ECU design
and verification. Do not use "Update PCB from Schematic" with the full ECU
schematic on this partial board.

## Mechanical dimensions

The outline is 140.80 × 164.42 mm, with two copper layers and a 1.6 mm board
thickness. Connector pads and all six chassis mounting centres retain the
rusEFI blank board coordinates confirmed by the owner. The side KEEP OUT
annotations are removed.

Each chassis contact has a plated 4.75 mm hole and an 8.75 mm copper pad,
exposed on both sides and connected solidly to GND. This interprets the
requested additional 4 mm as a diameter increase relative to the hole,
giving a 2 mm annular contact surface. Copper-to-edge clearance is 0.4 mm;
the closest contact pad is 0.495 mm from the outline.

All five TO-220 packages have their lead axes 4 mm from the corresponding
side edge, with their metal tabs facing the housing. The intended mounting
uses the housing spring clips. R1 has a 50 × 15 mm body and two 8.5 mm-wide
mounting ears, giving a 50 × 26 mm overall envelope. Its keepout and courtyard
follow the body and ears, leaving the spaces beside them available. The wire
pads are 69 mm apart. The two mounting centres retain their 40 × 20 mm
separation and the existing assumption of M3 screws with 3.2 mm PCB holes.

## Libraries and origin

`fp-lib-table` registers the shared `../footprints.pretty` library and the
standard KiCad libraries. The example adds these reusable footprints:

- `Ford_EEC-V_104pin`: adapted from rusEFI `rusefi_lib:eecv`; pin geometry
  is preserved, and legacy silkscreen graphics move to the fabrication layer.
- `ECU_Chassis_GND_D4.75_Pad8.75`: plated chassis ground contact with exposed
  copper on both sides.

The connector footprint and board geometry originate from David/dbh97's
[EEC-V blank board in rusEFI](https://github.com/rusefi/rusefi/tree/23933fda5193d68ad39a90ac7b1e431cc2f7d489/hardware/EEC-V-Blank-Board)
and its shared footprint library. The applicable
[rusEFI license 0.91](RUSEFI-LICENSE.txt) accompanies this adaptation.
The resistor footprint is shared with the ECU project.
