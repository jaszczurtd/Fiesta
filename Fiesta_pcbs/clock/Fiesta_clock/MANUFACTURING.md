# Fiesta_clock fabrication

Generate a local `Fiesta_clock-fabrication.zip` from the KiCad project using the contents
listed below. Generated archives are not stored in Git. The package contains the bare-board
manufacturing files for revision 1.0. Use the Gerber outline centreline for the finished
dimensions. Obtain the fabricator's DFM review before ordering.

| Parameter | Specification |
| --- | --- |
| Finished board | 70 × 70 mm, rectangular, single board |
| Layers | 2 copper layers |
| Material / thickness | FR-4, nominal 1.6 mm finished thickness |
| Copper | 1 oz, nominal 35 µm on each side |
| Surface finish | ENIG |
| Solder mask / legend | Green on both sides / white on both sides |
| Track widths | 0.30 mm signals; 0.60-0.80 mm supply paths |
| Minimum copper clearance | 0.25 mm |
| Copper to routed edge | At least 0.40 mm |
| Minimum plated drill | 0.30 mm, through vias only |
| Minimum via annular ring | 0.175 mm |
| Solder mask expansion / minimum web | 0.05 mm / 0.15 mm |
| Silkscreen | At least 1.0 mm text height and 0.15 mm stroke |
| Special processes | No blind/buried vias, controlled impedance or edge plating |

The two custom regulator contacts override the general mask expansion with
zero expansion. Preserve their exposed GND copper and their absence from
F.Paste. Tent ordinary vias on both sides; do not fill the two regulator
mounting holes or the four board mounting holes.

## Files to send

Upload the ZIP for bare PCB manufacture. It includes:

- F.Cu and B.Cu copper; F.Mask and B.Mask solder mask;
- F.Silkscreen and B.Silkscreen legends; Edge.Cuts outline;
- F.Paste stencil apertures for the SMD parts, if a stencil is ordered;
- Gerber job data and separate Excellon PTH/NPTH drill files;
- drill maps, drill report, these notes and SHA256SUMS.

All files share the absolute board origin. The Gerbers and drill files use
millimetres and are not manually mirrored. Read the Excellon header for the
coordinate format. Keep PTH and NPTH classification: H1-H4 are Ø3.2 mm
unplated, at (4,4), (66,4), (4,66), (66,66) mm in the top-view KiCad
coordinates. U4/U5 have separate Ø3.5 mm unplated mounting holes.

The schematic, BOM, component positions and assembly PDF remain beside the
project. They are useful for assembly, but ordering PCBA also requires
confirmed manufacturer part numbers, module/socket supply and assembly
instructions. The current BOM specifies values and footprints.

Before placing an order, compare the RTC header offset, actual JST XH
housings and enclosure holes with the assembly drawing. Perform the first
assembly as a prototype, including supply stability and thermal checks.
