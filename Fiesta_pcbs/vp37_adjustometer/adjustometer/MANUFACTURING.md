# Adjustometer fabrication specification

Generate a local `adjustometer-fabrication.zip` from the KiCad project using the
manufacturing-data list below. Generated archives are not stored in Git. Use that package for
bare-board fabrication. The PCB fabrication revision is `1.3-PCBWay`; the circuit and the
Rev1_3 module interface are unchanged. J1/J2 positions, mounting holes and the outline remain
compatible with the module footprint used by the ECU.

## Order settings

| Setting | Requirement |
| --- | --- |
| Board | Single rectangular PCB, 45.00 x 36.00 mm |
| Material | FR-4, two copper layers |
| Finished thickness | 1.60 mm, standard tolerance +/-10% |
| Outer copper | 1 oz, nominal 35 um per side |
| Surface finish | ENIG |
| Solder mask | Green, both sides |
| Silkscreen | White, both sides |
| Minimum track / copper spacing | 0.25 / 0.20 mm |
| Copper to routed board edge | At least 0.50 mm |
| Through vias | 0.60 mm pads, 0.30 mm finished holes; tented on both sides |
| Minimum annular ring | 0.15 mm at TO-92 pads and vias |
| Mounting holes | Four NPTH, finished diameter 3.20 mm |
| Outline tolerance | +/-0.20 mm |
| Finished hole tolerance | PTH +/-0.08 mm; NPTH +/-0.05 mm |
| Electrical test | Test all bare boards |

No controlled impedance, blind/buried vias, filled vias, slots or
castellations are required. The nominal stackup in the Gerber job file
describes the 1.60 mm board; the manufacturer may select its standard
FR-4 core for that finished thickness.

The four mounting-hole centres are 3.50 mm from their adjacent edges,
with 38.00 x 29.00 mm spacing. Preserve the copper-free screw-head area
of nominal radius 3.20 mm around each hole on both sides. These are unplated
holes without copper mounting pads. The Gerbers retain the polygonal
keepout boundaries from the PCB.

## Manufacturing data

- Eight Gerber X2 layers: front/back copper, solder mask and silkscreen,
  front paste and the board outline. The paste layer is optional stencil
  data; it is not a copper or solder-mask layer.
- Separate metric PTH and NPTH Excellon files, with PDF drill maps.
- A Gerber job file identifying the layers and stackup.
- A mechanical PDF at 1:1 scale. Edge.Cuts centre lines define the
  finished outline; Excellon files define finished hole sizes and centres.
- A schematic BOM for assembly reference. It is not a complete PCBA order:
  manufacturer part numbers and the header mounting variant need selection.
- This specification and `SHA256SUMS` for package integrity.

Use the same absolute origin for every layer and both drill files. Do not
mirror bottom Gerbers or drill files. Preserve the routed geometry, hole
centres and pin numbering; submit any proposed CAM changes for review.

The layout uses a 0.055 mm solder-mask expansion and a 0.11 mm minimum
mask web. The 1.27 mm-pitch TO-92 pads have 0.11 mm mask bridges.
Their copper pads are 1.05 x 1.50 mm with 0.75 mm finished holes; preserve
the hole sizes and centres during DFM. The 0.15 mm annular ring meets
PCBWay's [published minimum](https://www.pcbway.com/pcb_prototype/Annular_rings.html);
its normal PTH recommendation is 0.20 mm. Confirm these TO-92 pads in DFM.
The plotted apertures are authoritative. Silkscreen strokes and
spacing are at least 0.15 mm; text height is at least 0.80 mm.
These choices follow PCBWay's published
[capabilities](https://www.pcbway.com/capabilities.html),
[manufacturing tolerances](https://www.pcbway.com/pcb_prototype/PCB_Manufacturing_tolerances.html),
[silkscreen guidance](https://www.pcbway.com/pcb_prototype/PCB_Silkscreen.html)
and [solder-mask guidance](https://www.pcbway.com/pcb_prototype/PCB_Solder_mask.html).
Resolve the manufacturer's DFM comments against this specification before
releasing production. Fabrication review does not qualify the electrical
circuit or replace prototype testing.
