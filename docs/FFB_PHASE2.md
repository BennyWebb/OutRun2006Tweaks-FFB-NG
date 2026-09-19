# Force feedback Phase 2

## Phase 2A scope

Phase 2A adds the smallest useful output model: one persistent standard
DirectInput constant-force effect on the selected generic SDL Joystick. It is
off by default. Mapped SDL Gamepads retain their existing vibration behavior
and are not treated as steering-wheel FFB devices.

SDL's Windows DirectInput joystick backend already owns the selected device's
exclusive `IDirectInputDevice8` handle. `SDL_OpenHapticFromJoystick` safely
shares that same native handle instead of opening the wheel twice. On this
backend, SDL creates exactly one `GUID_ConstantForce` `DIEFFECT`; the effect is
created once with a 32,767 ms duration, explicitly started with
`SDL_RunHapticEffect(..., SDL_HAPTIC_INFINITY)` for infinite repetition, then
updated in place. SDL's
DirectInput update path calls `IDirectInputEffect::SetParameters` without a
restart flag, so normal magnitude updates leave the same effect running. This remains
standard DirectInput output and contains no Logitech protocol or HID commands.

## Shaped centering force

Only the physically validated `physicalSteer` and relative `speedCandidate`
signals are consumed. The original linear steering term produced useful
centering, but small offsets near centre were sometimes too weak to return the
wheel reliably while larger cornering angles felt unnecessarily strong. The
steering term is now shaped before the existing speed and strength scaling:

```text
absSteer = abs(physicalSteer)

if absSteer <= centreDeadZone:
    shapedSteer = 0
else:
    normalized = clamp((absSteer - centreDeadZone) /
                       (1 - centreDeadZone), 0, 1)
    shapedMagnitude = pow(normalized, curveExponent)
    shapedSteer = sign(physicalSteer) * shapedMagnitude

normalizedSpeed = 0                                      when speed <= minSpeed
normalizedSpeed = 1                                      when speed >= fullStrengthSpeed
normalizedSpeed = clamp((speed - minSpeed) /
                        (fullStrengthSpeed - minSpeed),
                        0, 1)                            otherwise
movementRamp = clamp(speed / lowSpeedRampSpeed, 0, 1)
curvedSpeed = pow(normalizedSpeed, speedCurveExponent)
effectiveHighSpeedScale = max(highSpeedScale, lowSpeedScale)
targetSpeedScale = lowSpeedScale +
                   (effectiveHighSpeedScale - lowSpeedScale) * curvedSpeed
speedScale = movementRamp * targetSpeedScale
requestedForce = clamp(shapedSteer * speedScale * strength, -1, 1)
diMagnitude = round(requestedForce * 10000)
```

The centre deadzone exists only to suppress force chatter or hunting at exact
centre; its default `0.01` is intentionally small and is separate from input
steering deadzones. An exponent below `1.0` raises the relative force for small
and medium displacements without changing the full-scale maximum. `1.0` is
linear outside the centre deadzone, while values above `1.0` soften the force
near centre. With the default exponent `0.60`, normalized values map
approximately as follows:

| Normalized steering | Shaped magnitude |
| ---: | ---: |
| 0.05 | 0.17 |
| 0.10 | 0.25 |
| 0.25 | 0.44 |
| 0.50 | 0.66 |
| 1.00 | 1.00 |

Physical testing found that mapping speed from zero to the full configured
strength could not satisfy both ends of the range: increasing `FFBStrength` to
improve low-speed return made high-speed steering too heavy. Normalized speed
is therefore mapped between independent low- and high-speed scale values. Both
are multipliers of `FFBStrength`, so the high-speed response no longer has to
reach the global strength maximum.

`FFBMinSpeed` defaults to zero, removing the earlier artificial no-force
region. An exponent below `1.0` raises low- and mid-speed progression, `1.0`
is linear, and values above `1.0` reduce progression below full-strength
speed. With the default exponent `0.50`, normalized speed maps approximately
as follows before the configured floor and ceiling are applied:

| Normalized speed | Speed scale |
| ---: | ---: |
| 0.05 | 0.22 |
| 0.10 | 0.32 |
| 0.25 | 0.50 |
| 0.50 | 0.71 |
| 0.75 | 0.87 |
| 1.00 | 1.00 |

The low-speed floor is not applied at full strength while stationary. A short
`movementRamp` rises smoothly from zero to one over
`FFBLowSpeedRampSpeed`, making force zero at genuine standstill and bringing
the useful low-speed scale in quickly as the car moves. Once that ramp is
complete, the defaults map curved speed from `0.30` at the low end to `0.65`
at the high end. If the configured high scale is below the low scale, the
effective high scale is clamped up to the low scale.

With the default `FFBStrength=0.75`, this gives a maximum steering multiplier
of `0.225` at the low-speed floor and `0.4875` at the high-speed ceiling. The
shaped steering value still determines how much of that available multiplier
is requested at a particular wheel angle.

| Raw speed candidate | Normalized speed | Movement ramp | Final speed scale | Maximum after strength |
| ---: | ---: | ---: | ---: | ---: |
| 0.000 | 0.000 | 0.00 | 0.000 | 0.0000 |
| 0.025 | 0.017 | 0.25 | 0.086 | 0.0647 |
| 0.050 | 0.033 | 0.50 | 0.182 | 0.1365 |
| 0.100 | 0.067 | 1.00 | 0.390 | 0.2928 |
| 0.375 | 0.250 | 1.00 | 0.475 | 0.3563 |
| 0.750 | 0.500 | 1.00 | 0.547 | 0.4106 |
| 1.500 | 1.000 | 1.00 | 0.650 | 0.4875 |

If `fullStrengthSpeed <= minSpeed`, normalized speed changes directly to 1
above the minimum rather than dividing by zero. Physical testing with the G27-tool
DirectInput driver established that a positive signed magnitude produces the
physical force needed to oppose positive (right) steering, while a negative
magnitude opposes negative (left) steering. The model therefore preserves the
`physicalSteer` sign: holding right requests a positive centering pull, and
holding left requests a negative centering pull. The signed DirectInput
magnitude is always clamped to `-10000..10000`.

## Experimental drift unloading

The optional drift stage attenuates the existing centering calculation; it does
not add a drift, countersteer, spring, or periodic force. Its raw input is the
Phase 1 `driftCandidate`, derived from two reverse-engineered lateral candidates:

```text
lateralCombined = field_264 + field_268
driftSignal = clamp((abs(lateralCombined) - 12) / 12, 0, 1)
```

The current and reference game structures contain no identified direct drift
flag, powerslide state, slip angle, or vehicle-heading-versus-velocity signal.
The reference FFB implementation uses these same lateral fields and thresholds;
that is supporting evidence, not proof that they are a definitive game drift
state. Ordinary high-lateral-load cornering may still be a false positive, so
the signal remains experimental pending controlled physical comparison.

The configurable thresholds remap the raw signal into a continuous factor:

```text
driftFactor = 0                                      when signal <= driftStart
driftFactor = 1                                      when signal >= driftFull
driftFactor = clamp((signal - driftStart) /
                    (driftFull - driftStart), 0, 1) otherwise
```

The effective `driftFull` is clamped no lower than `driftStart`. The factor is
then passed through a one-pole exponential filter on the fixed 60 Hz simulation
tick. Separate attack and release time constants are expressed in seconds:

```text
tau = attackSeconds when driftFactor is rising, otherwise releaseSeconds
alpha = 1 - exp(-(1 / 60) / tau)
smoothed += alpha * (driftFactor - smoothed)

driftResistanceScale = lerp(1, driftMinResistance, smoothed)
requestedForce = clamp(shapedSteer * speedScale * strength *
                       driftResistanceScale, -1, 1)
```

The shorter default attack unloads the wheel promptly as the signal rises; the
longer release restores normal centering more gradually. With full drift unload,
the default retains 40% of normal centering rather than making the wheel limp.
Disabling `FFBDriftUnload` holds the resistance scale at `1.0`.

## Settings

All settings are in `[FFB]` and are live-editable in the configuration overlay:

| Setting | Default | Overlay range | Meaning |
| --- | ---: | ---: | --- |
| `FFBEnable` | `false` | off/on | Enables Phase 2A output |
| `FFBStrength` | `0.75` | `0.00..1.00` | Global normalized force multiplier |
| `FFBMinSpeed` | `0.00` | `0.00..5.00` | Speed where normalization begins |
| `FFBFullStrengthSpeed` | `1.50` | `0.00..5.00` | Speed where normalized speed reaches 1 |
| `FFBSpeedCurveExponent` | `0.50` | `0.20..2.00` | Normalized speed-to-force curve shape |
| `FFBLowSpeedRampSpeed` | `0.10` | `0.01..0.50` | Speed where the standstill movement ramp reaches 1 |
| `FFBLowSpeedScale` | `0.30` | `0.00..1.00` | Low-speed multiplier of `FFBStrength` |
| `FFBHighSpeedScale` | `0.65` | `0.00..1.00` | High-speed multiplier of `FFBStrength`; clamped no lower than the low scale |
| `FFBCentreDeadZone` | `0.01` | `0.00..0.10` | Small force-only deadzone around exact centre |
| `FFBCentreCurveExponent` | `0.60` | `0.20..2.00` | Steering-to-force curve shape |
| `FFBDriftUnload` | `true` | off/on | Enables experimental attenuation from the drift heuristic |
| `FFBDriftStart` | `0.20` | `0.00..1.00` | Raw heuristic where unloading begins |
| `FFBDriftFull` | `0.80` | `0.00..1.00` | Raw heuristic where unloading reaches maximum; clamped no lower than start |
| `FFBDriftAttack` | `0.15` | `0.01..2.00` seconds | Rising-factor smoothing time constant |
| `FFBDriftRelease` | `0.35` | `0.01..2.00` seconds | Falling-factor smoothing time constant |
| `FFBDriftMinResistance` | `0.40` | `0.00..1.00` | Normal centering retained at full unload |
| `FFBDiagnosticLog` | `false` | off/on | Enables one-second input, telemetry and output logs |

## Safety and lifetime

Output is set to zero when FFB is disabled, the selected device is not a
generic joystick, the movement ramp is at standstill, the active race ends, the game
is paused, or telemetry has not updated for 250 ms. Goal, time-up, retry and
results states do not count as active racing. A broad simulation-tick
watchdog handles menus where the player-car telemetry hook no longer runs.
`WM_CLOSE`/`WM_DESTROY` also zero the effect while normal runtime services are
still valid.

Hot-unplug closes the haptic/effect handle before SDL closes the joystick. No
SDL or DirectInput cleanup is performed by a static destructor or from
`DllMain`; handles still open during process teardown are left for the OS to
reclaim, avoiding DLL shutdown ordering hazards.

With diagnostics enabled, the output module writes approximately once per
second:

```text
FFB OUTPUT window=N enabled=... physicalSteer=... centreDeadZone=... curveExponent=... shapedSteer=... speedCandidate=... normalizedSpeed=... movementRamp=... speedCurveExponent=... curvedSpeed=... lowSpeedScale=... highSpeedScale=... targetSpeedScale=... speedScale=... rawDriftSignal=... driftStart=... driftFull=... driftFactor=... smoothedDriftFactor=... driftResistanceScale=... requestedForce=... diMagnitude=... hapticOpened=... effectCreated=... effectRunning=... statusSupported=... lastOpen='...' lastCreate='...' lastUpdate='...' lastRun='...'
```

With diagnostics enabled, hysteretic transition messages are also emitted when
the smoothed factor crosses `0.10` entering and `0.05` exiting:

```text
FFB DRIFT: entered rawSignal=... rawFactor=... smoothedFactor=... resistanceScale=...
FFB DRIFT: exited rawSignal=... rawFactor=... smoothedFactor=... resistanceScale=...
```

Opening is retried after five seconds when SDL initially cannot match or query
the joystick through its haptic device list. SDL's public wrapper can replace
the lower-level DirectInput failure with the generic
`SDL_SYS_HapticOpenFromJoystick failed` message, so that message alone does not
identify the underlying HRESULT. The five-second delay is the module's bounded
retry interval rather than time spent inside the open call.

## Explicitly not implemented

Phase 2 has no steering-rate damping, spring or damper condition effect,
periodic/sine effect, collision impulse, road texture, surface response,
separate drift/countersteer force, gear response, Logitech-specific code,
native HID output, or custom wheel command. Drift telemetry only attenuates the
same constant-force centering output and remains subject to physical validation.

Physical testing also found that heavy normal high-speed centering could mask
the game's approximately 270-degree steering boundary. Lowering the ordinary
high-speed ceiling preserves force headroom for a separately identifiable
soft-lock cue in a future milestone. This phase does not detect, modify, or
generate force for that boundary.

## SDL DirectInput duration handling

SDL 3.4.12 documents `SDL_HAPTIC_INFINITY` (`0xFFFFFFFF`) as a valid effect
length, but its Windows DirectInput conversion does not special-case that
value. It calculates `dwDuration = length * 1000` in 32-bit arithmetic. The
previous effect therefore supplied `4,294,966,296` microseconds after overflow,
not DirectInput's `INFINITE` (`0xFFFFFFFF`) sentinel. The G27 device stack was
observed to stop that effect after approximately ten seconds, consistent with
a device/driver duration cap rather than SDL's requested repeat count keeping
the malformed duration alive.

Phase 2A now uses SDL's documented maximum ordinary length of 32,767 ms, which
converts exactly to a DirectInput duration of `32,767,000` microseconds, and
requests infinite repetitions separately. When effect-status reporting is
available, a non-zero force request checks the actual status. If the effect has
stopped, the existing effect is started again; zero-force stationary/menu
states do not cause repeated restarts.
