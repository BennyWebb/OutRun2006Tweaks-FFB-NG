#pragma once

#include <cstdint>

namespace FFB
{
	// Phase 1 signal boundary. Future force models should consume this snapshot
	// instead of reading reverse-engineered game memory directly.
	struct TelemetrySnapshot
	{
		float speed = 0.0f;
		float physicalSteer = 0.0f;
		float steerRate = 0.0f;
		float lateralA = 0.0f;
		float lateralB = 0.0f;
		float lateralCombined = 0.0f;
		float driftCandidate = 0.0f;
		uint32_t surface[4]{};
		uint32_t roadCollisionType = 0;
		uint32_t gear = 0;
		uint32_t stateFlags = 0;
		uint8_t collisionByte = 0;
		bool inGameplay = false;
	};

	const TelemetrySnapshot& GetTelemetrySnapshot();
}
