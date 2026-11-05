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

## Serve a growing log

```sh
vp37-viewer serve --log /path/to/collector.log --site /path/to/pages
```

The server reads the log from its beginning, follows appended bytes, and serves
previously generated offline pages through HTTP. Both listeners bind only to
`127.0.0.1`: HTTP uses port 8037 and WebSocket uses 8038. Change them with
`--port` and `--ws-port`. Stop with Ctrl+C. The source log is read-only; the
server neither opens the serial port nor creates a missing log.

`--site` is optional. With it, `/` serves the generated pages; without it,
`/` returns server status. `/api/status` always returns status JSON, and
`/assets/` serves the bundled web assets. Static files must be smaller than
512 KiB; files at or above this limit return HTTP 413. Paths outside the selected roots are
rejected. HTTP and WebSocket each allow four clients.

The WebSocket endpoint is `ws://127.0.0.1:8038/ws`. Protocol version 1 sends:

| Message | Meaning |
| --- | --- |
| `hello` | `protocol`, current `epoch`, next batch number `next`, log `present` and `ws_port`. |
| `reset` | The same fields after the log appears, disappears, is truncated or replaced. Discard the previous epoch's display buffers. |
| `samples` | `epoch`, increasing `seq` and `records`. Each record has `kind`, `t` in seconds, and numeric `values` or event `text`. Missing/non-finite numbers are JSON `null`. |

Batches normally leave every 40 ms, or earlier when they reach 32 KiB. Records
use MCU time where available, with the mapping described below. `HOST`, `TEST`
and other VP37 messages, including trace completion, retain their text. This
does not add interpretation of calibration records.

A connection receives subsequent batches, without replaying earlier history.
Batch numbers expose gaps. On reconnect, start from `hello`; a stopped server
starts a new stream. A client that cannot accept a complete frame is disconnected
without queuing or retrying a partial frame. Client data messages are rejected;
the endpoint only publishes measurements.

The reader preserves incomplete lines at EOF and discards them on a detected
file change. Detection uses file identity, size and the last 64 consumed bytes.
An in-place rewrite that restores the same bytes and size between polls cannot
be distinguished from append-only input. Use atomic replacement for log rotation.
Each poll reads at most 64 KiB. Lines are limited to 64 KiB, numeric records to
256 distinct fields, and encoded records to 32512 bytes. Exceeding a limit or
encountering a read error terminates the command with an error. A missing file
is waited for. Parser history and client queues do not grow with log length.

Live charts and VS Code integration are separate from this transport. Existing
offline pages continue to work through `file://`.

## Parser

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

The callback constructor streams one record at a time without retaining a
recording. Its borrowed values are valid only during the callback; an error
returned by the callback terminates the reader. Offline parsing is unchanged.
Monitor capture, live charts, VS Code integration and interpretation of
calibration records (`CAL`) are not implemented yet. Existing unknown fields
and line kinds are reported by the offline viewer.

The viewer tests also run through the ECU host-test build, including its local
gate and CI. They use only the independent files in `tests/fixtures/`.
