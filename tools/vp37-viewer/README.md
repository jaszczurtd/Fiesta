# VP37 measurement viewer

[Polski](README.pl.md)

Build offline HTML pages from VP37 collector logs, recorded metrics and run
descriptions. Pages include individual recordings, iterations and comparisons.
They open through `file://` without a server or external chart libraries.

## Build and run

From the Fiesta root, with its pinned HAL dependencies installed:

```sh
cmake -S tools/vp37-viewer -B tools/vp37-viewer/.build -DCMAKE_BUILD_TYPE=Release
cmake --build tools/vp37-viewer/.build --parallel
ctest --test-dir tools/vp37-viewer/.build --output-on-failure
```

The executable is `tools/vp37-viewer/.build/vp37-viewer` (under the selected
configuration directory with a multi-configuration generator).

With a Visual Studio generator, configure with `-A x64` or `-A Win32`,
pass `--config Release` to the build command and `-C Release` to CTest.

```sh
vp37-viewer parse /path/to/recording.log.gz
vp37-viewer build --root /path/to/recordings --site /path/to/pages \
  --cache /path/to/cache --descriptions /path/to/descriptions
vp37-viewer describe --root /path/to/recordings --cache /path/to/cache \
  --descriptions /path/to/descriptions
```

All data and output directories are explicit. The recording root contains
`vp37_implementation_tests/`, `measurements/` or `archive/measurements/`.
Input recordings remain read-only. Use dedicated site and cache directories:
`build` replaces generated files and removes obsolete outputs there.
`describe` prints proposed descriptions; `--write` saves them and
`--overwrite` permits replacing existing descriptions.

The reader accepts plain `.log` and single-member `.log.gz` files. Extract
`.tar.gz` measurement archives before passing their root. Metric binding uses
source identity and analysis windows; moved roots are resolved by data-area
components rather than a particular workspace name.

## Parser and live-view preparation

`src/telemetry.h` exposes `TelemetryParser::feed()` and `finish()`. Chunks may
end anywhere in a line; only complete lines are delivered before `finish()`.
The limit is 64 KiB per line. Errors terminate that reader; discard its partial
output and create another reader after a file replacement or truncation.

The default time axis retains collector timestamps for compatibility with
recorded metric windows. The optional MCU mode unwraps `us` and places delayed
`T`, `IPULSE` and `IWAVE` samples relative to control records. Backward control
timestamps start a new epoch; consecutive control observations must be less
than 2^31 microseconds apart. A reset near counter rollover is ambiguous without
an explicit restart marker. Lines without `us` retain collector time.
Numeric fields from every `CFG` are retained separately from revision summaries.

`parse_log()` uses the same reader. The line splitter and timestamp arithmetic
are C11; the existing recording model and page generator remain C++17. The
parser version invalidates older caches when its output changes.

Live serving, monitor capture, VS Code integration and calibration records
(`CAL`) are not implemented yet. Existing unknown fields and line kinds are
reported by the viewer.

The viewer tests also run through the ECU host-test build, including its local
gate and CI. They use only the independent files in `tests/fixtures/`.
