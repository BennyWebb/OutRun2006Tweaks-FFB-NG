#pragma once

#include <SDL3/SDL_joystick.h>

namespace FFB
{
	struct TelemetrySnapshot;

	// Phase 2A output boundary: one persistent constant-force effect only.
	void UpdateConstantForce(const TelemetrySnapshot& telemetry);
	void RuntimeTick();
	void OnInputDeviceRemoved(SDL_JoystickID instanceId);
	void StopOutputForExit();
}
