# DTC catalogue

[Polski](README.pl.md)

`obd_dtc_catalog.h` lists the diagnostic trouble codes the Fiesta modules
raise or report, each with its two-byte SAE J2012 value and its description:
the generic powertrain codes P0xxx, P2xxx and P3xxx of SAE J2012 / ISO 15031-6,
the generic network codes the project reports, and the project's own codes in
the manufacturer range U19xx. The ECU names the codes it raises in
[`src/ECU/dtc_codes.h`](../../ECU/dtc_codes.h) as aliases of this catalogue,
and `dtcManagerGetName()` returns the catalogue description.

`obd_dtc_catalog.c` builds the sorted lookup table from the same lists.
`obdDtcDescription()` finds the description of a wire value and
`obdDtcFormatCode()` prints a code the way a scan tool shows it, `P0116` or
`U190C`. `obdDtcCatalogCount()` and `obdDtcCatalogEntry()` walk the table.

To add a code, put its `X(name, value, description)` row into the list that
owns its range, at its place in the ascending order. A project code belongs
to `OBD_DTC_FIESTA_CATALOG` and the U19xx range. The ECU host test
`test_obd_dtc_catalog` checks the order, the uniqueness and the aliases.

`obdDefinitions_sources.txt` is the translation-unit list read by the shared
CMake source loader. The firmware manifests of the modules the registry marks
with `"sharedSources": ["obdDefinitions"]` compile the same files.

The generic list was compiled against the rusEFI catalogue
(`firmware/controllers/algo/obd_error_codes.h` of
<https://github.com/rusefi/rusefi>, GPL-3.0) and completed from the standard
where that catalogue keeps a code without its text. The code numbers and
descriptions are the standard's; no rusEFI code is part of this directory.
