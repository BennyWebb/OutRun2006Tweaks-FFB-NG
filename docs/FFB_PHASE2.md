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

## Force model

Only the physically validated `physicalSteer` and relative `speedCandidate`
signals are consumed:

```text
speedScale = 0                                             when speed <= minSpeed
speedScale = clamp((speed - minSpeed) /
                   (fullStrengthSpeed - minSpeed), 0, 1)  otherwise
requestedForce = clamp(physicalSteer * speedScale * strength, -1, 1)
diMagnitude = round(requestedForce * 10000)
```

If `fullStrengthSpeed <= minSpeed`, the scale changes directly to 1 above the
minimum rather than dividing by zero. Physical testing with the G27-tool
DirectInput driver established that a positive signed magnitude produces the
physical force needed to oppose positive (right) steering, while a negative
magnitude opposes negative (left) steering. The model therefore preserves the
`physicalSteer` sign: holding right requests a positive centering pull, and
holding left requests a negative centering pull. The signed DirectInput
magnitude is always clamped to `-10000..10000`.

## Settings

All settings are in `[FFB]` and are live-editable in the configuration overlay:

| Setting | Default | Overlay range | Meaning |
| --- | ---: | ---: | --- |
| `FFBEnable` | `false` | off/on | Enables Phase 2A output |
| `FFBStrength` | `0.30` | `0.00..1.00` | Maximum normalized force |
| `FFBMinSpeed` | `0.05` | `0.00..5.00` | Speed where the linear ramp begins |
| `FFBFullStrengthSpeed` | `1.50` | `0.00..5.00` | Speed where the ramp reaches 1 |
| `FFBDiagnosticLog` | `false` | off/on | Enables one-second input, telemetry and output logs |

## Safety and lifetime

Output is set to zero when FFB is disabled, the selected device is not a
generic joystick, speed is below the threshold, the active race ends, the game
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
FFB OUTPUT window=N enabled=... speedScale=... physicalSteer=... requestedForce=... diMagnitude=... hapticOpened=... effectCreated=... effectRunning=... statusSupported=... lastOpen='...' lastCreate='...' lastUpdate='...' lastRun='...'
```

Opening is retried after five seconds when SDL initially cannot match or query
the joystick through its haptic device list. SDL's public wrapper can replace
the lower-level DirectInput failure with the generic
`SDL_SYS_HapticOpenFromJoystick failed` message, so that message alone does not
identify the underlying HRESULT. The five-second delay is the module's bounded
retry interval rather than time spent inside the open call.

## Explicitly not implemented

Phase 2A has no steering-rate damping, spring or damper condition effect,
periodic/sine effect, collision impulse, road texture, surface response, drift
response, gear response, Logitech-specific code, native HID output, or custom
wheel command. The diagnostic-only telemetry candidates remain excluded until
physical validation supports a later phase.

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
