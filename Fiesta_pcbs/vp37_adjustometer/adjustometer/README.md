# Adjustometer schematic

Open `adjustometer.kicad_pro` in KiCad 9 with the standard symbol and
footprint libraries installed. The project tables load the RP2040-Zero
symbol and footprint from `libraries/` through `${KIPRJMOD}`. No global
library edits are needed. The circuit values and component assignments
live in `adjustometer.kicad_sch`.

The schematic uses 0805 resistors and ceramic capacitors, through-hole
electrolytics and headers, TO-92 transistors, and a SOT-223 regulator. The PCB file is an
empty starting point. In the schematic editor, run ERC, then **Tools ->
Update PCB from Schematic** (`F8`). Place the footprints, define the board
outline and mounting holes, route the nets, and run DRC with schematic
parity before generating manufacturing files.

## Connections and power

| Connector | Pins | Connection |
| --- | --- | --- |
| J1 | 1, 2 | VP37 inductive sensor winding connections |
| J1 | 3, 4 | GND |
| J1 | 7 | Fuel temperature sensor |
| J1 | 5, 6, 8, 9, 10 | Unused on this board |
| J2 | 1, 2, 3, 4, 5 | Regulated 5 V input, GND, SDA, SCL, 12 V input |

J1 is a 2.54 mm board header with numbers matching the pump harness.
It does not represent the mechanical connector fitted to the pump.
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

These package choices define the prototype BOM. Confirm the dimensions
and pinout of the parts to be fitted before ordering the PCB. Layout,
power protection, regulator decoupling and operation on hardware still
need review as part of that board design.

## References

- [Waveshare module schematic](https://files.waveshare.com/upload/4/4c/RP2040_Zero.pdf)
  and [mechanical drawing](https://www.waveshare.com/img/devkit/RP2040-Zero/RP2040-Zero-details-size.jpg).
- [TI UA78M-Q1 datasheet](https://www.ti.com/lit/gpn/UA78M-Q1).
- [Toshiba 2SK170 datasheet](https://radiotovar.kz/pdf/33702.pdf),
  page 1: package dimensions and lead identification.
- [Local library provenance and licence](libraries/README.md).

To regenerate the schematic preview from this directory:

```sh
kicad-cli sch export pdf -o ../adjustometer.pdf adjustometer.kicad_sch
```
