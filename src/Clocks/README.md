# Clocks

[Polski](README.pl.md) · [Architecture](../../ARCHITECTURE.md)

Clocks drives the instrument cluster and measures what the ECU does not read
itself. It produces square waves for the factory speedometer, tachometer, and
oil gauge, switches the oil and charge lamps, draws extra readouts on a TFT
display, and sounds the buzzer. It reads oil pressure from a resistive sender,
vehicle speed from the ABS pulse line, and exhaust gas temperature before and
inside the DPF through two MCP9600 amplifiers on its own I²C bus.

## Prepare and start

Check the [board and connections](../../Fiesta_pcbs/dashboard/Clocks/README.md). Use USB or
vehicle power, never both at once. Follow the [build guide](../../doc/BUILDING.md) with
`src/Clocks` as the project folder. After upload, check the display, lamps and data validity.
Missing ECU or GPS data does not prevent the display from starting.

## Read the lamps and display

The oil lamp is lit without oil pressure and blinks at 1 Hz while the sender
is shorted or open, when the oil gauge shows ERR. From power-up to the first
reading it stays lit.

The charge lamp follows engine RPM and battery voltage from the ECU. It stays
lit while the engine is stopped or those values are stale, and goes out once
the voltage confirms charging after the start. It lights again when charging
stops and blinks at 2 Hz on overvoltage.
The limits and confirmation times are in [`config.h`](../../src/Clocks/config.h);
the TFT voltage gauge keeps its own range.

Clocks expires RPM after 300 ms without a valid `0x127` frame, independently
of other ECU traffic. Core 0 stops the tachometer and publishes zero with
its RPM validity flag cleared. The TFT shows N/A until the next valid RPM
frame; a received zero remains a valid reading. Loss and recovery redraw
the RPM field on the next display loop pass, while regular numeric updates
remain once a second. Expiry also lights the charge lamp, which confirms
charging again when RPM returns.

The EGT readout on the TFT shows the temperature before the DPF. It takes
turns every 5 s with the temperature inside the DPF only while a connected
DPF module reports a regeneration in frame `0x124`.

The GPS icon on the TFT has a green dot with a fix and a red one blinking
once a second without it. The receiver sends NMEA also without a fix; when
no sentence with a valid checksum has arrived for 3 s, Clocks treats it as
missing. The speed then reads N/A, as for a missing thermocouple, and the dot
blinks twice as fast.

## Test the outputs

Connect [SerialConfigurator](../SerialConfigurator/README.md) and select the Clocks Tests tab.

The configurator can run five tests of the factory cluster: each lamp lit,
off and blinking, the tachometer and the speedometer stepped through fixed
values and back to zero, and the three buzzer signals. A step lasts 2 s
unless the configurator changes `step_ms`. A test replaces only the output
it drives. The measurements, the CAN frames and the other outputs carry on,
and the output returns to its measurement when the test ends, is stopped or
loses the configurator. Every Clocks image has the tests; the
[protocol description](../../src/common/scDefinitions/PROTOCOL.md#functional-tests)
lists their commands and status keys.

## Check the ABS input

The USB debug log reports the ABS pulse frequency once a window reaches
1000 ms, including zero and unchanged readings:
`ABS pulses: 1000.00 Hz, delta=1000, window_ms=1000, total=25000`.
Hz is the increase of `impulseCount` divided by the actual elapsed time;
a late loop pass lengthens the window. The counter records rising edges on
GP15, one per input period. Compare this reading with the ABS generator or
an external frequency measurement; pulses missed by the GPIO interrupt do
not reach the counter. The counting resolution is one pulse per window.
An open configurator session mutes the debug log.

## Data flow and timing

The two RP2040 cores split the work. Core 0 receives and sends CAN, measures
the ABS speed, and drives the gauges, lamps, and buzzer. Core 1 draws the TFT,
reads the oil sender and both thermocouples, and serves the configurator
session. The cores exchange copies of their state under a short lock. CAN
and TFT share SPI; display transfers release it between bounded pixel bands.
When the CAN controller does not answer at start, core 0 tries again every
second while the display and the gauges run.

ECU and Clocks use normal retransmission on the main CAN bus, so losing
arbitration does not discard an update. The HAL bounds each synchronous send
with a timeout and aborts a transmission that did not finish. ECU retains the
latest snapshot of each periodic status, throttle and turbo frame and sends
one pending frame per turn, at least 2 ms after its preceding transmission
attempt. The core-0 loop gives RPM priority. Sequence numbers follow the actual
send order; a newer snapshot replaces an unsent one of the same ID.
High-rate sensor reads and periodic status snapshots retain their configured
time grid after a late main-loop pass. Expired slots are skipped.

Clocks sends its display brightness, oil pressure, and ABS speed every 50 ms
and both exhaust temperatures every 250 ms. Each measurement carries a
validity flag. A missing thermocouple or a shorted or open oil sender goes
out with its flag cleared, so receivers do not read it as zero.

Core 1 polls GPS through PIO/software serial: receiver TX connects to GP27
and receiver RX to GP28, at 9600 baud. Clocks starts normally without the
receiver or a fix.

Coordinates remain `double` until conversion to microdegrees for `SC_GET_GPS`
and CAN. They survive loss of ECU communication. The ECU sends the old GPS
fields of frame `0x129` as zero, and Clocks does not read them. Polling runs every pass; GPS changes
and validity transitions trigger a new snapshot. Display refresh reuses
the converted speed.

The last position expires after 4 s. UTC comes from one RMC sentence with a
fix and expires after 2 s. Clocks keeps UTC for CAN and SC and publishes it
with seconds and validity in `0x131` every second, moved on by the age of the
sentence to the time of sending.

Once a second Clocks also sends the position with GPS speed (`0x12D`) and the
position with the date and time to the minute in UTC (`0x12E`). Its device
tree refers to the GPS binding in `src/common/bindings/`.

The CAN controller and the TFT share one SPI bus. The display writes in
bands of `HAL_DISPLAY_SPI_MAX_TRANSFER_BYTES`, set in
[`hal_project_config.h`](../../src/Clocks/hal_project_config.h), so CAN gets the
bus between two bands.

## Source map

| File | Responsibility |
|---|---|
| [`Cluster.cpp`](../../src/Clocks/Cluster.cpp) | square waves for the factory gauges |
| [`Gauge.h`](../../src/Clocks/Gauge.h), [`simpleGauge.cpp`](../../src/Clocks/simpleGauge.cpp), [`tempGauge.cpp`](../../src/Clocks/tempGauge.cpp), [`pressureGauge.cpp`](../../src/Clocks/pressureGauge.cpp) | gauge scaling |
| [`TFTExtension.cpp`](../../src/Clocks/TFTExtension.cpp) | TFT drawing |
| [`logic.cpp`](../../src/Clocks/logic.cpp) | start-up and loops of both cores; turns CAN signals into gauge, display, and buzzer state |
| [`clocksState.cpp`](../../src/Clocks/clocksState.cpp) | state copies passed between the cores |
| [`speed.cpp`](../../src/Clocks/speed.cpp) | ABS pulse frequency to speed |
| [`oilPressure.cpp`](../../src/Clocks/oilPressure.cpp) | ADC reading to bar, sender short and open detection |
| [`egt.cpp`](../../src/Clocks/egt.cpp) | both MCP9600 readings; a missing converter is looked for again |
| [`gps.cpp`](../../src/Clocks/gps.cpp) | local GPS, fresh UTC and GPS frame encoding |
| [`chargeLamp.cpp`](../../src/Clocks/chargeLamp.cpp) | charge lamp state from RPM and voltage |
| [`oilLamp.cpp`](../../src/Clocks/oilLamp.cpp), [`lampBlink.h`](../../src/Clocks/lampBlink.h) | oil lamp state from the oil pressure sender; blink phase of both lamps |
| [`buzzer.cpp`](../../src/Clocks/buzzer.cpp), [`buzzerStrategy.cpp`](../../src/Clocks/buzzerStrategy.cpp) | tones and warning patterns |
| [`alerts.cpp`](../../src/Clocks/alerts.cpp) | temperature warnings through the buzzer strategy; silent while a cluster test owns the buzzer |
| [`clusterOutput.cpp`](../../src/Clocks/clusterOutput.cpp) | the one `Cluster` instance; feeds it the measured speed and RPM, or the value of a running cluster test |
| [`testOutputs.cpp`](../../src/Clocks/testOutputs.cpp) | substitution of the gauge, lamp and buzzer outputs by the cluster tests; the measurements keep flowing underneath |
| [`clocksTests.cpp`](../../src/Clocks/clocksTests.cpp) | the five cluster tests (`charge`, `oil`, `rpm`, `speed`, `buzzer`) on the shared test runner, run through the configurator operations |
| [`can.cpp`](../../src/Clocks/can.cpp) | CAN reception and the brightness, oil and speed, EGT, GPS position and GPS UTC frames; the MCP2515 filters admit only Fiesta IDs |
