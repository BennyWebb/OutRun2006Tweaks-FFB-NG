# Generic joystick input

When `UseNewInput=true`, mapped controllers use SDL's Gamepad API and generic
wheels, pedals, flight sticks, and other devices use SDL's Joystick API. The
Controllers tab lists each physical SDL instance once and identifies its
category. Selecting a device activates its binding profile immediately:

- `[Gamepad]` for mapped SDL Gamepads;
- `[Joystick]` for generic SDL Joysticks;
- `[Keyboard]` alongside either profile.

Existing binding files containing only `[Gamepad]` and `[Keyboard]` remain
valid. Saving adds an empty `[Joystick]` section until joystick inputs are
bound.

## Joystick binding format and calibration

Joystick buttons and hats are stored as `Button-N` and `Hat-N-Direction`.
Steering is a signed `Axis-N` binding. Acceleration, brake, and other axis
actions capture the axis resting value and the direction moved during the
binding prompt, and are stored as `Axis-N@Rest:Direction`, for example
`Axis-2@32767:-`. This supports either endpoint polarity and combined pedals
without assuming device-specific axis numbers. The existing invert checkbox
reverses the captured movement direction.

Keep a pedal released when opening the binding prompt, then press it through at
least one quarter of its range. An axis must move by 8192 SDL units from that
captured rest value before it is accepted, which prevents ordinary axis noise
from creating a binding. Steering relies on the device reporting the usual
signed SDL range with its physical centre near zero; devices that do not report
that convention require driver-level calibration.

Mapped Gamepads and generic Joysticks have independent steering deadzones:
`GamepadSteeringDeadZone` defaults to 20%, while
`JoystickSteeringDeadZone` defaults to 0%. Both are available in the binding
dialog's Options tab. Existing configurations that only set the legacy
`SteeringDeadZone` use that value for the Gamepad deadzone; it remains the
legacy-input deadzone when `UseNewInput=false`.

Joystick input does not create force-feedback effects. Existing SDL Gamepad
rumble remains limited to the selected Gamepad device.
