#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "telemetry.hpp"
#include "constant_force.hpp"

#include <algorithm>
#include <cmath>

#include "hook_mgr.hpp"
#include "plugin.hpp"
#include "game_addrs.hpp"
#include "input_manager.hpp"

// Restored from the original Xbox vibration routine in hooks_forcefeedback.cpp.
// This lookup is observational here; waterFlag is a caller-owned output.
double __cdecl sub_1149C0(unsigned int surfaceMask, int roadCollisionType, unsigned long* waterFlag);

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

		class LateralRateEstimator
		{
			float previousA = 0.0f;
			float previousB = 0.0f;
			bool initialized = false;

		public:
			void update(TelemetrySnapshot& snapshot)
			{
				if (!initialized)
				{
					previousA = snapshot.lateralA;
					previousB = snapshot.lateralB;
					initialized = true;
					return;
				}

				constexpr float TickRate = 60.0f;
				snapshot.lateralARate = (snapshot.lateralA - previousA) * TickRate;
				snapshot.lateralBRate = (snapshot.lateralB - previousB) * TickRate;
				snapshot.lateralSumRate = snapshot.lateralARate + snapshot.lateralBRate;
				previousA = snapshot.lateralA;
				previousB = snapshot.lateralB;
			}
		};

		LateralRateEstimator LateralRates;

		class UnknownFieldRateEstimator
		{
			float previous1D0 = 0.0f;
			float previous1D4 = 0.0f;
			bool initialized = false;

		public:
			void update(TelemetrySnapshot& snapshot)
			{
				if (!initialized)
				{
					previous1D0 = snapshot.field1D0;
					previous1D4 = snapshot.field1D4;
					initialized = true;
					return;
				}

				constexpr float TickRate = 60.0f;
				snapshot.field1D0Rate = (snapshot.field1D0 - previous1D0) * TickRate;
				snapshot.field1D4Rate = (snapshot.field1D4 - previous1D4) * TickRate;
				previous1D0 = snapshot.field1D0;
				previous1D4 = snapshot.field1D4;
			}
		};

		UnknownFieldRateEstimator UnknownFieldRates;
		float CountersteerDuration = 0.0f;

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
			Snapshot.field1C8 = car->field_1C8;
			Snapshot.field1CC = car->field_1CC;
			Snapshot.field1D0 = car->field_1D0;
			Snapshot.field1D4 = car->field_1D4;
			Snapshot.field1DC = car->field_1DC;
			Snapshot.field1E0 = car->field_1E0;
			UnknownFieldRates.update(Snapshot);
			Snapshot.lateralA = car->field_264;
			Snapshot.lateralB = car->field_268;
			Snapshot.lateralCombined = Snapshot.lateralA + Snapshot.lateralB;
			Snapshot.lateralDifference = Snapshot.lateralA - Snapshot.lateralB;
			LateralRates.update(Snapshot);

			unsigned long waterFlag = 0;
			Snapshot.stockSurfaceRoughness = 0.0f;
			for (const uint32_t surfaceMask : car->water_flag_24C)
			{
				Snapshot.stockSurfaceRoughness = std::max(Snapshot.stockSurfaceRoughness,
					static_cast<float>(sub_1149C0(surfaceMask,
						static_cast<int>(car->OnRoadPlace_5C.loadColiType_0), &waterFlag)));
			}
			Snapshot.stockSurfaceVibration =
				Snapshot.stockSurfaceRoughness * Snapshot.speed * 0.10f;
			Snapshot.stockScrubCandidate = 0.0f;
			if (Snapshot.stockSurfaceRoughness > 0.30f)
			{
				float scrubAmount = 0.0f;
				if (Snapshot.lateralB < 0.10f)
					scrubAmount = 0.10f - Snapshot.lateralB;
				else if (Snapshot.lateralA > -0.10f)
					scrubAmount = Snapshot.lateralA + 0.10f;
				Snapshot.stockScrubCandidate =
					std::clamp(scrubAmount, 0.0f, 2.0f) * Snapshot.speed * 0.01f;
			}

			auto sign = [](float value) { return (value > 0.0f) - (value < 0.0f); };
			Snapshot.countersteerActive =
				std::abs(Snapshot.physicalSteer) >= 0.10f &&
				std::abs(Snapshot.lateralCombined) >= 1.50f &&
				sign(Snapshot.physicalSteer) != sign(Snapshot.lateralCombined);
			if (Snapshot.countersteerActive)
				CountersteerDuration += 1.0f / 60.0f;
			else
				CountersteerDuration = 0.0f;
			Snapshot.countersteerDuration = CountersteerDuration;
			Snapshot.field26C = car->field_26C;
			// This is deliberately a candidate indicator, not an assertion that
			// the two reverse-engineered fields are physical lateral G. The range
			// and threshold came from the reference fork and require road testing.
			Snapshot.driftCandidate = std::clamp((std::abs(Snapshot.lateralCombined) - 12.0f) / 12.0f, 0.0f, 1.0f);
			std::copy(std::begin(car->water_flag_24C), std::end(car->water_flag_24C), std::begin(Snapshot.surface));
			Snapshot.roadCollisionType = car->OnRoadPlace_5C.loadColiType_0;
			Snapshot.gear = car->cur_gear_208;
			Snapshot.stateFlags = car->field_8;
			Snapshot.stateFlag1000 = (Snapshot.stateFlags & 0x00001000u) != 0;
			Snapshot.collisionByte = car->field_coli_281;
			Snapshot.inGameplay = Game::is_in_game();
		}

		void LogSnapshot()
		{
			auto sign = [](float value) { return (value > 0.0f) - (value < 0.0f); };

			struct DiagnosticWindow
			{
				uint64_t number = 0;
				uint32_t samples = 0;
				float steerMin = 0.0f;
				float steerMax = 0.0f;
				float rateMin = 0.0f;
				float rateMax = 0.0f;
				float field1D0Min = 0.0f;
				float field1D0Max = 0.0f;
				float field1D4Min = 0.0f;
				float field1D4Max = 0.0f;
				float field1D0RateMin = 0.0f;
				float field1D0RateMax = 0.0f;
				float field1D4RateMin = 0.0f;
				float field1D4RateMax = 0.0f;
				float field26CMin = 0.0f;
				float field26CMax = 0.0f;
				uint32_t stateFlag1000Samples = 0;
				bool active = false;

				void reset(const TelemetrySnapshot& snapshot)
				{
					samples = 0;
					steerMin = steerMax = snapshot.physicalSteer;
					rateMin = rateMax = snapshot.steerRate;
					field1D0Min = field1D0Max = snapshot.field1D0;
					field1D4Min = field1D4Max = snapshot.field1D4;
					field1D0RateMin = field1D0RateMax = snapshot.field1D0Rate;
					field1D4RateMin = field1D4RateMax = snapshot.field1D4Rate;
					field26CMin = field26CMax = snapshot.field26C;
					stateFlag1000Samples = 0;
					active = true;
				}
			};

			static DiagnosticWindow window;
			if (!Settings::FFBDiagnosticLog)
			{
				window.active = false;
				return;
			}

			if (!window.active)
				window.reset(Snapshot);

			window.steerMin = std::min(window.steerMin, Snapshot.physicalSteer);
			window.steerMax = std::max(window.steerMax, Snapshot.physicalSteer);
			window.rateMin = std::min(window.rateMin, Snapshot.steerRate);
			window.rateMax = std::max(window.rateMax, Snapshot.steerRate);
			window.field1D0Min = std::min(window.field1D0Min, Snapshot.field1D0);
			window.field1D0Max = std::max(window.field1D0Max, Snapshot.field1D0);
			window.field1D4Min = std::min(window.field1D4Min, Snapshot.field1D4);
			window.field1D4Max = std::max(window.field1D4Max, Snapshot.field1D4);
			window.field1D0RateMin = std::min(window.field1D0RateMin, Snapshot.field1D0Rate);
			window.field1D0RateMax = std::max(window.field1D0RateMax, Snapshot.field1D0Rate);
			window.field1D4RateMin = std::min(window.field1D4RateMin, Snapshot.field1D4Rate);
			window.field1D4RateMax = std::max(window.field1D4RateMax, Snapshot.field1D4Rate);
			window.field26CMin = std::min(window.field26CMin, Snapshot.field26C);
			window.field26CMax = std::max(window.field26CMax, Snapshot.field26C);
			window.stateFlag1000Samples += Snapshot.stateFlag1000 ? 1u : 0u;
			if (++window.samples < 60)
				return;
			++window.number;

			spdlog::info(
				"FFB DIAG INPUT window={} samples={} physicalSteer.cur={:.5f} "
				"physicalSteer.range=[{:.5f},{:.5f}] steeringRate.filtered.cur={:.5f}/s "
				"steeringRate.filtered.range=[{:.5f},{:.5f}]/s",
				window.number, window.samples, Snapshot.physicalSteer, window.steerMin, window.steerMax,
				Snapshot.steerRate, window.rateMin, window.rateMax);

			spdlog::info(
				"FFB DIAG VEHICLE window={} inGameplay={} raw.speedCandidate={:.6f} "
				"raw.lateralA={:.6f} raw.lateralB={:.6f} derived.lateralSum={:.6f} "
				"derived.driftHeuristic={:.4f} raw.gearCandidate={}",
				window.number, Snapshot.inGameplay, Snapshot.speed,
				Snapshot.lateralA, Snapshot.lateralB, Snapshot.lateralCombined, Snapshot.driftCandidate,
				Snapshot.gear);

			const float absA = std::abs(Snapshot.lateralA);
			const float absB = std::abs(Snapshot.lateralB);
			const float absSum = std::abs(Snapshot.lateralCombined);
			const float absDiff = std::abs(Snapshot.lateralDifference);
			spdlog::info(
				"FFB DRIFT DIAG window={} speed={:.6f} steer={:.5f} "
				"A={:.6f} B={:.6f} sum={:.6f} diff={:.6f} "
				"absA={:.6f} absB={:.6f} absSum={:.6f} absDiff={:.6f} "
				"maxAbs={:.6f} minAbs={:.6f} "
				"signSteer={} signA={} signB={} signSum={} "
				"A_rate={:.6f}/s B_rate={:.6f}/s sum_rate={:.6f}/s "
				"stockRoughness={:.4f} stockSurfaceVibration={:.6f} stockScrubCandidate={:.6f} "
				"countersteerActive={} countersteerDuration={:.3f}s",
				window.number, Snapshot.speed, Snapshot.physicalSteer,
				Snapshot.lateralA, Snapshot.lateralB, Snapshot.lateralCombined, Snapshot.lateralDifference,
				absA, absB, absSum, absDiff, std::max(absA, absB), std::min(absA, absB),
				sign(Snapshot.physicalSteer), sign(Snapshot.lateralA), sign(Snapshot.lateralB),
				sign(Snapshot.lateralCombined), Snapshot.lateralARate, Snapshot.lateralBRate,
				Snapshot.lateralSumRate, Snapshot.stockSurfaceRoughness,
				Snapshot.stockSurfaceVibration, Snapshot.stockScrubCandidate,
				Snapshot.countersteerActive, Snapshot.countersteerDuration);

			spdlog::info(
				"FFB FIELD DIAG window={} "
				"raw.field1D0={:.7f} range1D0=[{:.7f},{:.7f}] "
				"raw.field1D4={:.7f} range1D4=[{:.7f},{:.7f}] "
				"sum={:.7f} diff={:.7f} abs1D0={:.7f} abs1D4={:.7f} "
				"sign1D0={} sign1D4={} product={:.9f} "
				"rate1D0={:.7f}/s rangeRate1D0=[{:.7f},{:.7f}]/s "
				"rate1D4={:.7f}/s rangeRate1D4=[{:.7f},{:.7f}]/s "
				"nearby=[1C8:{:.7f},1CC:{:.7f},1DC:{:.7f},1E0:{:.7f},26C:{:.7f}] "
				"range26C=[{:.7f},{:.7f}] stateFlag1000={} stateFlag1000Samples={}/{}",
				window.number, Snapshot.field1D0, window.field1D0Min, window.field1D0Max,
				Snapshot.field1D4, window.field1D4Min, window.field1D4Max,
				Snapshot.field1D0 + Snapshot.field1D4, Snapshot.field1D0 - Snapshot.field1D4,
				std::abs(Snapshot.field1D0), std::abs(Snapshot.field1D4),
				sign(Snapshot.field1D0), sign(Snapshot.field1D4),
				Snapshot.field1D0 * Snapshot.field1D4,
				Snapshot.field1D0Rate, window.field1D0RateMin, window.field1D0RateMax,
				Snapshot.field1D4Rate, window.field1D4RateMin, window.field1D4RateMax,
				Snapshot.field1C8, Snapshot.field1CC, Snapshot.field1DC, Snapshot.field1E0,
				Snapshot.field26C, window.field26CMin, window.field26CMax,
				Snapshot.stateFlag1000, window.stateFlag1000Samples, window.samples);

			spdlog::info(
				"FFB DIAG CONTACT window={} raw.wheelSurfaceMask=[0:{:#010x},1:{:#010x},2:{:#010x},3:{:#010x}] "
				"raw.roadCollisionType={}({:#010x}) raw.stateFlags={:#010x} "
				"raw.collisionByte={}({:#04x})",
				window.number,
				Snapshot.surface[0], Snapshot.surface[1], Snapshot.surface[2], Snapshot.surface[3],
				Snapshot.roadCollisionType, Snapshot.roadCollisionType, Snapshot.stateFlags,
				Snapshot.collisionByte, Snapshot.collisionByte);

			window.reset(Snapshot);
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
			UpdateConstantForce(Snapshot);
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
