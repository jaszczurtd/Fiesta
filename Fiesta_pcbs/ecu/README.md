# Fiesta ECU PCB3

Open `ECU/ECU.kicad_pro` in KiCad 9. The project contains the ECU schematic and
its revised two-layer PCB for the Ford EEC-V housing. `ECU/ECU.pdf` previews
the schematic; the PCB previews and assembly files are beside the project.
Relative library paths include the shared Adjustometer module library.

The board carries 122 schematic parts and six chassis contacts. Review the
[top](ECU/ECU-top.png), [bottom](ECU/ECU-bottom.png) and
[angled view](ECU/ECU-3d.png), or use the [assembly drawing](ECU/ECU-assembly.pdf)
and [copper layers](ECU/ECU-copper.pdf). The assembly PDF shows the top and bottom
on separate pages. The PDFs retain a 1:1 scale when printed
at actual size. `ECU-bom.csv` and `ECU-assembly.csv` beside the project list
parts and positions. The previews have no solid models for J50, CAN,
Adjustometer or R1; their outlines show the reserved space.

The [mechanical baseline](baselines/README.md) preserves the layout accepted by
the owner. The full PCB retains its outline, Ford connector, six chassis GND
contact holes, edge-mounted power stages and wired resistor mounting. The
[placement example](layout_example/README.md) remains available separately.
The Ford connector and board outline derive from rusEFI's EEC-V Blank Board;
the [rusEFI hardware licence](layout_example/RUSEFI-LICENSE.txt) is included.

`ECU/ECU-connector-map.csv` lists all 104 physical Ford pins. J50 is the only
vehicle connector, including all VP37 signals. The former individual pin
symbols are net labels tied to J50. J42 is a separate two-pin GPIO header
for the existing extension signals; it is not a VP37 connector.
Pins 67 and 69 share `EECV_69`, Q20 drain and D25 anode 3. Both harness contacts
must connect to the same VP37 coil terminal to share current. Pin 68 remains
a separate output. The connector has 44 connected contacts and 60 NC contacts.

[ECU-mechanical-fit.pdf](ECU-mechanical-fit.pdf) provides a 1:1 fit check for the
CAN modules and power resistor. Print at actual size. The electrical and thermal
checks below still apply before fabrication and use.

## Assembly choices

| Parts | Mounting |
| --- | --- |
| R2-R32, R50-R52, R55-R57 | 0805 SMD |
| Non-polarized capacitors | 0805 SMD |
| Polarized capacitors | Radial THT, 25 V; 1/10 µF: Ø5 mm, pitch 2 mm; 100 µF: Ø6.3 mm, pitch 2.5 mm; 2200 µF: Ø16 mm, pitch 7.5 mm |
| U6 CD4051B | SOIC-16, 3.9 × 9.9 mm, 1.27 mm pitch |
| U1/U2 TC4427 | DIP-8, 7.62 mm row spacing; non-inverting |
| Q3, Q5-Q12 2N6037 | TO-126-3, vertical; pins 1=E, 2=C, 3=B |
| U3 L7805 | TO-220-3, vertical |
| D37 1N4728A, 3.3 V | DO-41 THT, vertical, cathode up; 2.54 mm pitch, 1.10 mm finished holes |
| D25 MBR20100CTG | One TO-220-3 package; units A/B share cathode pin 2 |
| A1 MCU | Pico-compatible module on two 1×20 THT headers/sockets |
| U9 Adjustometer | Shared module footprint, revision 1.3 |
| F1, 10 A | External harness fuse shown as a wiring callout; no PCB footprint |
| J32 OBD-II | External socket; excluded from PCB and PCB BOM |
| U7/U8 MCP2515 + TJA1050 | Plug-in modules, components up; headers fitted underneath |
| R1, 0.22 Ω / 50 W | Aluminium body screwed to PCB; solder lugs connected by wires |

R23-R30 replace the eight 1 kΩ branches of the former RN1 bussed resistor array.

U6 retains the existing schematic pin positions in a project symbol. Replacing it
with a stock 4051 symbol without checking pin positions can detach wires.
D25 uses anodes 1/3 and common cathode 2, including the metal tab, according to the
[onsemi datasheet](https://www.onsemi.com/pdf/datasheet/mbr20100ct-d.pdf).
The CAN symbols describe SO as tri-state and INT as open-drain; this removes false
output conflicts without suppressing ERC checks.

The project symbol for 2N6037 uses the E-C-B pinout in the
[Central Semiconductor datasheet](https://my.centralsemi.com/datasheets/2N6034-2N6039.PDF).
These medium-power Darlingtons occupy the board interior. Only D21/D25,
Q4/Q16/Q20 and U3/U4 require the accepted edge positions for spring clips.
Their thermal design still depends on the actual loads and available cooling.
C21/C40 are mounted on the bottom directly between the DIP driver supply and
ground pins. C3/C10 are also on the bottom, beside the regulators.

A1 uses the project symbol `Fiesta_Modules:RaspberryPi_Pico_ECU`, with AGND
(pin 33) defined as a passive ground connection. Its pin positions, numbering
and footprint match the original symbol. J32 chassis ground connects to GND;
intentionally unused pins carry NC markers. `PWR_FLAG` identifies the external
12 V source at harness pin 97 and the supply after D2 feeding L7805.

Adjustometer uses I2C0 on GP0/SDA and GP1/SCL, with pull-ups on its module.
PCF8574 uses I2C1 on GP2/SDA and GP3/SCL, with R31/R32 pull-ups to 3.3 V.
The injector input connects to GP4. R14 is 470 Ω, matching the physical fuel
level circuit. Firmware adaptation follows manufacture of the new PCB.

## Checks before fabrication

PCB3 follows the selected schematic. Electrical and thermal qualification remains necessary:

- U1/U2 use GND on pin 3 and the existing `12V_PWR` power-stage rail on pin 6.
  C21/C22/C40 decouple that rail. TC4427 is deliberately non-inverting: LOW
  turns the MOSFET off. Firmware for the existing inverted hardware must be
  adapted and validated with the new hardware; it has not changed in this task.
  Review startup/reset bias, PWM polarity and transient protection of the driver
  supply. The specified driver supply range is 4.5-18 V; see the
  [Microchip datasheet](https://ww1.microchip.com/downloads/en/DeviceDoc/20001422G.pdf).
  The [IRF540N datasheet](https://www.infineon.com/assets/row/public/documents/24/49/infineon-irf540n-datasheet-en.pdf)
  specifies on-resistance at a 10 V gate drive.
- U7/U8 remain on 3.3 V by the owner's decision, based on operation of the
  existing modules. This is outside the TJA1050 specified 4.75-5.25 V supply
  range. That operating observation does not establish the full temperature,
  supply or CAN loading range. See the
  [NXP datasheet](https://www.nxp.com/docs/en/data-sheet/TJA1050.pdf).
  A future 5 V option requires review of SPI and interrupt logic levels; it is
  not part of the selected assembly.
- The owner confirmed the RP2040-Plus/RP2350-Plus pin and outline compatibility
  with Pico. A1 uses the THT footprint. Fit socket height to clear underside
  joints, and check access to the USB connector. The preview uses a standard
  Pico model; it does not reproduce the exact Plus module.
- J50 uses the accepted Ford connector geometry. Capacitor diameters, pitches
  and 25 V ratings are selected; verify actual heights against the housing.
  Check resistor dissipation and component ratings against the load. An 0805
  footprint alone does not specify a power or voltage rating.
- Check R1 temperature and heat removal under the actual load. A PCB mounting
  pattern does not establish the dissipation available without a heatsink.

The PCB requires 70 µm finished copper on both outer layers and at least 25 µm
plating in the current-sharing vias. Overall board thickness remains 1.6 mm.
Do not substitute 35 µm copper without resizing the current paths.

| Current path | Routing |
| --- | --- |
| J50.67/69 to Q20 drain | 4 mm trunk; each connector exit uses 1 mm on both layers, widening to 2 mm per layer before merging |
| Q20 drain to D25.3 | Parallel 2 mm tracks on both layers |
| Q20 source to R1.2 | 4 mm trunk; short 2 mm tracks on both layers at Q20 |
| Q16 drain and D25.1 | 3 mm trunk, 2 mm terminal connections; connector escape uses 0.8 mm on both layers |
| D25.2 supply and main GND return | 4 mm trunks; short paired terminal connections |

The provisional allocation is 12 A for Q20, 6 A for Q16 and 18 A for their shared
supply and return. These are design assumptions, not measured continuous-current
ratings. Using the external-layer IPC-2221 estimate documented in the
[KiCad calculator manual](https://docs.kicad.org/5.1/en/pcb_calculator/pcb_calculator.html#_track_width),
4 mm of 70 µm copper gives approximately 13 °C rise at 12 A and 32 °C at 18 A.
The estimate does not qualify an enclosed automotive assembly. Check total rail
loading, current sharing, voltage drop and temperature on the prototype,
including cold-coil startup calibration and the freewheel interval. The shunt
telemetry measures the MOSFET on interval and does not establish full-cycle RMS
current. Keep the existing harness protection; this allocation does not specify
a replacement fuse rating.

Layer transitions use groups of 1 mm / 0.5 mm vias, with six or eight vias at
the relevant transitions. Q16 has a short 1 mm fan-out into its six-via row
between the main power trunks. A single signal via is not a substitute. Critical
tracks are locked; custom DRC rules protect the connector escape widths and
separation of SENSOR_A/B from the Q20/Q16 current nets.

Signal tracks use 0.25-0.3 mm and low-voltage supplies use 0.45-0.6 mm.
The other actuator outputs use 0.45-1.2 mm, depending on the available routing
space; qualify their current against each connected load. The Q20/Q16 power
paths follow the wider dimensions listed above. The global minimum trace width
and copper clearance are 0.25 mm; copper-to-edge clearance remains 0.4 mm.
Both outer layers carry GND pours with stitching vias. The ADC branch runs
separately to the R1 terminal pad. The resistor's two connecting wires still
contribute their resistance to the measurement; this is not a four-wire shunt connection.
ERC and DRC do not establish supply, reset or thermal behaviour.

U9 is beside J50 pins 81/82. SENSOR_A/B have short front-layer routes with a
continuous GND guard between them, joined to the ground plane by stitching vias
and to U9 ground. Outside the immediate connector escape, the other actuator
routes also stay at least 3 mm from the sensor pair on either layer. The fixed
neighbouring connector pads limit separation within that escape. The high-current
returns have their own wide paths.

The front silkscreen identifies the project and author in two lines above the
ground return near the bottom edge.

Preserve electrical insulation between live TO-220 tabs and the grounded
housing where required by the part pinout. Six chassis contacts expose GND
copper on both PCB faces, nominally Ø9.75 mm around unchanged plated Ø4.75 mm
holes. H3 is clipped by 0.405 mm at the left edge and 0.025 mm at the bottom;
H4 is clipped by 0.025 mm at the bottom and H5 by 0.055 mm at the left edge.
These flats retain 0.4 mm copper-to-edge clearance without moving the holes or
changing the outline. The mask openings follow the contact shapes on both sides.
The accepted spring-clip mounting does not add individual TO-220 screw holes.

## Custom footprints

The project registers `footprints.pretty` as `FiestaFootprints`. U7/U8 and R1
already reference the new footprints. Their dimensions come from the owner's
measurements, with the assumptions below. Check the printed fit sheet against
both physical parts before ordering a PCB. It includes a 20 mm scale.

### MCP2515/TJA1050 module

`MCP2515_TJA1050_Module_J3_40x27mm` represents the module above the ECU PCB,
components facing up. Refit its 7-pin header and two-pin J3 underneath and use
mating sockets on the ECU. Choose socket height to clear solder joints and any
components below the module. The module's corner mounting holes are not drilled
through the ECU; their positions have not been measured.

Coordinates below are a top view, in mm, with VCC at (0, 0) and INT above it.
The header pitch is 2.54 mm. CAN L is pad 8; CAN H is pad 9, nearer the crystal.

| Pad | Signal | X | Y |
| --- | --- | ---: | ---: |
| 1 | VCC | 0 | 0 |
| 2 | GND | 0 | -2.54 |
| 3 | CS | 0 | -5.08 |
| 4 | SO | 0 | -7.62 |
| 5 | SI | 0 | -10.16 |
| 6 | SCK | 0 | -12.70 |
| 7 | INT | 0 | -15.24 |
| 8 | CAN L / J3 | 29 | -6.35 |
| 9 | CAN H / J3 | 29 | -8.89 |

The body measures 40 × 27 mm, extending from X = -1 to 39 mm. The main header
is centred vertically, giving 5.88 mm from each end pin to the corresponding
edge, consistent with the approximately 6 mm measurement. J3 uses the midpoints
between CS/SO and SO/SI. Its exact vertical alignment still needs the physical
fit check. Pads are 1.7 mm with 1 mm drills. The footprint reserves the module
outline and socket bodies in the courtyard; it contains no host-board edge cuts.

The older `MCP2515_CAN_Module_7P54` footprint has unverified generic geometry
and a 5.08 mm CAN pad pitch. Do not use it for this J3 connection. It remains
in the library because other, unreviewed projects still reference it.

### Aluminium resistor

`R_Aluminium_50W_50x26mm_Mount40x20mm_Wired` follows a 50 × 15 mm body with
two diagonal mounting ears, each 8.5 mm wide along the 50 mm axis. The ears
extend the overall height in the PCB plane to 26 mm. They are centred at
X = -20 mm on the lower side and X = +20 mm on the upper side. The screw
centres remain (-20, +10) and (+20, -10) mm, giving a 40 × 20 mm separation.

The footprint assumes M3 screws and 3.2 mm unplated clearance holes. Confirm
screw size against the part; its hole diameter was not supplied. Electrical pads
1 and 2 lie at (-34.5, 0) and (+34.5, 0) mm, giving a 69 mm centre pitch
matching the owner's revised terminal span. Each has a 5 mm pad and a
2 mm plated hole for a short wire. Verify wire size and strain
relief against the actual current and mounting arrangement.

A keepout follows the body and ears with a 0.5 mm margin on both outer copper
layers. The courtyard follows the same contour and adds narrow corridors for
the terminals and wire pads. The areas beside the ears and terminal corridors
remain available for other components. Heat removal, insulation, underside
clearances and the resistor's actual temperature remain part of physical
validation.
