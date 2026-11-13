# VP37TestBench

[Polski](README.pl.md) · [Architecture](../../ARCHITECTURE.md)

This is a separate pump stand with its own RP2040 and display. The ECU task `VP37: Live
viewer` uses a different image: [the ECU BENCH variant](../ECU/README.md).

## Prepare and observe

Check the wiring in [device_tree.json](device_tree.json) and follow the [Adjustometer
procedure](../Adjustometer/README.md) for power and baseline acquisition. Open
`src/VP37TestBench` and use the [shared build guide](../../doc/BUILDING.md). Pump startup
includes calibration and can move the actuator.

The potentiometer sets demand. A green display dot confirms incoming Adjustometer frames;
blinking red means frames are missing. This module does not support SerialConfigurator.

## Hardware and execution

VP37TestBench runs the shared VP37 drive module (`src/common/vp37`) on the
pump test stand: its own RP2040 with an ST7796S display, a power stage of the
same type as the ECU one (parts and values from
[`fiesta-vp37-power-stage.json`](../../src/common/vp37/fiesta-vp37-power-stage.json),
pins from its own `device_tree.json`, read through
[`vp37_power_stage.h`](../../src/common/vp37/vp37_power_stage.h)) and the
Adjustometer on the same I²C protocol. The bench adapter
([`vp37_bench_adapter.c`](../../src/VP37TestBench/vp37_bench_adapter.c)) provides
the board services: the two PWM channels written active low like on the ECU,
the drive enable on a plain GPIO instead of the ECU's PCF8574, and the
Adjustometer transfer without a bus mutex, because the bench I²C has one
user. Like the ECU (through
[`ecu_hal_config.h`](../../src/ECU/ecu_hal_config.h)), it includes
[`vp37_hal_config.h`](../../src/common/vp37/vp37_hal_config.h) from its
`hal_project_config.h`: each I²C phase is bounded to 2 ms, so a hung bus
cannot hold a control step past the 20 ms feedback cutoff, and the module
stops the build when the bound is missing. The demand comes from a potentiometer on the auxiliary scan input.
Core 0 runs the timers, the display and debug output; core 1 starts the pump
and runs `VP37_process()`. The screen redraws a value only when it changes,
and a corner dot shows the Adjustometer connection the way Clocks shows its
CAN devices: steady green while frames flow, blinking red while they do not
(with the pump stopped, core 1 probes the status register once a second). Without a pump attached the start ends after the
baseline timeout and the bench keeps running, which is also its bring-up
smoke test. VP37TestBench does not take part in the configurator protocol.
