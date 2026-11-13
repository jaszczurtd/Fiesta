# Fiesta_clock

[Polski](README.pl.md) · [Architecture](../../ARCHITECTURE.md)

The module displays time, inside and outside temperatures, and supply voltage. It sends time
and temperatures to the other modules over CAN. The RTC keeps time without GPS.

## Prepare and start

Check the [board revision](../../Fiesta_pcbs/clock/README.md) and RTC type. Firmware defaults
to PCF8563; the new board has a DS3231 Mini and a different voltage divider. Select the
DS3231 variant and adapt the voltage conversion before assessing its readings. Use USB or
vehicle power, never both.

Open `src/Fiesta_clock` and follow the [build and upload guide](../../doc/BUILDING.md). Set
the time using the buttons or [SerialConfigurator](../SerialConfigurator/README.md). Check
the date, time and both sensors; a missing sensor does not mean 0°C.

## Local time and temperature readings

The display, the buttons and the configurator use CET/CEST, converted with
`hal_time`. Setting the time converts it to UTC. A local time skipped by the
spring change counts as CET; the repeated autumn hour counts as its first,
CEST pass. The weekday follows the date, so the buttons do not set it. The
configurator can read and set the date and time; a commit is rejected unless
the whole date is valid, leap years included.

The clock also sends the inside and outside air temperatures from its two
DS18B20 sensors as `CAN_ID_AIR_TEMP_UPDATE` (`0x132`), in 0.1 °C. The frame
goes out with the time once a second, and at once when a reading or its
validity changes. Both sensors keep converting in every display mode; a
12-bit conversion takes 750 ms. A sensor whose newest reading is 3 s old or
more loses its valid bit and sends 0. A sensor missing when the clock opens it
stays without a reading until the next opening, which the temperature screen
and an ignition change perform.

## GPS synchronization

The clock sets the RTC from the GPS UTC frame of Clocks (`0x131`), the only ID
its MCP2515 filters accept. The first valid frame sets an RTC without trusted
UTC, also one whose registers hold no valid date. A running RTC is corrected
only when it is more than 2 s off, at most once in 10 minutes; after an RTC
error, a failed read included, or a flash error the next attempt waits 30 s.
Every write is read back and checked. Frames read after a main-loop pause longer
than 500 ms, or while the buttons or the configurator edit the time, are
dropped. Without GPS or CAN the RTC keeps running on its own.

## UTC storage and recovery

Fiesta_clock keeps UTC in a PCF8563 or DS3231 and sends it once a second as
`CAN_ID_RTC_UPDATE` (`0x130`). The frame has integrity 1 only for trusted UTC;
otherwise its date and time bytes are zero. A marker in the HAL key-value store
records that this firmware has set the RTC to UTC. Until then the RTC holds the
local time written by older firmware: the display shows it unchanged, and the
frame reports no integrity. The marker is stored only after the RTC reads back
the new UTC. If the read-back or the marker fails and the flash answers that
the marker is missing, the clock writes the same moment back as local time, so
the display and the next start read it right. If the flash gives no answer,
the marker may be there: the RTC keeps UTC, shown right but not trusted, and
the clock confirms the marker every 30 s. A start that cannot read the flash
shows the RTC as UTC without trust, as most clocks have the marker by then.

## Display implementation

The module also shows the time, temperatures from two DS18B20 sensors, and the
supply voltage on a 128×32 SSD1306 OLED driven by JaszczurHAL. The OLED and RTC share the
I²C bus described in `device_tree.json`. HAL writes text directly to panel
RAM, without a framebuffer. The clock retains its original 6×8 font
(including Polish ś), scales 1 and 2, and panel settings, including contrast
0x3f. Idle mode suspends the panel while retaining controller RAM. An ignition
change reapplies the panel
configuration. The greeting is cleared on the panel after the initial
temperature readings.
