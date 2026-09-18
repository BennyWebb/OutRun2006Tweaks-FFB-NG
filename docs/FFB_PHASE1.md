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

Phase 2A uses only the physically validated relative `speed` candidate. No
lateral, drift, surface, collision or gear candidate is used to generate
force. `lateralCombined` and `driftCandidate` are explicitly derived heuristics; every value prefixed
`raw.` in the log is read directly from the documented candidate field. Raw
values are retained so their interpretation can be confirmed or rejected
through gameplay tests.

## Diagnostic log format

When `FFBDiagnosticLog=true`, three lines with the same `window` number are
written every 60 player-car simulation updates (approximately once per
second):

```text
FFB DIAG INPUT window=N samples=60 physicalSteer.cur=... physicalSteer.range=[min,max] steeringRate.filtered.cur=.../s steeringRate.filtered.range=[min,max]/s
FFB DIAG VEHICLE window=N inGameplay=... raw.speedCandidate=... raw.lateralA=... raw.lateralB=... derived.lateralSum=... derived.driftHeuristic=... raw.gearCandidate=...
FFB DIAG CONTACT window=N raw.wheelSurfaceMask=[0:0x...,1:0x...,2:0x...,3:0x...] raw.roadCollisionType=decimal(hex) raw.stateFlags=0x... raw.collisionByte=decimal(hex)
```

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
