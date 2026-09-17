# Force-feedback telemetry foundation (Phase 1)

Phase 1 is diagnostic only. It creates no DirectInput effects and sends no
commands to a wheel motor. Its output is a `FFB::TelemetrySnapshot` intended to
be the input boundary for a later force model.

## Steering source

With `UseNewInput=true`, steering is captured from the active SDL steering
binding after `SteeringDeadZone` but before OutRun's optional sensitivity
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
| `speed` | `EVWORK_CAR+0x1C4` | Medium: reference fork behavior; scale remains normalized/unknown |
| `lateralA/B` | `+0x264`, `+0x268` | Low/medium: correlated with slide in the reference fork; physical units unknown |
| `driftCandidate` | magnitude derived from the two lateral candidates | Low: diagnostic heuristic only |
| `surface[4]` | `+0x24C..0x258` | Medium: used as per-wheel surface masks by the game's vibration path |
| `roadCollisionType` | `OnRoadPlace+0x00` | Medium: used by the game's surface lookup; exact enum unknown |
| `gear` | `+0x208` | Medium/high: reference fork observation, easy to verify against HUD |
| `stateFlags`, `collisionByte` | `+0x08`, `+0x281` | Low: raw candidates logged for collision correlation |

No candidate is used to generate force. Raw values are retained in logs so
their interpretation can be confirmed or rejected through gameplay tests.

## Diagnostic procedure

1. Set `FFBDiagnosticLog=true` in the `[FFB]` section, or enable it in the F11
   settings overlay. Restarting is not required.
2. Start a race and open `OutRun2006Tweaks.log`. A line beginning `FFB DIAG`
   is written once per 60 simulation ticks (approximately once per second).
   `steerRange` and `rateRange` retain movement that occurred between lines.
3. While stopped, move the wheel to full left, centre and full right, holding
   each position for at least two log lines. Confirm `physicalSteer` approaches
   `-1`, `0`, `+1`; confirm `steerRate` changes with motion and decays to zero
   while held.
4. Accelerate through several gears and compare `speed` and `gear` with the
   HUD. Brake to a stop and confirm speed returns near its stationary value.
5. Drive steady asphalt, grass/sand, water if available, and a sustained drift.
   Record which raw surface/lateral values change in each controlled state.
6. Make one light barrier scrape and one direct collision. Compare
   `stateFlags`, `collisionByte`, lateral values and speed before/during/after.
7. Repeat steps 3-6 with both `UseNewInput=true` and `false` if legacy input
   compatibility is required. Confirm normal steering, pedals and deadzone are
   unchanged in both modes.
8. Disable `FFBDiagnosticLog` after capture to stop periodic logging.
