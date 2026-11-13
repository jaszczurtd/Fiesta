# ECU: setup and first use

[Polski](README.pl.md) · [Project status](../../doc/STATUS.md)

The ECU controls the VP37 pump actuator, boost, glow plugs, fan, block heater
and heated windshield. It also stores DTCs and answers OBD-II requests as a
Ford Fiesta 1.8 DI EEC-V ECU. The project is a prototype; engine and VP37 work
uses a separate engine test stand.

Start here to run the existing bench setup and see its measurements. For the
control algorithm, read [how VP37 control works](guides/CONTROL.md).

## Before powering the bench

- Identify the board and wiring. The current `rp2350-plus` and `rp2040-plus`
  assemblies share the existing ECU wiring. The newer PCB 3 requires firmware
  changes to its I²C buses and output polarity; see [hardware compatibility](../../doc/STATUS.md#hardware-and-firmware).
- Prepare the checkout and toolchain using the [build guide](../../doc/BUILDING.md).
  Open **`src/ECU`** as the VS Code folder, then select the assembly that matches
  the installed processor board.
- Check the [Adjustometer power and startup procedure](../Adjustometer/README.md).
  It must establish its baseline with the actuator drive off and the mechanism
  settled. Restart the ECU after the baseline is ready. ECU startup calibrates
  the actuator and can move it through its travel.

## Open the live measurements

1. In VS Code, choose **Terminal -> Run Task -> `VP37: Live viewer`**.
   The task builds the viewer, rebuilds the ECU **BENCH** variant and uploads it
   to the selected assembly. It repeats the build and upload on each start.
2. Wait for the serial connection and the viewer URL in the terminal.
3. Open **<http://127.0.0.1:8037/live>** in a web browser, or run
   **`Open Integrated Browser`** in VS Code and enter that address.
4. Leave the task running. Check that the page is connected and new control
   samples arrive. The interface starts in English; the **EN / PL** button
   changes the language.

This task uses the ECU's BENCH variant. The separate
[VP37TestBench project](../VP37TestBench/README.md) has its own hardware and
is not the image used by this procedure.

The default ECU build forwards driver demand to the position controller.
The start/idle supervisor is optional and disabled by default
(`ECU_ENGINE_CONTROL_ENABLED=0`). A live position plot therefore does not
establish that the complete engine-control strategy is enabled or validated.

## Finish the session

Closing a browser tab only closes the view. **Ctrl+C** or **Terminate Task**
stops the server and serial capture; it does not stop the actuator.
Finish an active bench test through its controlling console or configurator
before closing that session. Console `stop` ends the test and returns its
demand to zero; `X` stops VP37 itself and requires an ECU restart to initialize
it again. After a test ends, the ordinary driver-demand path can resume. Use `X` when
you need VP37 itself to remain stopped. See the [session and recording rules](guides/BENCH.md).
Do not open a second serial owner while the viewer task owns the port.

## If no measurements appear

| What you see | What to check |
| --- | --- |
| The task fails before connecting | Build output, installed native compiler and selected firmware assembly. |
| Serial connection fails | Correct ECU USB device and another monitor or configurator holding its port. |
| The page cannot connect | The task is still running and the address matches the URL printed by that task. |
| Connected, but no new control samples | The terminal contains new `VP37 C` records and the flashed image is BENCH. A serial connection alone does not prove telemetry is arriving. |
| Invalid position or calibration | Adjustometer power, baseline and feedback; follow its startup procedure before restarting the ECU. |

For saved logs, command-line use, chart controls and capture errors, see the
[viewer guide](../../tools/vp37-viewer/README.md).

<a id="execution-and-synchronization"></a>
<a id="engine-speed"></a>
<a id="position-demand-and-input-conditioning"></a>
<a id="position-control"></a>
<a id="supply-and-resistance-compensation"></a>
<a id="current-feedback"></a>
<a id="telemetry"></a>
<a id="research-control-profile"></a>
<a id="source-map"></a>
<a id="bench-image-and-hardware"></a>
<a id="control-loop-ram-budget"></a>

## Continue by task

- [Understand demand, position and compensation](guides/CONTROL.md).
- [Use bench commands and record a complete run](guides/BENCH.md).
- [Change scheduling, buffers or source code](guides/IMPLEMENTATION.md).
- [Build, upload or debug without the viewer](../../doc/BUILDING.md).
- [Run host tests](../../doc/TESTING.md) or read the [MISRA scope](../../MISRA.md).
