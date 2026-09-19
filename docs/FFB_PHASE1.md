# Force-feedback telemetry foundation (Phase 1)

Phase 1 is the diagnostic telemetry foundation. Its output is a
`FFB::TelemetrySnapshot` used as the input boundary by the separately
documented Phase 2 output module.

## Steering source

With `UseNewInput=true`, steering is captured from the selected SDL Gamepad or
generic SDL Joystick steering profile (plus the shared keyboard profile), after
its device-specific steering deadzone but before OutRun's optional sensitivity
curve. With `UseNewInput=false`, the module calls OutRun's existing
`GetVolume(Steering)` function and normalizes its signed `-127..127` result.
Both paths report approximately `-1` at full left, `0` at centre and `+1` at
full right without changing the value delivered to the game.

Steering rate is the per-tick change multiplied by the fixed 60 Hz simulation
rate, then passed through a one-pole low-pass filter (`alpha=0.20`). A one-count
axis jitter deadband is applied and the filter snaps to zero below `0.001/s`
while the input is stationary.

## Candidate vehicle signals

The following offsets and the player-car control hook at executable offset
`0xA8330` were adapted from `d-b-c-e/OutRun2006Tweaks-FFB`. Names reflect only
the present confidence level:

| Snapshot field | Game source | Confidence |
| --- | --- | --- |
| `speed` | `EVWORK_CAR+0x1C4` | High for relative speed: physically validated against acceleration, cruising and stopping; units remain unknown |
| `lateralA/B` | `+0x264`, `+0x268` | Low/medium: correlated with slide in the reference fork; physical units unknown |
| `driftCandidate` | magnitude derived from the two lateral candidates | Low: diagnostic heuristic only |
| `surface[4]` | `+0x24C..0x258` | Medium: used as per-wheel surface masks by the game's vibration path |
| `roadCollisionType` | `OnRoadPlace+0x00` | Medium: used by the game's surface lookup; exact enum unknown |
| `gear` | `+0x208` | High: physically validated against the HUD; not used by the Phase 2A force model |
| `stateFlags`, `collisionByte` | `+0x08`, `+0x281` | Low: raw candidates logged for collision correlation |

The Phase 2 centering model uses the physically validated relative `speed`
candidate. Its optional drift-unload stage now consumes `driftCandidate` only
as an experimental attenuation input; surface, collision and gear candidates
remain excluded from force generation. `lateralCombined` and `driftCandidate` are explicitly derived heuristics; every value prefixed
`raw.` in the log is read directly from the documented candidate field. Raw
values are retained so their interpretation can be confirmed or rejected
through gameplay tests.

### Drift telemetry investigation

A controlled grip-versus-drift capture rejected `abs(lateralA + lateralB)` as
a reliable drift detector: it produced a large false positive in a normal
right-hand corner and frequently cancelled toward zero during a sustained
drift. The richer diagnostic line therefore records each field, their sum and
difference, magnitude relationships, signs, and unsmoothed derivatives without
selecting a replacement detector.

The strongest code-level evidence is the restored original Xbox vibration
routine in `hooks_forcefeedback.cpp`: it reads `EVWORK_CAR+0x264` and `+0x268`
independently and applies different sign tests. This supports treating them as
distinct directional lateral-related values, but does not establish whether
they represent front/rear slip, lateral velocity, acceleration, or another
quantity. The adjacent `+0x26C` field has no identified access or semantics in
the current, upstream, or reference source.

The relevant stock vibration branch first takes the maximum surface roughness
returned by the game's four wheel-surface lookups. Only when that value is
greater than `0.30` does it test the lateral fields: `lateralB < +0.10`, or,
if that is false, `lateralA > -0.10`. Its reconstructed extra motor contribution
is diagnostic-only:

```text
surfaceBase = maxSurfaceRoughness * speed * 0.10

scrubAmount = 0.10 - lateralB       when lateralB < +0.10
scrubAmount = lateralA + 0.10       otherwise when lateralA > -0.10
scrubAmount = 0                     otherwise

stockScrubCandidate = clamp(scrubAmount, 0, 2) * speed * 0.01
```

The branch adds this candidate to the surface base for the right-motor working
value and writes three times that result to the left-motor working value. Later
parts of the stock routine can further modify those channels, so the logged
values are branch contributions rather than reconstructed final motor output.
The surface gate and directional tests do not support interpreting either raw
field magnitude, their sum, or this branch alone as a drift detector.

The proposed countersteer candidate is also diagnostic-only. Its continuous
persistence timer resets immediately whenever the predicate becomes false:

```text
countersteerActive = abs(physicalSteer) >= 0.10 and
                     abs(lateralSum) >= 1.50 and
                     sign(physicalSteer) != sign(lateralSum)
```

`EVENT_DRIFT_ATTACK` is one entry in the game's global event-type enumeration,
adjacent to Race Attack and Time Attack. Source searches found no exposed
producer/consumer or related drift-start, drift-end, continuation, chain, or
score event identifiers. The available evidence identifies it as the Drift
Attack game-mode event slot, not a per-car indication that a drift has begun.
It is therefore not hooked or treated as telemetry.

### Candidate status

**Rejected as production drift detectors:**

- `abs(field_264 + field_268)`; grip false positives and drift cancellation.
- Steering/lateral-sum sign opposition, including persistence; a confirmed
  non-drift corner sustained it for 1.417 seconds, overlapping drift runs.

**Investigating through neutral diagnostics:**

- `EVWORK_CAR+0x1D0` and `+0x1D4`.
- `stateFlags` bit `0x00001000`.
- Nearby floats `+0x1C8`, `+0x1CC`, `+0x1DC`, `+0x1E0`, and `+0x26C`.
- The unavailable retail tyre-smoke/skid trigger and drift-scoring state.

**Confirmed from available code/xrefs:**

- `+0x1D0` and `+0x1D4` are 32-bit floats read by the restored stock
  vibration routine. No write xrefs or producer function are present in this
  source tree. The reference fork's physical log observed `abs(1D0) < 0.011`
  while `1D4` reached about `0.54`, disproving its earlier position/derivative
  naming rather than establishing replacement semantics.
- When `abs(1D0) > 0.0018`, stock vibration adds `speed * 0.25` to its
  left-motor working value for a two-frame hold if `1D0 >= 0 && 1D4 < 0`, or
  if `1D0 < 0 && 1D4 >= 0`. This is an opposing-sign-style test, but its
  physical meaning remains unknown.
- The stock vibration routine tests `stateFlags & 0x00001000`; when set, it
  adds `speed * 0.5` to the right-motor working value. Available source has no
  setter/clearer xref, so the bit cannot yet be named collision, skid, drift,
  or contact state. The reference fork's “contact event” label was an
  inference, not a recovered game symbol.
- No tyre-smoke particle trigger or drift-score accumulator is present in the
  available source. Tracing those producers requires the retail executable
  and disassembly/debug xrefs; this repository contains neither.

## Diagnostic log format

When `FFBDiagnosticLog=true`, five lines with the same `window` number are
written every 60 player-car simulation updates (approximately once per
second):

```text
FFB DIAG INPUT window=N samples=60 physicalSteer.cur=... physicalSteer.range=[min,max] steeringRate.filtered.cur=.../s steeringRate.filtered.range=[min,max]/s
FFB DIAG VEHICLE window=N inGameplay=... raw.speedCandidate=... raw.lateralA=... raw.lateralB=... derived.lateralSum=... derived.driftHeuristic=... raw.gearCandidate=...
FFB DRIFT DIAG window=N speed=... steer=... A=... B=... sum=... diff=... absA=... absB=... absSum=... absDiff=... maxAbs=... minAbs=... signSteer=... signA=... signB=... signSum=... A_rate=.../s B_rate=.../s sum_rate=.../s stockRoughness=... stockSurfaceVibration=... stockScrubCandidate=... countersteerActive=... countersteerDuration=...s
FFB FIELD DIAG window=N raw.field1D0=... range1D0=[min,max] raw.field1D4=... range1D4=[min,max] sum=... diff=... abs1D0=... abs1D4=... sign1D0=... sign1D4=... product=... rate1D0=.../s rangeRate1D0=[min,max]/s rate1D4=.../s rangeRate1D4=[min,max]/s nearby=[1C8:...,1CC:...,1DC:...,1E0:...,26C:...] range26C=[min,max] stateFlag1000=... stateFlag1000Samples=.../60
FFB DIAG CONTACT window=N raw.wheelSurfaceMask=[0:0x...,1:0x...,2:0x...,3:0x...] raw.roadCollisionType=decimal(hex) raw.stateFlags=0x... raw.collisionByte=decimal(hex)
```

The lateral rates are unsmoothed first differences multiplied by the fixed
60 Hz simulation rate. They are diagnostic observations only and do not feed
the force model. The `1D0`/`1D4` derivatives use the same unsmoothed method.
Signs are logged as `-1`, `0`, or `+1`; ranges and the `0x1000` set-sample
count cover every tick in the window rather than only its final sample.

Wheel surface slots remain numbered because their physical wheel ordering has
not been verified. Unknown masks and flags are printed in hexadecimal; the
road/collision byte candidates are also printed in decimal for easier change
comparison. Disabling and re-enabling diagnostics starts a fresh steering
range window, so the first window does not incorrectly assume the wheel passed
through zero.

## Diagnostic procedure

1. Set `FFBDiagnosticLog=true` in the `[FFB]` section, or enable it in the F11
   settings overlay. Restarting is not required.
2. Start a race, stop the car on a level road, and open
   `OutRun2006Tweaks.log`. Match the three lines for each sample using their
   shared `window` number.
3. Stationary steering test: centre and hold; turn approximately 90 degrees
   left and hold; return to centre; turn approximately 90 degrees right and
   hold; rapidly steer left/right; finally hold the wheel off-centre. Hold each
   static position for at least two windows. Check that `physicalSteer.cur` and
   its range follow the wheel, while filtered steering rate reacts during
   motion and decays approximately to zero while held.
4. Driving test: accelerate gradually through several gears, cruise steadily,
   then brake to a stop. Compare `raw.speedCandidate` and `raw.gearCandidate`
   against the speedometer, vehicle motion, and HUD gear indication.
5. Drive a sustained left bend and a sustained right bend. Compare the sign,
   magnitude, and repeatability of `raw.lateralA`, `raw.lateralB`, and the
   derived values. Attempt a deliberate drift if practical and safe, recording
   whether the same candidates distinguish it from ordinary cornering.
6. Drive from asphalt onto grass, sand, or another off-road surface and back.
   Record every `raw.wheelSurfaceMask` slot and `raw.roadCollisionType` before,
   during, and after the transition. Do not assign wheel positions or surface
   meanings until the changes repeat reliably.
7. Perform one light barrier scrape, allow values to settle, then one direct
   collision. Compare `raw.stateFlags`, `raw.collisionByte`,
   `raw.roadCollisionType`, lateral candidates, and speed around each event.
8. Repeat observations before treating any candidate as verified. Repeat with
   `UseNewInput=false` only if legacy input compatibility also needs testing;
   physical steering must still remain distinct from vehicle-state telemetry.
9. Disable `FFBDiagnosticLog` after capture to stop periodic logging.
