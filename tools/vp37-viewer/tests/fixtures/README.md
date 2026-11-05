# Viewer fixtures

These small, self-contained excerpts cover collector logs from revisions 82,
114, 148, 149, 154, 203 and 205. Tests read them without firmware, a device or
an external measurement archive. Gzip files contain one member and retain
their original telemetry bytes.

The `tree/` directory exercises metric binding and comparison pages. Absolute
paths in metadata use a fictional `/recordings/` root. One metrics file names
an absent log to check that proximity does not bind unrelated measurements.
The `2026-01-01-fixture-gate` iteration deliberately uses artificial metric
values so tests can distinguish recorded metrics from recalculated values.

`test_unknown_key_and_kind` adds an unfamiliar key and line kind in memory.
The binding test relocates a source directory and shifts an analysis window;
only the valid source and matching window may bind.
