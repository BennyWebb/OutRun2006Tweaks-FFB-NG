#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "constant_force.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "game_addrs.hpp"
#include "input_manager.hpp"
#include "plugin.hpp"
#include "telemetry.hpp"

namespace Settings
{
	Setting<bool> FFBEnable{ "FFB", "FFBEnable", false,
		"Enables the experimental Phase 2A constant-force centering effect." };
	Setting<float> FFBStrength{ "FFB", "FFBStrength", 0.75f,
		"Maximum constant-force strength. Start low and increase cautiously.", Range<float>{ 0.0f, 1.0f } };
	Setting<float> FFBMinSpeed{ "FFB", "FFBMinSpeed", 0.0f,
		"Relative vehicle speed below which Phase 2A force is zero.", Range<float>{ 0.0f, 5.0f } };
	Setting<float> FFBFullStrengthSpeed{ "FFB", "FFBFullStrengthSpeed", 1.50f,
		"Relative vehicle speed at which Phase 2A reaches full configured strength.", Range<float>{ 0.0f, 5.0f } };
	Setting<float> FFBSpeedCurveExponent{ "FFB", "FFBSpeedCurveExponent", 0.50f,
		"Shapes centering force versus relative vehicle speed.", Range<float>{ 0.20f, 2.0f } };
	Setting<float> FFBLowSpeedRampSpeed{ "FFB", "FFBLowSpeedRampSpeed", 0.10f,
		"Relative speed over which centering ramps up from standstill.", Range<float>{ 0.01f, 0.50f } };
	Setting<float> FFBLowSpeedScale{ "FFB", "FFBLowSpeedScale", 0.30f,
		"FFB strength multiplier at low speed.", Range<float>{ 0.0f, 1.0f } };
	Setting<float> FFBHighSpeedScale{ "FFB", "FFBHighSpeedScale", 0.65f,
		"FFB strength multiplier at high speed.", Range<float>{ 0.0f, 1.0f } };
	Setting<float> FFBCentreDeadZone{ "FFB", "FFBCentreDeadZone", 0.01f,
		"Small centre-only deadzone used to prevent force chatter.", Range<float>{ 0.0f, 0.10f } };
	Setting<float> FFBCentreCurveExponent{ "FFB", "FFBCentreCurveExponent", 0.60f,
		"Shapes centering force versus steering displacement.", Range<float>{ 0.20f, 2.0f } };
}

namespace FFB
{
	namespace
	{
		// SDL's DirectInput joystick backend already owns the selected device's
		// exclusive IDirectInputDevice8 handle. Opening the haptic interface from
		// that joystick deliberately shares the handle; on Windows SDL translates
		// this single SDL_HAPTIC_CONSTANT into one GUID_ConstantForce DIEFFECT.
		SDL_Haptic* Haptic = nullptr;
		SDL_HapticEffectID EffectId = -1;
		SDL_JoystickID DeviceId = 0;
		bool EffectRunning = false;
		bool EffectStatusSupported = false;
		int32_t CurrentMagnitude = 0;
		std::string LastOpenResult = "not-attempted";
		std::string LastCreateResult = "not-attempted";
		std::string LastUpdateResult = "not-attempted";
		std::string LastRunResult = "not-attempted";
		uint64_t LastTelemetryMs = 0;
		uint64_t NextOpenAttemptMs = 0;
		SDL_JoystickID LastOpenAttemptDeviceId = 0;
		uint32_t DiagnosticSamples = 0;
		uint64_t DiagnosticWindow = 0;
		float LastRawSpeed = 0.0f;
		float LastNormalizedSpeed = 0.0f;
		float LastSpeedCurveExponent = 0.50f;
		float LastMovementRamp = 0.0f;
		float LastCurvedSpeed = 0.0f;
		float LastLowSpeedScale = 0.30f;
		float LastHighSpeedScale = 0.65f;
		float LastTargetSpeedScale = 0.0f;
		float LastSpeedScale = 0.0f;
		float LastRequestedForce = 0.0f;
		float LastPhysicalSteer = 0.0f;
		float LastCentreDeadZone = 0.01f;
		float LastCurveExponent = 0.60f;
		float LastShapedSteer = 0.0f;
		constexpr Uint32 PersistentEffectLengthMs = 32767;

		SDL_HapticEffect MakeEffect(int32_t diMagnitude)
		{
			SDL_HapticEffect effect{};
			effect.type = SDL_HAPTIC_CONSTANT;
			effect.constant.direction.type = SDL_HAPTIC_STEERING_AXIS;
			// SDL 3.4.12's DirectInput backend multiplies this millisecond field
			// by 1000 without special-casing SDL_HAPTIC_INFINITY. Use SDL's
			// documented maximum finite duration and repeat it indefinitely.
			effect.constant.length = PersistentEffectLengthMs;
			effect.constant.button = 0;
			// SDL's public level is Sint16 full scale; its DirectInput backend
			// converts that to DI's documented -10000..10000 magnitude range.
			effect.constant.level = static_cast<Sint16>(std::lround(
				std::clamp(diMagnitude, -10000, 10000) * (32767.0f / 10000.0f)));
			return effect;
		}

		bool StartEffect(bool recovery = false)
		{
			if (!Haptic || EffectId < 0)
				return false;

			SDL_ClearError();
			const bool result = SDL_RunHapticEffect(Haptic, EffectId, SDL_HAPTIC_INFINITY);
			if (!result)
			{
				LastRunResult = std::string("failed: ") + SDL_GetError();
				spdlog::error("FFB: SDL_RunHapticEffect failed: {}", SDL_GetError());
				EffectRunning = false;
				return false;
			}

			LastRunResult = "ok";
			EffectRunning = true;
			if (recovery)
				spdlog::info("FFB: effect was stopped; restarted persistent constant force");
			else
				spdlog::info("FFB: SDL_RunHapticEffect started persistent constant force "
					"(effect length {} ms, infinite repeat)", PersistentEffectLengthMs);
			return true;
		}

		void SetMagnitude(int32_t magnitude)
		{
			if (!Haptic || EffectId < 0)
				return;

			magnitude = std::clamp(magnitude, -10000, 10000);
			if (magnitude != 0 && EffectStatusSupported &&
				!SDL_GetHapticEffectStatus(Haptic, EffectId))
			{
				EffectRunning = false;
				if (magnitude == CurrentMagnitude)
				{
					StartEffect(true);
					return;
				}
			}
			if (magnitude == CurrentMagnitude && EffectRunning)
				return;

			auto effect = MakeEffect(magnitude);
			SDL_ClearError();
			const bool updateResult = SDL_UpdateHapticEffect(Haptic, EffectId, &effect);
			if (!updateResult)
			{
				LastUpdateResult = std::string("failed: ") + SDL_GetError();
				spdlog::error("FFB: failed to update constant-force effect: {}", SDL_GetError());
				// The old magnitude may still be running. Stop it explicitly rather
				// than allowing stale force after a failed safety-zero update.
				SDL_StopHapticEffect(Haptic, EffectId);
				EffectRunning = false;
				return;
			}
			LastUpdateResult = "ok";
			CurrentMagnitude = magnitude;

			// Normal DirectInput SetParameters updates leave a running effect
			// running. Only restart after an observed run/update failure.
			if (magnitude != 0 && !EffectRunning)
				StartEffect(true);
		}

		void ZeroOutput()
		{
			if (CurrentMagnitude != 0 || !EffectRunning)
				SetMagnitude(0);
		}

		void CloseRuntimeDevice()
		{
			if (!Haptic)
				return;

			ZeroOutput();
			if (EffectId >= 0)
			{
				SDL_StopHapticEffect(Haptic, EffectId);
				SDL_DestroyHapticEffect(Haptic, EffectId);
			}
			SDL_CloseHaptic(Haptic);
			Haptic = nullptr;
			EffectId = -1;
			DeviceId = 0;
			EffectRunning = false;
			EffectStatusSupported = false;
			CurrentMagnitude = 0;
		}

		bool EnsureDevice()
		{
			SDL_Joystick* joystick = InputManager_GetPrimaryJoystick();
			const SDL_JoystickID selectedId = InputManager_GetPrimaryJoystickId();
			if (!joystick || selectedId == 0)
			{
				if (Haptic)
					CloseRuntimeDevice();
				return false;
			}

			if (Haptic && DeviceId == selectedId)
				return true;
			if (Haptic)
				CloseRuntimeDevice();
			const uint64_t now = SDL_GetTicks();
			if (selectedId == LastOpenAttemptDeviceId && now < NextOpenAttemptMs)
				return false;
			LastOpenAttemptDeviceId = selectedId;
			NextOpenAttemptMs = now + 5000;

			if (!SDL_InitSubSystem(SDL_INIT_HAPTIC))
			{
				LastOpenResult = std::string("failed init: ") + SDL_GetError();
				spdlog::error("FFB: SDL haptic subsystem initialization failed: {}", SDL_GetError());
				return false;
			}

			SDL_ClearError();
			Haptic = SDL_OpenHapticFromJoystick(joystick);
			if (!Haptic)
			{
				LastOpenResult = std::string("failed: ") + SDL_GetError();
				spdlog::error("FFB: selected joystick '{}' has no usable DirectInput haptic interface: {}",
					SDL_GetJoystickName(joystick), SDL_GetError());
				return false;
			}
			LastOpenResult = "ok";

			auto effect = MakeEffect(0);
			if (!SDL_HapticEffectSupported(Haptic, &effect))
			{
				spdlog::error("FFB: selected joystick '{}' does not support constant force",
					SDL_GetJoystickName(joystick));
				SDL_CloseHaptic(Haptic);
				Haptic = nullptr;
				LastCreateResult = "failed: constant force unsupported";
				return false;
			}

			SDL_ClearError();
			EffectId = SDL_CreateHapticEffect(Haptic, &effect);
			if (EffectId < 0)
			{
				LastCreateResult = std::string("failed: ") + SDL_GetError();
				spdlog::error("FFB: failed to create DirectInput constant-force effect for '{}': {}",
					SDL_GetJoystickName(joystick), SDL_GetError());
				SDL_CloseHaptic(Haptic);
				Haptic = nullptr;
				return false;
			}
			LastCreateResult = "ok";

			DeviceId = selectedId;
			NextOpenAttemptMs = 0;
			EffectRunning = false;
			EffectStatusSupported = (SDL_GetHapticFeatures(Haptic) & SDL_HAPTIC_STATUS) != 0;
			CurrentMagnitude = 0;
			spdlog::info("FFB: created one persistent DirectInput constant-force effect for '{}' (SDL instance {})",
				SDL_GetJoystickName(joystick), DeviceId);
			if (!StartEffect())
			{
				SDL_DestroyHapticEffect(Haptic, EffectId);
				EffectId = -1;
				SDL_CloseHaptic(Haptic);
				Haptic = nullptr;
				DeviceId = 0;
				return false;
			}
			return true;
		}

		bool ForceAllowed()
		{
			if (!Settings::FFBEnable || !Settings::UseNewInput || !Game::current_mode)
				return false;
			if (*Game::current_mode == GameState::STATE_GAME)
				return true;
			return *Game::current_mode == GameState::STATE_START && Game::game_start_progress_code &&
				*Game::game_start_progress_code == 65;
		}

		void LogOutput()
		{
			if (!Settings::FFBDiagnosticLog)
			{
				DiagnosticSamples = 0;
				return;
			}
			if (++DiagnosticSamples < 60)
				return;

			++DiagnosticWindow;
			const bool reportedRunning = EffectStatusSupported && Haptic && EffectId >= 0
				? SDL_GetHapticEffectStatus(Haptic, EffectId)
				: EffectRunning;
			spdlog::info(
				"FFB OUTPUT window={} enabled={} physicalSteer={:.5f} centreDeadZone={:.4f} "
				"curveExponent={:.3f} shapedSteer={:.5f} speedCandidate={:.5f} "
				"normalizedSpeed={:.5f} movementRamp={:.5f} speedCurveExponent={:.3f} "
				"curvedSpeed={:.5f} lowSpeedScale={:.4f} highSpeedScale={:.4f} "
				"targetSpeedScale={:.4f} speedScale={:.4f} "
				"requestedForce={:.5f} diMagnitude={} hapticOpened={} effectCreated={} "
				"effectRunning={} statusSupported={} lastOpen='{}' lastCreate='{}' lastUpdate='{}' lastRun='{}'",
				DiagnosticWindow, Settings::FFBEnable.get(), LastPhysicalSteer, LastCentreDeadZone,
				LastCurveExponent, LastShapedSteer, LastRawSpeed, LastNormalizedSpeed,
				LastMovementRamp, LastSpeedCurveExponent, LastCurvedSpeed, LastLowSpeedScale,
				LastHighSpeedScale, LastTargetSpeedScale, LastSpeedScale, LastRequestedForce, CurrentMagnitude,
				Haptic != nullptr, EffectId >= 0,
				reportedRunning, EffectStatusSupported, LastOpenResult, LastCreateResult,
				LastUpdateResult, LastRunResult);
			DiagnosticSamples = 0;
		}
	}

	void UpdateConstantForce(const TelemetrySnapshot& telemetry)
	{
		LastTelemetryMs = SDL_GetTicks();
		LastPhysicalSteer = telemetry.physicalSteer;
		LastRawSpeed = telemetry.speed;
		LastNormalizedSpeed = 0.0f;
		LastSpeedCurveExponent = Settings::FFBSpeedCurveExponent;
		LastMovementRamp = 0.0f;
		LastCurvedSpeed = 0.0f;
		LastLowSpeedScale = Settings::FFBLowSpeedScale;
		LastHighSpeedScale = std::max(Settings::FFBHighSpeedScale.get(), LastLowSpeedScale);
		LastTargetSpeedScale = 0.0f;
		LastSpeedScale = 0.0f;
		LastRequestedForce = 0.0f;
		LastCentreDeadZone = Settings::FFBCentreDeadZone;
		LastCurveExponent = Settings::FFBCentreCurveExponent;
		LastShapedSteer = 0.0f;

		if (!ForceAllowed())
		{
			ZeroOutput();
			LogOutput();
			return;
		}
		if (!EnsureDevice())
		{
			LogOutput();
			return;
		}

		const float minSpeed = Settings::FFBMinSpeed;
		const float fullSpeed = Settings::FFBFullStrengthSpeed;
		if (telemetry.speed > minSpeed)
		{
			if (fullSpeed <= minSpeed)
				LastNormalizedSpeed = 1.0f;
			else
				LastNormalizedSpeed = std::clamp(
					(telemetry.speed - minSpeed) / (fullSpeed - minSpeed), 0.0f, 1.0f);
		}
		LastMovementRamp = std::clamp(
			telemetry.speed / Settings::FFBLowSpeedRampSpeed.get(), 0.0f, 1.0f);
		LastCurvedSpeed = std::pow(LastNormalizedSpeed, LastSpeedCurveExponent);
		LastTargetSpeedScale = LastLowSpeedScale +
			(LastHighSpeedScale - LastLowSpeedScale) * LastCurvedSpeed;
		LastSpeedScale = LastMovementRamp * LastTargetSpeedScale;

		const float absSteer = std::abs(telemetry.physicalSteer);
		if (absSteer > LastCentreDeadZone)
		{
			const float normalized = std::clamp(
				(absSteer - LastCentreDeadZone) / (1.0f - LastCentreDeadZone), 0.0f, 1.0f);
			const float shapedMagnitude = std::pow(normalized, LastCurveExponent);
			LastShapedSteer = std::copysign(shapedMagnitude, telemetry.physicalSteer);
		}

		LastRequestedForce = std::clamp(
			LastShapedSteer * LastSpeedScale * Settings::FFBStrength.get(), -1.0f, 1.0f);
		const auto magnitude = static_cast<int32_t>(std::lround(LastRequestedForce * 10000.0f));
		SetMagnitude(magnitude);
		LogOutput();
	}

	void RuntimeTick()
	{
		if (!Haptic)
			return;

		const bool telemetryStale = LastTelemetryMs != 0 && SDL_GetTicks() - LastTelemetryMs > 250;
		if (!ForceAllowed() || telemetryStale || InputManager_GetPrimaryJoystickId() != DeviceId)
			ZeroOutput();
	}

	void OnInputDeviceRemoved(SDL_JoystickID instanceId)
	{
		if (Haptic && instanceId == DeviceId)
			CloseRuntimeDevice();
	}

	void StopOutputForExit()
	{
		// Window shutdown is still normal runtime, so zeroing is safe here. Do not
		// close or release SDL/DirectInput objects: CRT/DLL teardown ordering is
		// deliberately left to the OS.
		ZeroOutput();
	}
}
