# How the ECU controls the VP37 actuator

[Polski](CONTROL.pl.md) · [ECU setup](../README.md)

The controller positions the pump actuator. Driver demand passes through a ramp, whose output
is compared with Adjustometer feedback. A holding map supplies the initial drive; P, I and D
correct the position error. Current, supply and resistance compensation adjust that drive to
the electrical conditions.

## Reading the quantities

| Quantity | Meaning |
| --- | --- |
| Requested position, % | Share of the usable stroke. By default, 100% demand corresponds to 90% of calibrated travel; the physical travel limit (`X13`) changes that boundary. This is not a fuel measurement in mg/stroke. |
| Physical position, % | Share of the full travel between the calibrated endpoints. Maps and upper-stroke rules use this scale. |
| Position signal and error, Hz | Feedback from the Adjustometer frequency sensor. An error threshold in Hz is not the controller update frequency. |
| PWM, Hz | Power-stage switching frequency: 200 Hz. |
| PWM, counts | Drive in controller units; do not read it directly as duty percentage. |
| P / Kp | P is the current proportional contribution to drive; Kp is its gain. Likewise, I is the retained correction and Ki is its tuning value. |

The following sections explain the demand path, position holding and electrical corrections.
The [bench guide](BENCH.md) covers commands and recording; [implementation
notes](IMPLEMENTATION.md) cover cores, synchronization and memory.

## Position demand and input conditioning

VP37 takes a position request in one of two units:
`hal_status_t VP37_setPositionDemandPercentage(VP37Pump *pump, float percent)`
for a 0-100% demand across the usable stroke, and
`hal_status_t VP37_setPositionDemandValue(VP37Pump *pump, int32_t value)` for
raw feedback counts, bounded by `VP37_getPositionDemandMinValue()` and
`VP37_getPositionDemandMaxValue()`. The usable stroke ends at
`VP37_PHYSICAL_LIMIT_PERCENT` of the calibrated travel. This keeps the
actuator below the negative-stiffness steps of the upper stroke, at the cost
of the largest fuel quantities; 100 restores the full travel. The limit is a
share of the stroke, above zero and at most 100, checked at compile time and
by the bench knob `X13` alike. The integration dead zone widens steeply above
82% of the travel, so a full-demand hold settles short of the limit instead
of creeping up to it. Both write the same
demand, so driver input, engine control, and bench tests share one position
ramp, feedforward, and PID. The unit is the only difference: the controller
has no input-source mode, source-specific behaviour, filter-mode flag, or
alternate position path.
Emergency stop and calibration remain separate operations.

Analog input conditioning belongs to `sensors.c`. A 10 ms update preserves
fractional percentages, applies a 30 ms filter with a 0.1 percentage-point
deadband, and caches the result for `getDriverDemandPercent()`. Zero input
reaches the cache without filter delay. Both direct driver demand and
`engineOperation` read this output; the application selects which request to
send to VP37. Optional engine control is enabled by
`ECU_ENGINE_CONTROL_ENABLED` in `config.h`.

## Position control

### Following a changing demand

The first demand after start
ramps from the calibrated bottom, where calibration leaves the actuator.

VP37 uses 200 Hz PWM, shared with the Adjustometer drive-period filter. A lone rising demand step is approached as a standing
target from its first cycle: the ramp rises at 225%/s with 0.6 of the motion
assist and brakes before the target at 750%/s², so the actuator follows the
ramp closely and arrives slowly instead of overshooting and ringing. A target
that changes again within 25 ms is tracked at up to 300%/s without a brake;
its upward assist grows with the ramp rate up to 250%/s and stays there.
Descent always keeps 300%/s; a lone falling step keeps
the full assist for its first 25 ms and 0.3 of it afterwards. The
upper-stroke rules follow the physical position whatever the limit: the
upward-assist taper between 75% and 85%, the integral authority and dead-zone
tapers from 75%, the slower upper slews (275%/s, standing 187.5%/s) and the
optional settled-target damping (`H`, disabled by default). An explicitly
configured derivative reaches its full position weight between 85% and 90%,
engages after the target settles and fades through a 50 ms blend when movement
resumes. The holding map and supply/temperature compensation remain shared
across the stroke. Once the
demand settles, the climb floor follows negative learned integral trim, so
crossing below the target preserves the reduction needed to hold position.
Moving demands retain the original climb floor. Telemetry reports
`mode:position` and a separate `test:<name|none>` field; it does not identify a
potentiometer controller mode.

Upward motion assistance retains its full value through 75% of the calibrated
stroke, fades linearly to zero at 85%, and remains off above that point.
This limits acceleration into the sensitive upper region. The holding map,
downward assistance, demand ramp and position API retain their existing roles.

### Holding the upper stroke

At a fixed target above 85% of the calibrated stroke, valid feedback and active
current/resistance compensation qualify a holding profile. Its capture delay
decreases with the existing upper blend, from three seconds at 85% to zero
at 90%. Before capture, integration, dead zone and hold confirmation keep
their ordinary rules. Capture halves integration, narrows the dead zone to
±12 Hz and caps hold confirmation at 5 ms, confirmed on a control step. The
legacy mean-error entry gate and offset release are bypassed in that phase.
A large sustained error still releases the hold.

After three qualifying seconds, continuous integration stops and proportional gain Kp increases
in quiet intervals at least 1.5 seconds apart,
toward three times the configured gain at 90% stroke; its additional share
tapers to zero at 85%. Each increase is at most 0.01 PWM/Hz and transfers at
most 2.5 nominal PWM counts into I to preserve drive. The extra upper derivative
fades as holding stiffness rises. A moving target restores ordinary P,
including when the integral limit prevents full compensation of that transfer.

A quiet update either transfers a gain change or corrects the integral bias.
The paired gain transfer preserves drive; bias correction waits for a later
interval with unchanged gain. Quiet means a position variance at most 225 Hz
squared in the latest 0.3 to 0.6 seconds of feedback. Its statistics include the
current sample and refresh every control step. Updates are at least
1.5 seconds apart and use 5% of the configured Ki throughout the holding
range. Each bias step
is limited to three nominal PWM counts and uses the mean error outside
±12 Hz. Continuous integration remains held; these separate bias steps still
correct a settled offset within the integral and actuator limits. `uh`,
`rkp`, `ierr` and `I` report profile qualification, effective gain, continuous
integration input and retained bias. Research profiles keep their own rules.

### Holding the lower and middle stroke

Outside the upper holding profile, 100 ms within ±12 Hz at a settled target
freezes the integral to avoid
building force against static friction; the error averaged over 0.3 s must be
within ±12 Hz too, so a pass through the band during an arrival does not
freeze it. An error beyond ±40 Hz starts a 500 ms release timer. Returning to
±12 Hz cancels it; fluctuations around ±40 Hz do not restart it, so persistent
drift releases the integral. Below 75 % of the stroke, where the integral dead
zone has its base width, the hold engages only once the averaged error is
within ±6 Hz (bench knob `X8=<Hz>`, 1-12), and an averaged error outside
±12 Hz for 1 s releases it (bench knob `X6=<ms>`, 0 off).

After the position first comes within ±40 Hz of a settled target below 75 % of
the stroke, the thermal multiplier does not fall below the value the actuator
arrived with until the target moves (bench knob `X7=0|1`). A rising approach
raises the drive-resistance estimate, and its later relaxation would otherwise
pull the settled position below the target while the integral is held. Coil
heating raises the estimate afterwards, and the multiplier follows that rise.
While the multiplier is held, the current loop aims at the same excess over
the estimate, so it does not take the extra drive back (bench knob `X9=0|1`).
When the target moves, the multiplier returns to the estimate at once.
Above 75 % of the stroke both rules stay off: there they walked long holds
into stick-slip.

## Supply and resistance compensation

Fuel temperature is a signed measurement; a valid zero or negative reading
does not indicate a sensor failure. The existing control model uses valid
readings above 0 C and up to 120 C. Outside that range, or on a sensor fault,
it retains the last temperature factor (1 after startup) and the lower limit
of positive PID correction. The measured-resistance path and factor limits
remain unchanged. Supporting a negative reading in telemetry does not qualify
operation of a cold pump; that requires measurements on a cooled assembly.


Supply compensation scales the complete feedforward and PID command by `12 V / compensated
voltage`. ADC scan blocks arrive independently of the position-control step. Core 1 copies
them between position steps and tracks gate edges across block boundaries, including between
turbo, RPM and test processing while outside the deadline guard. Each ADC code is compensated
once on arrival (RP2040 correction table; RP2350 codes pass unchanged); the 5 ms control step
reduces each completed pulse once and refreshes the latest supply window at every step. The
frames provide a mean over the latest PWM-period window without waiting for a current edge.
While the mean is less than 20 ms old, a bounded voltage prediction accounts for its midpoint
age at the control step and half a PWM period for output latching. The voltage slope grows
through a 20 ms filter, stays within the latest measured slope, and drops promptly when the
ramp slows or reverses. The predicted offset is limited to ±0.5 V and affects only the
command scale. The local ADC fallback retains its 50 ms filter without prediction. Current
and resistance estimation use a separate voltage mean from the same PWM period as the current
capture.

### Learning coil resistance

Resistance estimation uses the same historical PWM-latch matching as the
current loop. Each command records whether position, delivered PWM and supply
have remained quiet for 150 ms after the demand ramp finishes. Learning requires
both that historical qualification and a still-quiet present state, so a delayed
observation from an approach cannot become eligible just by arriving later.
Those quiet captures alone build the first estimate. Once it is ready, matched
captures taken in motion keep updating it at the same 2 s rate: the observed
resistance rides the current lag behind the duty during a sweep, so the
estimate follows coil self-heating and at the same time acts as motion
feedforward. Slowing or capping that update raised the cyclic tracking error
on the bench.
The quiet window permits 60 Hz of position movement and 8 PWM counts from its
anchors; exceeding either restarts it. Each current period can update the
slow resistance estimate once. Its filter uses observation intervals, capped
at 50 ms after gaps. A cumulative supply
change over 0.1 V pauses estimation for 100 ms. Healthy new current captures
keep the last ready correction active during long supply changes; losing
those captures still expires it after 10 s. Supply-change detection uses the
measured voltage before prediction. Telemetry exposes `vage` (window midpoint
age at the control step, in microseconds), `vlead` (prediction offset in volts
before the 7 V floor), `vdot` (voltage slope in V/s), and `rhold` (resistance
estimation paused by supply or drive movement). `Robs` is the latest matched
resistance observation in ohms; `rmatch` reports a matched capture, `rquiet` its
historical qualification, `rlearn` an update in this control step, and `rlcnt`
the cumulative update count. Unmatched observations keep the last ready
estimate alive without changing its value. `scanus` measures ADC collection
and reduction time; `execus` measures execution of the complete control step.
Both durations are in microseconds and exclude the wait for the next step.
`late` is the largest start after a deadline among the last completed window
of 100 steps, in microseconds and without the steps a whole period late;
`ovr` counts those steps since the start. The guard of core 1 has to cover
the `late` values seen on the bench.

## Current feedback

A bounded proportional current loop corrects the position command before
the supply and thermal multipliers. Its target is the nominal feedforward
plus position-PID output expressed as guarded ON-phase current. The conversion
preserves the resistance estimator's raw-voltage reference and the local
divider calibration. The source shunt does not measure recirculation current
while the MOSFET is off, so this target is not full-period coil current.

The inverted drive conducts at the end of each hardware PWM period. Each
observation is matched to the command written before the preceding current
falling edge, where the PWM counter wraps and latches its compare register.
The ADC history retains 3.25 PWM periods. A current capture closes when its
falling edge is confirmed, without waiting for the next ON phase. Current,
duty and paired supply use the same fall-to-fall interval, whose duration does
not change when the duty changes.
Writes within 100 us of that edge are ambiguous and rejected, as are
duty mismatches, invalid captures and ON-midpoint ages of 25 ms or more.
The proportional gain is 0.35 in equivalent nominal-command units; correction
is limited to ±40 nominal PWM counts and changes at most 1000 counts/s.
Position-PID limits account for this correction before stepping the PID.
Repeated observations do not recompute the error; unavailable feedback slews
the correction to zero, and rest or stop clears it immediately. There is no
additional current integrator.

## Research control profile

An optional full-calibration profile is available for bench research. The normal startup and
tuning reset retain the default travel limit; building or uploading the firmware does not
enable the research profile.

Bench controls select the physical travel limit (`X13`), motion-dependent
integral hold (`X18`), reference weight (`X21`), upper integral deadband
(`X23`), ramp braking (`X24`) and descent assistance (`X25`, disabled by
default). The retained share of descent correction (`X26`) ranges from 0.25 to 0.75,
with a default of 0.4. It applies when descent assistance (`X25`) is enabled
and no learned holding correction is available for that target. These options use the shared position
demand path and remain subject to output and integral limits.

During a descending approach, assistance begins after a measured 0.3
percentage-point movement. It reduces excess negative integral toward a
holding correction and ends on the first integral-band entry or target
crossing. A previously measured correction can be reused for the same
interior target after two seconds of stable, unsaturated holding. Endpoints
cannot teach this correction. Stop, reset and accepted tuning changes
invalidate it. Unknown targets use the retained-fraction fallback.

Telemetry reports `ibias`, `ianchor`, `ipart` and `iphase` for the descent,
and `ihvalid`, `ihtar`, `ihbias` and `ireuse` for the learned holding correction.
The research profile requires separate hardware qualification before use as
normal operating defaults.

## Why arrival assistance is limited

These bench observations explain tuning choices. They are not acceptance criteria or a
guaranteed result for a new firmware image.

A tracked demand that stops at the top of the range (pedal on the floor)
arrives at the full rate. The upward motion assist follows the ramp rate up
to `VP37_PWM_FF_MOTION_RATE_CAP_PERCENT_PER_S` (250 %/s, bench knob `X5`)
and stays there for faster ramps: with the assist of the 300 %/s slew the
actuator led the ramp by 260-500 Hz into the top and rang when hot, while a
250 %/s ramp led by about 150 Hz and settled. With the Adjustometer's
drive-period filter the arrival overshoots by 100-200 Hz and stays below the
negative-stiffness steps. The optional brake into the top
(`VP37_TOP_ARRIVAL_DECEL_PERCENT_PER_S2`, bench knob `X2`) is off: at 750 %/s²
it slowed the last 70 % of the range and at 3000 %/s² it still delayed the
arrival by 100 ms and deepened the sag after it; slowing only the ramp widens
the actuator's lead instead of removing it.
