# VP37 measurement viewer

[Polski](README.pl.md)

The viewer displays live VP37 telemetry and builds pages for saved measurements and
comparisons. Offline pages work through `file://`, without a server or external chart
libraries.

- **My ECU is connected over USB:** follow the VS Code steps below.
- **I have saved recordings:** use the [offline build workflow](#build-and-use-offline-pages).
- **Another program is already recording a log:** [serve that growing log](#serve-a-growing-log).

## Live ECU measurements in VS Code

Prepare the hardware using the [ECU guide](../../src/ECU/README.md). The uploaded firmware
calibrates the actuator during startup.

1. Open **`src/ECU`** as the folder. You need CMake and a native C/C++ compiler; on Windows use MSVC Build Tools.
2. Run **Tasks: Run Task -> `VP37: Live viewer`**. It builds the viewer, then rebuilds and uploads **ECU BENCH** for the selected assembly.
3. After `Connected to ...`, read the URL in the terminal. Its default is **<http://127.0.0.1:8037/live>**.
4. Open that address in a web browser or through **Open Integrated Browser** in VS Code. Leave the task running.

The recording goes to `src/ECU/.build/vp37.log`. A later start appends a session and repeats
the build/upload. The view shows new samples; a connection does not replay earlier history.

**Ctrl+C** or **Tasks: Terminate Task** ends capture and the server. Closing the tab only
closes the view. Neither action commands the actuator to stop; the ECU guide explains how to
end a test.

## Live view

Live and offline pages default to English. The **EN -> PL / PL -> EN** button
switches language and remembers the choice when browser storage is available.
Links also carry the language choice, including when storage is blocked.
Switching a live view preserves reception, pause, zoom and the selected trace.
Offline pages reload in the selected language.

Each chart has a short explanation and a color legend beside it, or above it
in a narrow window. Hover over a parameter name to see its log key, unit and
description; focus it with Tab or click to expand the description. Incoming
samples keep these descriptions open. Raw sensor frequency has its own chart,
as does PID correction, so their scales do not obscure position or PWM output.
CSV keeps the original field identifiers. PNG uses the selected language and
wraps the legend labels. Recorded reports and log messages retain their source
text. Adjustometer supply readings use decivolts (`dV`); PWM counts are not
percentages.

Panels show position and error, PWM and PID terms, control state bands,
current, voltage and temperature, loop timings, and Adjustometer values and
status bits. Units and descriptions come from `field_catalog()`; legends mark
missing fields. Hover for the shared cursor, drag for shared zoom, and
double-click to restore the retained range. Offline pages and their controls
still work through `file://`.

The rolling window spans 60 seconds. Pause and zoom freeze one snapshot while
reception continues; Resume returns to the newest samples. Reception redraws
at most ten times per second, with no drawing in hidden tabs. CSV exports
numeric records and events in the viewed range, including source time and
segment identifiers. CSV prefixes text cells starting with a spreadsheet
formula marker with an apostrophe. PNG captures the displayed panels and range.
Returning with the browser's Back button reconnects reception and preserves
the cached view, including a paused range.

Reconnect, file changes, sequence gaps and MCU clock resets separate segments.
Their boundaries are marked; curves do not bridge them, missing values or
sample intervals longer than two seconds. Live time starts at zero; a new
segment starts 0.1 seconds after the previous one. This visual separator does
not measure outage duration. CSV retains original parser timestamps.
Reconnect never recovers missed samples, even if the server reuses an epoch.

`T` samples are collected separately. `trace end samples:N` opens a trace view
unless another range is paused. Select retained traces from the dropdown.
Trace axes show seconds from the first MCU sample; CSV also retains parser
timestamps. A successful `trace: HAL_OK samples:N` acknowledgement closes any
unfinished previous dump as incomplete. A count mismatch, interruption,
missing `us`, non-increasing sample time or capacity overflow marks a dump
incomplete. A selected trace stays available while newer dumps arrive.

## Build and use offline pages

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
Tests require Chrome, Chromium or Edge, detected by CMake or selected with
`-DVP37V_BROWSER=/path/to/browser`. A C harness runs the production scripts
in headless mode, without Node or npm. `-DBUILD_TESTING=OFF` builds only the
tool and requires no browser.

Use dedicated output directories: `build` replaces generated files and removes obsolete
outputs in the site and cache directories. Input recordings remain read-only.

```sh
vp37-viewer parse /path/to/recording.log.gz
vp37-viewer build --root /path/to/recordings --site /path/to/pages \
  --cache /path/to/cache --descriptions /path/to/descriptions
vp37-viewer describe --root /path/to/recordings --cache /path/to/cache \
  --descriptions /path/to/descriptions
```

All data and output directories are explicit. The recording root contains
`vp37_implementation_tests/`, `measurements/` or `archive/measurements/`.
`describe` prints proposed descriptions; `--write` saves them and
`--overwrite` permits replacing existing descriptions.

The reader accepts plain `.log` and single-member `.log.gz` files. Extract
`.tar.gz` measurement archives before passing their root. Metric binding uses
source identity and analysis windows; moved roots are resolved by data-area
components rather than a particular workspace name.

## Serve a growing log

```sh
vp37-viewer serve --log /path/to/collector.log --site /path/to/pages
```

The server reads the log from its beginning, follows appended bytes, and serves
previously generated offline pages through HTTP. Both listeners bind only to
`127.0.0.1`: HTTP uses port 8037 and WebSocket uses 8038. Change them with
`--port` and `--ws-port`. Stop with Ctrl+C. The source log is read-only; the
server neither opens the serial port nor creates a missing log.

Open `http://127.0.0.1:8037/live` for the oscilloscope. `--site` is optional:
with it, `/` serves generated pages; without it, `/` opens the oscilloscope.

## Configuration and limits

The [technical reference](REFERENCE.md) covers log paths and ports, keyboard shortcuts,
capture rules, buffer limits, the WebSocket protocol and parser.
