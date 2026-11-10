# Fiesta_clock PCB

Fiesta_clock is a 70 × 70 mm, two-layer carrier for RP2040-Zero, an
MCP2515/TJA1050 CAN module and a DS3231 Mini RTC module. Open
[Fiesta_clock.kicad_pro](Fiesta_clock/Fiesta_clock.kicad_pro) with KiCad 9.
Keep the adjacent ECU and Adjustometer library directories when copying the
project; its library tables use relative paths to those shared footprints.

[Assembly drawing](Fiesta_clock/Fiesta_clock-assembly.pdf) ·
[Schematic](Fiesta_clock/Fiesta_clock.pdf) ·
[3D preview](Fiesta_clock/Fiesta_clock-3d.png) ·
[Board BOM](Fiesta_clock/Fiesta_clock-bom.csv) ·
[Fabrication package](Fiesta_clock/Fiesta_clock-fabrication.zip) ·
[Fabrication notes](Fiesta_clock/Fiesta_clock-fabrication.md)

## Layout and wiring

J1 (`Set-buttons`), J7 (`LEDs`) and J3 (`OLED module`) sit along the bottom
edge. J4 (`temp-sensors`) and J6 (`Power`) sit along the opposite edge.
All five use vertical JST XH headers with 2.50 mm pitch. The RP2040-Zero
USB connector faces left. Four Ø3.2 mm mounting holes form a 62 × 62 mm
square, with their centres 4 mm from each board edge.

Pin numbers below are the footprint numbers; square pads mark pin 1.
J4/J6 face the opposite direction to the lower headers. Match the numbers
when crimping the harness rather than assuming a common left-to-right order.

| Header | Pin assignment |
| --- | --- |
| J1, buttons | 1 GND; 2 SET; 3 MINUTE; 4 HOUR |
| J7, LEDs | 1 LED1; 2 LED2; 3 LED3; 4 GND |
| J3, OLED | 1 GND; 2 SDA; 3 SCL; 4 3.3 V |
| J4, temperature sensors | 1 3.3 V; 2 TEMP A; 3 GND; 4 3.3 V; 5 TEMP B; 6 GND |
| J6, power and CAN | 1 +12 V; 2 ignition input; 3 GND; 4 CAN H; 5 CAN L |

J5/J8/J9, SW1-SW3, D3-D5 and U1/U3 belong to the external harness.
The schematic shows them; the PCB and board BOM exclude them.

## Modules and parts

J2 uses a male 1×5, 2.54 mm header for the RTC module's female socket.
The 13 × 14 mm module lies parallel to the carrier, about 8.5 mm above it.
Viewed from above with the header at the lower edge, its order is
**−, NC, C, D, +**, from left to right. Pin 1 (+3.3 V) is on the right;
pins 2/3 carry SDA/SCL, pin 4 is unconnected and pin 5 is GND.

The RTC outline assumes a nominal 1.27 mm distance from the header axis to
the module's lower edge, with extra courtyard clearance. Compare that distance,
the 2.54 mm pitch and the battery clearance with the actual module before
ordering boards. Check its I2C pull-ups and battery charging arrangement
against the installed battery. The [3D module models](models/README.md)
show simplified mounting envelopes.

U7 uses the shared 40 × 27 mm MCP2515/TJA1050 footprint from the ECU,
including its seven-pin SPI header and two-pin CAN connection. U2 uses the
shared RP2040-Zero footprint. Fit matching sockets/headers at all module
contacts; account for their height when checking the enclosure.

Resistors and ceramic capacitors use 0805 packages. C2-C4 use radial
Ø6.3 mm footprints with 2.50 mm lead spacing. Select capacitor voltage
ratings and ESR for their respective supply and regulator requirements.
Q1 uses the wide TO-92 footprint: form its leads to 2.54 mm pitch and
check C/B/E orientation. D1 and D2 have marked cathodes.

## Regulators and operation

U4 (L7805) and U5 (LM2931-3.3) lie flat with their metal tabs facing the PCB.
Their `FiestaClock:TO-220-3_Horizontal_TabDown_GND` footprint provides an
exposed copper contact connected to pin 2, without solder paste or silkscreen
under the tab. GND extends beneath both bodies on both copper layers;
stitching vias join the areas. Pin 2 has its own electrical connection.
Use only regulator variants whose pin 2 and tab are GND. Secure the tabs
through the Ø3.5 mm holes and keep mounting hardware within the tab area.
The design uses these copper areas without separate heatsinks.

Never connect USB power and vehicle power at the same time. The circuit
retains that operating restriction. R9/R10 remain 47 kΩ / 10 kΩ; the firmware
voltage conversion still needs the corresponding update. Check supply
stability, regulator temperature and the complete assembly on the prototype.
The JST XH body/keying and the mounting holes must also match the chosen
connectors and enclosure.

The thermal footprint derives from the KiCad library's
`Package_TO_SOT_THT:TO-220-3_Horizontal_TabDown`, by the KiCad community.
It retains the [KiCad library license](footprints.pretty/LICENSE.md).
