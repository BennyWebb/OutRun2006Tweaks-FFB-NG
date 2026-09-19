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
		float field1C8 = 0.0f;
		float field1CC = 0.0f;
		float field1D0 = 0.0f;
		float field1D4 = 0.0f;
		float field1D0Rate = 0.0f;
		float field1D4Rate = 0.0f;
		float field1DC = 0.0f;
		float field1E0 = 0.0f;
		float lateralA = 0.0f;
		float lateralB = 0.0f;
		float lateralCombined = 0.0f;
		float lateralDifference = 0.0f;
		float lateralARate = 0.0f;
		float lateralBRate = 0.0f;
		float lateralSumRate = 0.0f;
		float stockSurfaceRoughness = 0.0f;
		float stockSurfaceVibration = 0.0f;
		float stockScrubCandidate = 0.0f;
		bool countersteerActive = false;
		float countersteerDuration = 0.0f;
		float field26C = 0.0f;
		float driftCandidate = 0.0f;
		uint32_t surface[4]{};
		uint32_t roadCollisionType = 0;
		uint32_t gear = 0;
		uint32_t stateFlags = 0;
		bool stateFlag1000 = false;
		uint8_t collisionByte = 0;
		bool inGameplay = false;
	};

	const TelemetrySnapshot& GetTelemetrySnapshot();
}
