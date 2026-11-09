# ECU mechanical baseline

The owner accepted `eecv-mechanical-2026-11-09` as the reference for the physical ECU layout and mounting. The archive contains the original placement example, previews, project libraries and rusEFI licence. Its JSON manifest identifies the archive and every source file by SHA-256.

Extract the archive and open `layout_example/ECU-layout-example.kicad_pro` in KiCad 9. Standard KiCad footprints and models are required. Keep this snapshot unchanged when revising the full ECU board.

The baseline fixes the 140.80 × 164.42 mm outline, Ford connector, six exposed chassis GND contacts, approved TO-220 positions and the wired resistor footprint. It does not certify the electrical, thermal or manufacturing readiness of the complete ECU.
