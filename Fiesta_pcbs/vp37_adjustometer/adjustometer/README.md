# Adjustometer prototype PCB

Open `adjustometer.kicad_pro` in KiCad 9 with the standard symbol and
footprint libraries installed. The project tables load the RP2040-Zero
symbol and footprint from `libraries/` through `${KIPRJMOD}`. No global
library edits are needed. The circuit values and component assignments
live in `adjustometer.kicad_sch`.

To mount the complete carrier on another PCB, use the
[Adjustometer module symbol and footprint](libraries/Adjustometer_Module.md).
The module instructions describe the assembly variant with J1/J2 headers
facing down.

The two-layer PCB measures 45 x 36 mm and uses 0805 resistors and ceramic
capacitors, through-hole electrolytics and headers, TO-92 transistors,
and a SOT-223 regulator. It includes routed connections, ground planes on
both copper layers and four M3 mounting holes. The RP2040-Zero's USB end
faces the top edge; J1 sits along the bottom and J2 on the right.

See the [PCB layers](../adjustometer-pcb.pdf), [3D preview](../adjustometer-pcb.png)
and [fabrication package](../adjustometer-fabrication.zip). The 3D preview
shows the carrier board and standard components; it has no model of the
RP2040-Zero module. Its outline and mounting pads are visible.

## Board geometry and fabrication

- FR-4, nominal 1.6 mm thickness, two copper layers, nominal 35 um copper.
- Four unplated 3.2 mm holes, with centres 3.5 mm from each adjacent edge;
  their rectangular spacing is 38 x 29 mm.
- Copper keepouts of 3.2 mm radius around the mounting-hole centres
  reserve space for screw heads on both sides.
- Signal tracks: 0.25 mm. Supply and ground tracks: 0.5 mm.
  Minimum copper clearance: 0.2 mm; copper-to-board-edge clearance: 0.5 mm.
- Through vias: 0.6 mm diameter with 0.3 mm drills. Ground planes use
  thermal reliefs and stitching vias.
- PCBWay fabrication revision `1.3-PCBWay`: ENIG, green solder mask and
  white silkscreen on both sides. Text height is at least 0.8 mm, with
  0.15 mm strokes and spacing. Mask expansion is 0.055 mm, with a 0.11 mm
  minimum web. The circuit and Rev1_3 module interface are unchanged.
- For standalone use, all components mount on the top. In the plug-in
  variant, J1 and J2 mount from the underside as described in the module
  instructions above. Fit the RP2040-Zero on headers with clearance for
  its underside components; keep access to USB and buttons.

The ZIP contains Gerber X2 files for copper, masks, silkscreen, top paste
and outline, separate PTH/NPTH Excellon drills, drill maps, a Gerber job,
a mechanical drawing, an assembly-reference BOM and checksums.
Use both drill files. The outline and drill coordinates share the same
absolute origin. Send the ZIP with the settings in the
[PCBWay fabrication specification](PCBWay-fabrication.md).
Resolve the manufacturer's DFM comments and check the physical parts
before ordering a prototype.

After schematic edits, run ERC, then **Tools -> Update PCB from Schematic**
(`F8`). Review placement and tracks, refill zones with `B`, and run DRC
with schematic parity before regenerating outputs.

## Connections and power

| Connector | Pins | Connection |
| --- | --- | --- |
| J1 | 1, 2 | VP37 inductive sensor winding connections |
| J1 | 3, 4 | GND |
| J1 | 5 | Fuel temperature sensor (pump harness pin 7) |
| J2 | 1, 2, 3, 4, 5 | Regulated 5 V input, GND, SDA, SCL, 12 V input |

With USB at the top and the component side facing you, J1 pin 1 is on
the left and J2 pin 1 is at the top. J1 pin numbers are printed on the
back; the front labels A/B/G/T identify the winding, ground and temperature
connections.

J1 is a five-pin 2.54 mm board header. Its pins 1-5 connect to pump
harness pins 1, 2, 3, 4 and 7, respectively. Fit the temperature wire
to J1 pin 5. The header does not represent the mechanical connector
fitted to the pump.
J2 pins 1-4 retain their wiring; pin 5 supplies the oscillator regulator and
the supply-voltage divider. The board needs both 12 V and 5 V, with a common
return; U1 supplies only the 8 V oscillator rail. Use external supply
protection suitable for the installation. Disconnect external 5 V when
powering the RP2040-Zero through USB: its 5 V header and USB VBUS share a net.
The I2C pull-ups use the module's 3.3 V output.

Firmware connections are GP0/SDA, GP1/SCL, GP2/OSC_OUT, GP28/fuel temperature
and GP29/supply divider. See the [firmware documentation](../../../src/Adjustometer/README.md).

## Assembly choices

- R1-R11: 0805 (2012 metric) SMD resistors.
- C1/C2: 10 nF ceramic capacitors, 0805; use a stable C0G/NP0 dielectric
  suitable for the oscillator, rated for at least 25 V.
- C4: 100 nF ceramic capacitor, 0805, X7R, rated for at least 25 V.
  Check the selected regulator's
  input-capacitor requirement against this retained circuit value.
- C3/C5-C9: radial electrolytics, 5 mm diameter, 2 mm lead spacing.
  Use parts rated for at least 25 V. Pad 1 is positive.
- U1: UA78M08 in SOT-223, with input on pad 1, GND on pad 2 and the tab,
  and output on pad 3. The symbol identifies the QDCYRQ1 package variant.
- Q1: BC547, TO-92, pads 1/2/3 = C/B/E.
- Q2: Toshiba 2SK170, TO-92, pads 1/2/3 = D/G/S. The schematic uses
  `Device:Q_NJFET_DGS`. Orient the part using the package drawing in the
  Toshiba datasheet; the footprint has 1.27 mm lead spacing.
- U2: Waveshare RP2040-Zero on through-hole headers, with 2.54 mm pitch and
  15.24 mm between side rows. The footprint follows the project's symbol
  numbering; other RP2040-Zero libraries may number the same pads differently.
  Leave clearance for the underside components, USB plug and BOOT/RESET buttons.

These package choices define the prototype BOM. The layout retains the
schematic's circuit values. Power protection, regulator decoupling and
operation on hardware still need electrical review and prototype testing.

## References

- [Waveshare module schematic](https://files.waveshare.com/upload/4/4c/RP2040_Zero.pdf)
  and [mechanical drawing](https://www.waveshare.com/img/devkit/RP2040-Zero/RP2040-Zero-details-size.jpg).
- [TI UA78M-Q1 datasheet](https://www.ti.com/lit/gpn/UA78M-Q1).
- [Toshiba 2SK170 datasheet](https://radiotovar.kz/pdf/33702.pdf),
  page 1: package dimensions and lead identification.
- [Local library provenance and licence](libraries/README.md).

To check the project and regenerate fabrication files from this directory:

```sh
kicad-cli sch erc --exit-code-violations -o /tmp/adjustometer-erc.rpt adjustometer.kicad_sch
kicad-cli pcb drc --schematic-parity --exit-code-violations -o /tmp/adjustometer-drc.rpt adjustometer.kicad_pcb
kicad-cli pcb export gerbers -o /tmp/adjustometer-fabrication/ --disable-aperture-macros --layers F.Cu,B.Cu,F.Mask,B.Mask,F.SilkS,B.SilkS,F.Paste,Edge.Cuts adjustometer.kicad_pcb
kicad-cli pcb export drill -o /tmp/adjustometer-fabrication/ --format excellon --excellon-separate-th --excellon-units mm --drill-origin absolute --generate-map --map-format pdf adjustometer.kicad_pcb
kicad-cli sch export bom -o /tmp/adjustometer-fabrication/adjustometer-bom.csv adjustometer.kicad_sch
```

To regenerate the schematic preview:

```sh
kicad-cli sch export pdf -o ../adjustometer.pdf adjustometer.kicad_sch
```
