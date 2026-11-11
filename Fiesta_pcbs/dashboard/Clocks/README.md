# Clocks carrier PCB

Open [Clocks.kicad_pro](Clocks.kicad_pro) with KiCad 9. The schematic covers
the RP2040-Zero dashboard controller, two Adafruit MCP9600 modules, GPS,
CAN, TFT wiring and the vehicle interfaces. Revision **0.1-review** is a
two-layer **70 × 75 mm** carrier, drawn 75 mm wide and 70 mm high. It is a
first layout for placement, assembly and enclosure review.

[Schematic PDF](Clocks.pdf) · [Board BOM](Clocks-bom.csv) ·
[Module fit drawing, 1:1](Clocks-module-fit.pdf) ·
[Assembled preview](Clocks-3d.png) · [Carrier without modules](Clocks-carrier-3d.png) ·
[Top copper](Clocks-top.svg) · [Bottom copper, mirrored](Clocks-bottom.svg) ·
[Top assembly](Clocks-assembly-top.pdf) · [Bottom assembly](Clocks-assembly-bottom.pdf)

Keep the adjacent ECU and Adjustometer library directories when copying
the project. The project library tables use relative paths to their shared
symbols and footprints. Keep `Fiesta_pcbs/clock/models` for the RP2040-Zero
preview. The `libraries` directory holds the Clocks parts.

## Layout and review assumptions

All SMD parts are on the **bottom**: R1-R19 and C6. All through-hole parts,
connectors and modules are on the **top**. The existing circuit is retained;
no buffer or additional capacitors are fitted.

J3 (TFT) and the RP2040-Zero USB connector face the upper edge. The two
MCP9600 thermocouple terminals face the lower edge. J1/J2 occupy the lower
right corner; J4 is beside the GPS header. Check cable access with the
actual plugs, antenna and enclosure.

CAN and GPS overlap low through-hole components. This layout assumes
**12 mm from the carrier's top surface to the underside of each module PCB**:
Q1-Q7 sit under CAN, and D1/D2/D4/D5 under GPS. Keep those fitted parts,
their leads and solder below **8 mm**. Use matching raised sockets and
insulated supports; account for any components protruding below the module.
The 12 mm height is a layout assumption, not a measured connector specification.
The local `Raised12mm` footprints retain the original pad positions; their
courtyards reserve the sockets and their fabrication outlines show the full
module bodies. This height-dependent overlap needs a physical fit check.

RP2040-Zero and MCP9600 previews assume 8.5 mm underside clearance. The
[3D models](models/README.md) are assembly envelopes, not detailed production
models. The four Ø2.7 mm holes support the MCP9600 modules; no separate
enclosure mounting pattern has been selected. The TO-220 regulators remain
upright, with limited room for heatsinks. Confirm cooling and enclosure
height before ordering a prototype.

The carrier uses 1.6 mm board thickness, 0.25 mm clearance, 0.30 mm signal
tracks with local 0.25 mm escapes, and 0.80 mm supply tracks with local
0.60 mm escapes. Ground pours on both sides are joined by vias. The ground
pads of U3/U4/U8 and C3/C5/C10 connect directly to copper; use adequate heat
when hand soldering them. The remaining ground pads use thermal reliefs.
Review manufacturing constraints with the chosen fabricator before CAM export.

## Harness connectors

J1-J4 use **vertical JST XH headers, 2.50 mm pitch**, from `Connector_JST`.
Pin numbers below are footprint numbers; the square pad marks pin 1.
Match these numbers when crimping the harness.

| Header | Pins |
| --- | --- |
| J1, sensors | 1 CAN H; 2 CAN L; 3 GND; 4 oil pressure; 5 ABS signal; 6 filtered ABS supply |
| J2, dashboard | 1 switched +12 V; 2 GND; 3 tachometer; 4 speedometer; 5 oil warning lamp; 6 charge warning lamp |
| J3, TFT | 1 +3.3 V; 2 GND; 3 CS; 4 reset; 5 D/C; 6 MOSI; 7 SCK; 8 switched backlight supply; 9 MISO |
| J4, battery | 1 permanent +12 V for GPS; 2 GND |

R12 = 1 kΩ and R19 = 4.7 kΩ deliberately remain in parallel in the ABS
input. Fit both. The oil sensor pull-up R14 remains 220 Ω.

## Modules and assembly

U5/U6 use the original [Adafruit MCP9600 breakout, product 4101](https://github.com/adafruit/Adafruit-MCP9600-PCB).
Each footprint includes the 24.13 × 20.32 mm module outline, a nine-pin
2.54 mm header and two Ø2.7 mm carrier holes for M2.5 spacers. Their centres
are 19.05 mm apart. With the header along the lower edge and components
facing up, pins 1-9 run left to right: **VIN, GND, SCL, SDA, A1, A2, A3,
A4, ADDR**. The thermocouple connects to the terminal on the module.
Leave room for its wires and keep heat sources away from that terminal.

U5 ADDR connects to GND for address **0x60**. U6 ADDR stays open; its onboard
10 kΩ pull-up selects **0x67**. Leave the address solder jumpers open on
both modules. A1-A4 are unused. R8/R9 and the two modules' onboard 10 kΩ
pull-ups give about 2.42 kΩ on each I2C line. The firmware uses 100 kHz.
The [Adafruit pinout guide](https://learn.adafruit.com/adafruit-mcp9600-i2c-thermocouple-amplifier/pinouts)
describes these address and pull-up connections.

U9 uses the measured **25 × 35 mm GY-NEO6MV2** module. Its four-pin 2.54 mm
header is centred on the 25 mm edge, with its axis 1 mm from that edge.
Viewed from above with the header along the lower edge, pins 1-4 are
**VCC, RX, TX, GND**, left to right. Use the local edge-header footprint.
Reserve space for the antenna cable, antenna and module support.

U7 uses a local raised mounting variant of the **40 × 27 mm MCP2515/TJA1050**
footprint from ECU, including its seven-pin SPI header and two-pin CAN connection. U2 uses
the shared RP2040-Zero footprint. Fit matching sockets/headers at all
module contacts and account for their height. Use insulated supports;
keep copper and components clear of screw heads under the MCP9600 modules.

BZ1 is an **active 3.3 V DC buzzer**, Ø11.5 mm, height 9 mm, with 8 mm lead
spacing. Pin 1 is positive. The firmware switches its supply through Q2;
it requires a buzzer with an internal oscillator. Verify the part's rated
voltage and polarity before assembly.

Q3-Q6 are ZTX601 Darlington transistors. Their local symbol maps **1 = E,
2 = B, 3 = C** to the wide TO-92 footprint, viewed from above with the flat
face aligned to the footprint. Form the leads to 2.54 mm pitch. Q1 is
2N7000 (1 S, 2 G, 3 D); Q2/Q7 are BC307 (1 C, 2 B, 3 E). Do not substitute
TO-92 parts by shape alone.

Print the fit drawing at 100%, check its 50 mm scale and compare the
modules and buzzer with it before ordering. The drawing shows their top
views. Also print the assembly drawings at 100% and check the header
orientation, mounting heights and enclosure clearances on the proposed carrier.

## Power and component selection

U3 is L7805; U4 and U8 are **LM2937ET-3.3**, in vertical TO-220 packages.
All three use 1 = IN, 2 = GND/tab, 3 = OUT. U4 supplies the peripherals;
U8 keeps GPS powered from J4 when ignition is off. GPS TX connects
directly to GP27, and GPS RX connects to GP28.

Resistors and ceramic capacitors use 0805, except R15-R18, which use 2512
with a rating of at least 0.5 W. Polarized capacitors use radial Ø6.3 mm
footprints with 2.50 mm lead spacing. Check body heights against the
enclosure. C2/C3 are **100 µF/25 V aluminum electrolytics**; select parts
that retain at least 10 µF and ESR between 0.01 Ω and 3 Ω throughout the
operating temperature range, as required by the
[LM2937 datasheet](https://www.ti.com/lit/ds/symlink/lm2937.pdf).
Do not replace them with arbitrary ceramic capacitors. Other capacitor
voltage requirements appear in the BOM; parts on the raw supply require
at least 50 V.

Place C2/C3 close to their regulator outputs. Keep regulator ground returns short.
Provide heatsink clearance and check U3/U4/U8 dissipation at maximum input
voltage and load; the LDO's 500 mA rating does not establish the assembly's
continuous current capability. In the GPS input, R17 = 100 Ω also limits
the available current and dissipates power.

**Use USB power or vehicle power, never both at once.** The circuit has
no USB/vehicle power arbitration. Use fused, transient-protected vehicle
feeds. Qualify supply stability, cold starts, regulator temperature and
the assembled module clearances on the prototype. The selected CAN module
retains the existing 3.3 V supply arrangement; verify communication with
the installed module and bus over the intended operating range.

## Library sources

The MCP9600 carrier geometry and contact mapping follow Adafruit's Eagle
board files. Designed by Adafruit Industries; the upstream board carries
the Creative Commons Attribution/Share-Alike license and requires keeping
its attribution. The carrier symbol omits the thermocouple terminal from
the host PCB pad list.

The ZTX601 symbol derives from KiCad's `Device:Q_NPN_Darlington`, by the
KiCad community, with numeric pins and the Diodes Incorporated E-Line
pinout. It retains the [KiCad library license and exception](../../clock/footprints.pretty/LICENSE.md).
The GPS and buzzer footprints use the measured dimensions above.
