#include "telemetry.hpp"

#include <algorithm>
#include <cmath>

#include "hook_mgr.hpp"
#include "plugin.hpp"
#include "game_addrs.hpp"
#include "input_manager.hpp"

namespace Settings
{
	Setting<bool> FFBDiagnosticLog{ "FFB", "FFBDiagnosticLog", false,
		"Logs physical steering and candidate vehicle telemetry once per second. "
		"Diagnostics only; no force-feedback effects are created." };
}

namespace FFB
{
	namespace
	{
		TelemetrySnapshot Snapshot;

		class SteeringRateEstimator
		{
			float previousSteer = 0.0f;
			float filteredRate = 0.0f;
			bool initialized = false;

		public:
			float update(float steer)
			{
				if (!initialized)
				{
					previousSteer = steer;
					initialized = true;
					return 0.0f;
				}

				// GamePlCar_Ctrl runs on the fixed 60 Hz simulation tick, including
				// when the renderer is unlocked. Ignore one raw-axis count of jitter,
				// then low-pass the derivative for a future damping input.
				float delta = steer - previousSteer;
				previousSteer = steer;
				if (std::abs(delta) <= (1.0f / 32768.0f))
					delta = 0.0f;

				constexpr float TickRate = 60.0f;
				constexpr float FilterAlpha = 0.20f;
				const float instantaneousRate = delta * TickRate;
				filteredRate += FilterAlpha * (instantaneousRate - filteredRate);
				if (delta == 0.0f && std::abs(filteredRate) < 0.001f)
					filteredRate = 0.0f;
				return filteredRate;
			}
		};

		SteeringRateEstimator RateEstimator;

		float ReadPhysicalSteering()
		{
			if (Settings::UseNewInput)
				return InputManager_GetPhysicalSteering();

			// Legacy input already exposes the selected DirectInput axis through
			// GetVolume. Calling it is observational and preserves the game's
			// device selection, deadzone and input behavior.
			using GetVolumeFn = int(__cdecl*)(ADChannel);
			auto getVolume = Module::fn_ptr<GetVolumeFn>(0x53720);
			if (!getVolume)
				return 0.0f;
			return std::clamp(getVolume(ADChannel::Steering) / 127.0f, -1.0f, 1.0f);
		}

		void UpdateSnapshot(EVWORK_CAR* car)
		{
			if (!car)
				return;

			Snapshot.physicalSteer = ReadPhysicalSteering();
			Snapshot.steerRate = RateEstimator.update(Snapshot.physicalSteer);
			Snapshot.speed = car->field_1C4;
			Snapshot.lateralA = car->field_264;
			Snapshot.lateralB = car->field_268;
			Snapshot.lateralCombined = Snapshot.lateralA + Snapshot.lateralB;
			// This is deliberately a candidate indicator, not an assertion that
			// the two reverse-engineered fields are physical lateral G. The range
			// and threshold came from the reference fork and require road testing.
			Snapshot.driftCandidate = std::clamp((std::abs(Snapshot.lateralCombined) - 12.0f) / 12.0f, 0.0f, 1.0f);
			std::copy(std::begin(car->water_flag_24C), std::end(car->water_flag_24C), std::begin(Snapshot.surface));
			Snapshot.roadCollisionType = car->OnRoadPlace_5C.loadColiType_0;
			Snapshot.gear = car->cur_gear_208;
			Snapshot.stateFlags = car->field_8;
			Snapshot.collisionByte = car->field_coli_281;
			Snapshot.inGameplay = Game::is_in_game();
		}

		void LogSnapshot()
		{
			static uint32_t ticksSinceLog = 0;
			static float steerMin = 0.0f;
			static float steerMax = 0.0f;
			static float rateMin = 0.0f;
			static float rateMax = 0.0f;
			if (!Settings::FFBDiagnosticLog)
				return;

			steerMin = std::min(steerMin, Snapshot.physicalSteer);
			steerMax = std::max(steerMax, Snapshot.physicalSteer);
			rateMin = std::min(rateMin, Snapshot.steerRate);
			rateMax = std::max(rateMax, Snapshot.steerRate);
			if (++ticksSinceLog < 60)
				return;
			ticksSinceLog = 0;

			spdlog::info(
				"FFB DIAG: inGame={} speed={:.4f} physicalSteer={:.4f} steerRange=[{:.4f},{:.4f}] "
				"steerRate={:.4f}/s rateRange=[{:.4f},{:.4f}]/s "
				"lateral=[{:.4f},{:.4f}] combined={:.4f} driftCandidate={:.3f} "
				"surface=[{:#x},{:#x},{:#x},{:#x}] roadCollisionType={:#x} gear={} "
				"stateFlags={:#x} collisionByte={:#x}",
				Snapshot.inGameplay, Snapshot.speed, Snapshot.physicalSteer, steerMin, steerMax,
				Snapshot.steerRate, rateMin, rateMax,
				Snapshot.lateralA, Snapshot.lateralB, Snapshot.lateralCombined, Snapshot.driftCandidate,
				Snapshot.surface[0], Snapshot.surface[1], Snapshot.surface[2], Snapshot.surface[3],
				Snapshot.roadCollisionType, Snapshot.gear, Snapshot.stateFlags, Snapshot.collisionByte);

			steerMin = steerMax = Snapshot.physicalSteer;
			rateMin = rateMax = Snapshot.steerRate;
		}
	}

	const TelemetrySnapshot& GetTelemetrySnapshot()
	{
		return Snapshot;
	}

	class TelemetryHook : public Hook
	{
		inline static SafetyHookInline GamePlCarCtrlHook{};

		static void __cdecl GamePlCarCtrlDest(EVWORK_CAR* car)
		{
			GamePlCarCtrlHook.call(car);
			UpdateSnapshot(car);
			LogSnapshot();
		}

	public:
		std::string_view description() override { return "FFBTelemetry"; }

		bool apply() override
		{
			// Address and EVWORK_CAR candidates are adapted from the reference
			// FFB fork. This hook only observes state after the original update.
			GamePlCarCtrlHook = safetyhook::create_inline(Module::exe_ptr(0xA8330), GamePlCarCtrlDest);
			return bool(GamePlCarCtrlHook);
		}

		static TelemetryHook instance;
	};

	TelemetryHook TelemetryHook::instance;
}
