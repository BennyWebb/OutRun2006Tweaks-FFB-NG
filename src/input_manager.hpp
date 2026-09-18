#pragma once

#include <SDL3/SDL.h>
#include <unordered_map>
#include <vector>
#include <memory>

#include "hook_mgr.hpp"
#include "plugin.hpp"
#include "game_addrs.hpp"
#include "overlay/overlay.hpp"

#include <array>
#include <algorithm>
#include <optional>
#include <istream>
#include "input_names.hpp"

#include "imgui.h"
#include <format>
#include <string>
#include <fstream>
#include <cstdlib>
#include <cstdio>

// fixups for SDL3 sillyness
#define SDL_GAMEPAD_BUTTON_A SDL_GAMEPAD_BUTTON_SOUTH
#define SDL_GAMEPAD_BUTTON_B SDL_GAMEPAD_BUTTON_EAST
#define SDL_GAMEPAD_BUTTON_X SDL_GAMEPAD_BUTTON_WEST
#define SDL_GAMEPAD_BUTTON_Y SDL_GAMEPAD_BUTTON_NORTH

enum class ListenState
{
	False = 0,
	WaitForButtonRelease = 1,
	Listening = 2,
	WaitForBindButtonRelease = 3
};

inline ListenState isListeningForInput = ListenState::False;

namespace FFB
{
	// Called before SDL closes a joystick during a normal runtime removal.
	void OnInputDeviceRemoved(SDL_JoystickID instanceId);
}

// Actions belonging to the mod rather than the game.
enum class ModAction
{
	OverlayToggle,
	HudToggle,
	OpenChat,
	MusicNext,
	MusicPrevious,
	Count
};

enum class InputSourceType
{
	GamePad,
	Keyboard
};

enum class InputDeviceKind
{
	Gamepad,
	Joystick
};

struct InputDevice
{
	InputDeviceKind kind = InputDeviceKind::Gamepad;
	SDL_JoystickID instanceId = 0;
	SDL_Gamepad* gamepad = nullptr;
	SDL_Joystick* joystick = nullptr;
	std::string name;
};

struct InputState
{
	float currentValue = 0.0f;
	float previousValue = 0.0f;
	bool isAxis = false;
	InputSourceType lastSourceType;

	void update(float newValue)
	{
		previousValue = currentValue;
		currentValue = newValue;
	}

	bool isNewlyPressed(float threshold = 0.5f) const
	{
		return currentValue >= threshold && previousValue < threshold;
	}

	bool isPressed(float threshold = 0.5f) const
	{
		return currentValue >= threshold;
	}
};

//
// A single bound input from one of the persistent input profiles.
//
struct InputBinding
{
	enum class Kind : uint8_t { None, PadButton, PadAxis, JoystickButton, JoystickAxis, JoystickHat, Key };

	static constexpr float StickRange = 32768.f;
	static constexpr float RStickDeadzone = 0.7f; // needs a high deadzone or flicks bounce

	Kind kind = Kind::None;
	bool negate = false;
	SDL_GamepadButton button = SDL_GAMEPAD_BUTTON_INVALID;
	SDL_GamepadAxis axis = SDL_GAMEPAD_AXIS_INVALID;
	SDL_Scancode key = SDL_SCANCODE_UNKNOWN;
	int joystickIndex = -1;
	Sint16 joystickRest = 0;
	int8_t joystickDirection = 1;
	Uint8 hatMask = SDL_HAT_CENTERED;
	bool positiveAxis = false;

	InputBinding() = default;
	InputBinding(SDL_GamepadButton b, bool n = false) : kind(Kind::PadButton), negate(n), button(b) {}
	InputBinding(SDL_GamepadAxis a, bool n = false) : kind(Kind::PadAxis), negate(n), axis(a) {}
	InputBinding(SDL_Scancode k, bool n = false) : kind(Kind::Key), negate(n), key(k) {}
	static InputBinding joystickButton(int index)
	{
		InputBinding binding;
		binding.kind = Kind::JoystickButton;
		binding.joystickIndex = index;
		return binding;
	}
	static InputBinding joystickAxis(int index, bool positive, Sint16 rest = 0, int direction = 1)
	{
		InputBinding binding;
		binding.kind = Kind::JoystickAxis;
		binding.joystickIndex = index;
		binding.positiveAxis = positive;
		binding.joystickRest = rest;
		binding.joystickDirection = direction < 0 ? -1 : 1;
		return binding;
	}
	static InputBinding joystickHat(int index, Uint8 mask)
	{
		InputBinding binding;
		binding.kind = Kind::JoystickHat;
		binding.joystickIndex = index;
		binding.hatMask = mask;
		return binding;
	}

	bool isAxis() const { return kind == Kind::PadAxis || kind == Kind::JoystickAxis; }
	bool isKeyboard() const { return kind == Kind::Key; }
	bool isGamepad() const { return kind == Kind::PadButton || kind == Kind::PadAxis; }
	bool isJoystick() const { return kind == Kind::JoystickButton || kind == Kind::JoystickAxis || kind == Kind::JoystickHat; }
	bool isNegated() const { return negate; }

	InputSourceType sourceType() const
	{
		return isKeyboard() ? InputSourceType::Keyboard : InputSourceType::GamePad;
	}

	float read(SDL_Gamepad* gamepad, SDL_Joystick* joystick) const
	{
		float value = 0.0f;

		switch (kind)
		{
		case Kind::Key:
		{
			const bool* state_array = SDL_GetKeyboardState(nullptr);
			value = float(state_array[key]);
			break;
		}
		case Kind::PadButton:
			if (!gamepad)
				return 0.0f;
			value = float(SDL_GetGamepadButton(gamepad, button));
			break;
		case Kind::PadAxis:
		{
			if (!gamepad)
				return 0.0f;

			Sint16 raw = SDL_GetGamepadAxis(gamepad, axis);

			int deadzone = 0;
			if (axis == SDL_GAMEPAD_AXIS_LEFTX || axis == SDL_GAMEPAD_AXIS_LEFTY)
				deadzone = int(StickRange * Settings::GamepadSteeringDeadZone);
			else if (axis == SDL_GAMEPAD_AXIS_RIGHTX || axis == SDL_GAMEPAD_AXIS_RIGHTY)
				deadzone = int(StickRange * RStickDeadzone);
			else
				deadzone = XINPUT_GAMEPAD_TRIGGER_THRESHOLD;

			if (abs(raw) < deadzone)
				raw = 0;

			value = raw / StickRange;
			break;
		}
		case Kind::JoystickButton:
			if (!joystick)
				return 0.0f;
			value = float(SDL_GetJoystickButton(joystick, joystickIndex));
			break;
		case Kind::JoystickHat:
			if (!joystick)
				return 0.0f;
			value = (SDL_GetJoystickHat(joystick, joystickIndex) & hatMask) == hatMask ? 1.0f : 0.0f;
			break;
		case Kind::JoystickAxis:
		{
			if (!joystick)
				return 0.0f;

			const Sint16 raw = SDL_GetJoystickAxis(joystick, joystickIndex);
			if (positiveAxis)
			{
				const int direction = joystickDirection * (negate ? -1 : 1);
				const int extent = direction > 0 ? (32767 - int(joystickRest)) : (int(joystickRest) + 32768);
				value = extent > 0
					? std::clamp((int(raw) - int(joystickRest)) * direction / float(extent), 0.0f, 1.0f)
					: 0.0f;
				return value;
			}

			value = raw < 0 ? raw / 32768.0f : raw / 32767.0f;
			if (std::abs(value) < Settings::JoystickSteeringDeadZone)
				value = 0.0f;
			break;
		}
		default:
			return 0.0f;
		}

		return negate ? -value : value;
	}

	std::string displayName(SDL_GamepadType padType = SDL_GAMEPAD_TYPE_UNKNOWN, bool isSteerAction = false) const
	{
		switch (kind)
		{
		case Kind::Key:       return SDL_GetScancodeName(key);
		case Kind::PadAxis:   return InputNames::displayNameForAxis(axis, padType, negate, isSteerAction);
		case Kind::PadButton: return InputNames::displayNameForButton(button, padType);
		case Kind::JoystickButton: return std::format("Button {}", joystickIndex);
		case Kind::JoystickAxis: return std::format("Axis {}{}", joystickIndex,
			positiveAxis ? " (calibrated)" : "");
		case Kind::JoystickHat: return std::format("Hat {} {}", joystickIndex, InputNames::displayNameForHat(hatMask));
		default:              return "";
		}
	}

	std::string iniName() const
	{
		switch (kind)
		{
		case Kind::Key:       return SDL_GetScancodeName(key);
		case Kind::PadAxis:   return InputNames::iniNameForAxis(axis);
		case Kind::PadButton: return InputNames::iniNameForButton(button);
		case Kind::JoystickButton: return std::format("Button-{}", joystickIndex);
		case Kind::JoystickAxis:
			return positiveAxis
				? std::format("Axis-{}@{}:{}", joystickIndex, joystickRest, joystickDirection > 0 ? "+" : "-")
				: std::format("Axis-{}", joystickIndex);
		case Kind::JoystickHat: return std::format("Hat-{}-{}", joystickIndex, InputNames::iniNameForHat(hatMask));
		default:              return "";
		}
	}
};

class InputAction
{
	std::vector<InputBinding> bindings_;
	InputState state_;

public:
	const InputState& update(SDL_Gamepad* primary_pad, SDL_Joystick* primary_joystick)
	{
		float maxValue = 0.0f;
		bool isAxisInput = false;
		InputSourceType lastSource = state_.lastSourceType;

		// Read all bindings and take the highest absolute value
		for (const auto& binding : bindings_)
		{
			float currentValue = binding.read(primary_pad, primary_joystick);
			if (std::abs(currentValue) > std::abs(maxValue))
			{
				maxValue = currentValue;
				isAxisInput = binding.isAxis();
				lastSource = binding.sourceType();
			}
		}

		state_.lastSourceType = lastSource;
		state_.isAxis = isAxisInput;
		state_.update(maxValue);
		return state_;
	}

	void add(const InputBinding& binding) { bindings_.push_back(binding); }

	void clear() { bindings_.clear(); }

	std::vector<InputBinding>& bindings() { return bindings_; }
	const std::vector<InputBinding>& bindings() const { return bindings_; }

	const InputState& getState() const { return state_; }
	void setState(const InputState& state) { this->state_ = state; }
};

// ReadSwitch translates the raw DirectInput button mask into the SwitchId bits
// the rest of the game uses. Code that reads the raw mask instead needs to see
// the same presses, so this is that translation table, inverted.
struct RawButtonMapping
{
	uint32_t rawBit;
	SwitchId switchId;
};
inline constexpr RawButtonMapping RawButtonMap[] = {
	{ 0x00000001, SwitchId::Start          },
	{ 0x00000200, SwitchId::Back           },
	{ 0x00000002, SwitchId::A              },
	{ 0x00000004, SwitchId::B              },
	{ 0x00000008, SwitchId::X              },
	{ 0x00000010, SwitchId::Y              },
	{ 0x00000040, SwitchId::SelectionUp    },
	{ 0x00000020, SwitchId::SelectionDown  },
	{ 0x00000100, SwitchId::SelectionLeft  },
	{ 0x00000080, SwitchId::SelectionRight },
	{ 0x00000800, SwitchId::License        },
	{ 0x00000400, SwitchId::SignIn         },
	{ 0x08000000, SwitchId::Unknown0x200   },
	{ 0x00100000, SwitchId::Unknown0x100   },
};

// Two buttons that ReadSwitch has no entry for, so they reach the game only
// through the raw mask. A DirectInput pad reports the triggers here, and the
// Sumo car select screen toggles between its two car lists on the right one.
inline constexpr uint32_t RawTriggerLeft = 0x1000;
inline constexpr uint32_t RawTriggerRight = 0x2000;
inline constexpr float RawTriggerThreshold = 0.5f;

class InputManager
{
public:
	// Which of the three binding tables an action lives in.
	enum class ActionKind { Volume, Switch, Mod };

private:
	std::array<InputAction, size_t(ADChannel::Count)> volumeBindings;
	std::array<InputAction, size_t(SwitchId::Count)> switchBindings;
	std::array<InputAction, size_t(ModAction::Count)> modBindings;

	std::mutex mtx;
	std::vector<InputDevice> controllers;
	int primaryControllerIndex = -1;
	// Steering as read from the active binding, before the game's optional
	// sensitivity curve. Kept separately for telemetry; gameplay continues to
	// consume the existing volumes cache unchanged.
	float physicalSteering = 0.0f;

	SDL_Window* window = nullptr;

	// cached values as of last update call
	std::array<InputState, size_t(ADChannel::Count)> volumes;
	std::array<InputState, size_t(ModAction::Count)> modStates;

	// Mod actions are readable while the overlay is up, since the overlay toggle
	// has to be able to close it again, but not while the binding dialog owns
	// every input.
	bool modActionsDeaf = false;

	// Switch bitmasks. switch_current/_previous are what the game sees;
	// switch_overlay is the same data before game-side suppression, so the
	// overlay can still be driven while the game is deaf.
	uint32_t switch_current;
	uint32_t switch_previous;
	uint32_t switch_overlay;

	// Mirror of the raw DirectInput masks. Edges are tracked here rather than
	// read back out of the game's copy, because DInputUpdate rewrites the same
	// fields whenever a device is still being polled.
	uint32_t raw_buttons = 0;
	uint32_t raw_pressed = 0;
	uint32_t raw_released = 0;

	// Latched until the user releases everything - see update().
	// Previously function-local statics inside update().
	bool suppressOverlayUntilRelease = false;
	bool suppressGameUntilRelease = false;
	InputSourceType lastInputSource_ = InputSourceType::GamePad;

private:
	static inline const std::string volumeNames[] = {
		"Steering",
		"Acceleration",
		"Brake"
	};

	static inline const std::string switchNames[] = {
		"Start",
		"Back",
		"A",
		"B",
		"X",
		"Y",
		"Gear Down",
		"Gear Up",
		"Unk0x100",
		"Unk0x200",
		"Selection Up",
		"Selection Down",
		"Selection Left",
		"Selection Right",
		"License",
		"Sign In",
		"Unk0x10000",
		"Unk0x20000",
		"Change View"
	};

	static inline const std::string modNames[] = {
		"Overlay",
		"HUD Toggle",
		"Open Chat",
		"Music Next",
		"Music Previous"
	};
	static_assert(std::size(modNames) == size_t(ModAction::Count));

	// switchNames must stay 1:1 with SwitchId - the ini reader/writer index both
	// by the same value. (volumeNames deliberately does NOT match ADChannel:
	// the enum reserves 4 unused AD channels, so always bound by std::size.)
	static_assert(std::size(switchNames) == size_t(SwitchId::Count));

	static int Sumo_CalcSteerSensitivity_wrapper(int a1, int a2)
	{
		int returnValue;
		__asm {
			push ebx

			mov eax, a1
			mov ebx, a2

			call Game::Sumo_CalcSteerSensitivity

			mov returnValue, eax

			pop ebx
		}
		return returnValue;
	}

	void setupGamepad(SDL_Gamepad* controller)
	{
		Game::CurrentPadType = Game::GamepadType::Xbox;
		auto type = SDL_GetGamepadType(controller);
		switch (type)
		{
		case SDL_GAMEPAD_TYPE_PS3:
		case SDL_GAMEPAD_TYPE_PS4:
		case SDL_GAMEPAD_TYPE_PS5:
			Game::CurrentPadType = Game::GamepadType::PS;
			break;
		case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
		case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
		case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
		case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
			Game::CurrentPadType = Game::GamepadType::Switch;
			break;
		};
	}

	void onControllerAdded(SDL_JoystickID instanceId)
	{
		std::lock_guard<std::mutex> lock(mtx);
		if (std::find_if(controllers.begin(), controllers.end(), [instanceId](const InputDevice& device)
			{ return device.instanceId == instanceId; }) != controllers.end())
			return;

		InputDevice device;
		device.instanceId = instanceId;
		if (SDL_IsGamepad(instanceId))
		{
			device.kind = InputDeviceKind::Gamepad;
			device.gamepad = SDL_OpenGamepad(instanceId);
			if (!device.gamepad)
			{
				spdlog::error(__FUNCTION__ "({}): failed to open gamepad: {}", instanceId, SDL_GetError());
				return;
			}
			const char* name = SDL_GetGamepadName(device.gamepad);
			device.name = name ? name : "Unknown gamepad";
			spdlog::info("Input device: '{}' [Gamepad], instance {}", device.name, instanceId);
		}
		else
		{
			device.kind = InputDeviceKind::Joystick;
			device.joystick = SDL_OpenJoystick(instanceId);
			if (!device.joystick)
			{
				spdlog::error(__FUNCTION__ "({}): failed to open joystick: {}", instanceId, SDL_GetError());
				return;
			}
			const char* name = SDL_GetJoystickName(device.joystick);
			device.name = name ? name : "Unknown joystick";
			spdlog::info("Input device: '{}' [Joystick], instance {}, {} axes, {} buttons, {} hats",
				device.name, instanceId, SDL_GetNumJoystickAxes(device.joystick),
				SDL_GetNumJoystickButtons(device.joystick), SDL_GetNumJoystickHats(device.joystick));
		}

		controllers.push_back(std::move(device));

		// If we don't have primary already, set it as this
		if (primaryControllerIndex == -1)
			setPrimaryGamepad(controllers.size() - 1);
	}

	void onControllerRemoved(SDL_JoystickID instanceId)
	{
		spdlog::debug(__FUNCTION__ "(instance {})", instanceId);

		std::lock_guard<std::mutex> lock(mtx);

		const SDL_JoystickID previousPrimary = getPrimaryDevice() ? getPrimaryDevice()->instanceId : 0;
		auto it = std::find_if(controllers.begin(), controllers.end(), [instanceId](const InputDevice& device)
			{ return device.instanceId == instanceId; });

		if (it != controllers.end())
		{
			FFB::OnInputDeviceRemoved(instanceId);
			Game::CurrentPadType = Game::GamepadType::PC;

			const bool removedPrimary = previousPrimary == instanceId;
			if (it->gamepad)
				SDL_CloseGamepad(it->gamepad);
			if (it->joystick)
				SDL_CloseJoystick(it->joystick);
			controllers.erase(it);

			spdlog::debug(__FUNCTION__ "(instance {}): removed instance", instanceId);

			if (removedPrimary || controllers.empty())
				setPrimaryDevice(controllers.empty() ? -1 : 0);
			else
			{
				auto primary = std::find_if(controllers.begin(), controllers.end(), [previousPrimary](const InputDevice& device)
					{ return device.instanceId == previousPrimary; });
				setPrimaryDevice(primary == controllers.end() ? 0 : int(primary - controllers.begin()));
			}
		}
	}

public:
	// This singleton is destroyed by the CRT during DLL_PROCESS_DETACH, when
	// SDL's joystick backend globals may already be gone. Do not call SDL from
	// here; runtime device-removal events close handles in onControllerRemoved(),
	// and the OS reclaims any handles still open when the process exits.
	~InputManager() = default;

	SDL_Gamepad* getPrimaryGamepad()
	{
		if (primaryControllerIndex < 0)
			return nullptr;
		if (primaryControllerIndex >= controllers.size())
			return nullptr;
		return controllers[primaryControllerIndex].gamepad;
	}

	SDL_Joystick* getPrimaryJoystick()
	{
		if (primaryControllerIndex < 0 || primaryControllerIndex >= int(controllers.size()))
			return nullptr;
		return controllers[primaryControllerIndex].joystick;
	}

	InputDevice* getPrimaryDevice()
	{
		if (primaryControllerIndex < 0 || primaryControllerIndex >= int(controllers.size()))
			return nullptr;
		return &controllers[primaryControllerIndex];
	}

	void setPrimaryDevice(int index)
	{
		if (index < 0 || index >= int(controllers.size()))
			primaryControllerIndex = -1;
		else
			primaryControllerIndex = index;

		if (auto* device = getPrimaryDevice())
			spdlog::info("Primary input device: '{}' [{}], instance {}", device->name,
				device->kind == InputDeviceKind::Gamepad ? "Gamepad" : "Joystick", device->instanceId);
		else
			spdlog::info("Primary input device: none");

		if (auto* pad = getPrimaryGamepad())
			setupGamepad(pad);
		else
			Game::CurrentPadType = Game::GamepadType::PC;
	}

	void setPrimaryGamepad(int index) { setPrimaryDevice(index); }

	void init(HWND hwnd);

	// An ini written before an action existed has no lines for it, so it loads
	// unbound. That is recoverable for every action except the overlay toggle:
	// without it there is no way to reach the UI that would rebind it, so it
	// always gets its default back.
	void ensureOverlayBindable()
	{
		InputAction& overlay = modBindings[size_t(ModAction::OverlayToggle)];
		if (!overlay.bindings().empty())
			return;

		spdlog::warn(__FUNCTION__ ": overlay toggle had no bindings, restoring F11");
		addModBinding(ModAction::OverlayToggle, SDL_SCANCODE_F11);
	}

	void setupDefaultBindings()
	{
		// Remove any previous bindings
		for (auto& binding : volumeBindings)
			binding.clear();
		for (auto& binding : switchBindings)
			binding.clear();
		for (auto& binding : modBindings)
			binding.clear();

		// Keyboard
		addVolumeBinding(ADChannel::Steering, SDL_SCANCODE_LEFT, true);
		addVolumeBinding(ADChannel::Steering, SDL_SCANCODE_RIGHT);
		addVolumeBinding(ADChannel::Acceleration, SDL_SCANCODE_UP);
		addVolumeBinding(ADChannel::Brake, SDL_SCANCODE_DOWN);

		addSwitchBinding(SwitchId::GearUp, SDL_SCANCODE_W);
		addSwitchBinding(SwitchId::GearDown, SDL_SCANCODE_D);

		addSwitchBinding(SwitchId::Start, SDL_SCANCODE_ESCAPE);
		addSwitchBinding(SwitchId::Back, SDL_SCANCODE_ESCAPE);
		addSwitchBinding(SwitchId::A, SDL_SCANCODE_RETURN);
		addSwitchBinding(SwitchId::B, SDL_SCANCODE_ESCAPE);
		addSwitchBinding(SwitchId::X, SDL_SCANCODE_E);
		addSwitchBinding(SwitchId::Y, SDL_SCANCODE_F);

		addSwitchBinding(SwitchId::ChangeView, SDL_SCANCODE_F);

		addSwitchBinding(SwitchId::SelectionUp, SDL_SCANCODE_UP);
		addSwitchBinding(SwitchId::SelectionDown, SDL_SCANCODE_DOWN);
		addSwitchBinding(SwitchId::SelectionLeft, SDL_SCANCODE_LEFT);
		addSwitchBinding(SwitchId::SelectionRight, SDL_SCANCODE_RIGHT);

		addSwitchBinding(SwitchId::SignIn, SDL_SCANCODE_F1);
		addSwitchBinding(SwitchId::License, SDL_SCANCODE_F2);
		addSwitchBinding(SwitchId::X, SDL_SCANCODE_F1);
		addSwitchBinding(SwitchId::Y, SDL_SCANCODE_F2);

		// Gamepad
		addVolumeBinding(ADChannel::Steering, SDL_GAMEPAD_AXIS_LEFTX);
		addVolumeBinding(ADChannel::Steering, SDL_GAMEPAD_BUTTON_DPAD_LEFT, true);
		addVolumeBinding(ADChannel::Steering, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
		addVolumeBinding(ADChannel::Acceleration, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
		addVolumeBinding(ADChannel::Brake, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);

		addSwitchBinding(SwitchId::GearUp, SDL_GAMEPAD_AXIS_RIGHTY);
		addSwitchBinding(SwitchId::GearDown, SDL_GAMEPAD_AXIS_RIGHTY, true);
		addSwitchBinding(SwitchId::GearUp, SDL_GAMEPAD_BUTTON_A);
		addSwitchBinding(SwitchId::GearDown, SDL_GAMEPAD_BUTTON_B);

		addSwitchBinding(SwitchId::Start, SDL_GAMEPAD_BUTTON_START);
		addSwitchBinding(SwitchId::Back, SDL_GAMEPAD_BUTTON_BACK);
		addSwitchBinding(SwitchId::A, SDL_GAMEPAD_BUTTON_A);
		addSwitchBinding(SwitchId::B, SDL_GAMEPAD_BUTTON_B);
		addSwitchBinding(SwitchId::X, SDL_GAMEPAD_BUTTON_X);
		addSwitchBinding(SwitchId::Y, SDL_GAMEPAD_BUTTON_Y);

		addSwitchBinding(SwitchId::ChangeView, SDL_GAMEPAD_BUTTON_Y);

		addSwitchBinding(SwitchId::SelectionUp, SDL_GAMEPAD_AXIS_LEFTY, true);
		addSwitchBinding(SwitchId::SelectionDown, SDL_GAMEPAD_AXIS_LEFTY, false);
		addSwitchBinding(SwitchId::SelectionLeft, SDL_GAMEPAD_AXIS_LEFTX, true);
		addSwitchBinding(SwitchId::SelectionRight, SDL_GAMEPAD_AXIS_LEFTX, false);
		addSwitchBinding(SwitchId::SelectionUp, SDL_GAMEPAD_BUTTON_DPAD_UP);
		addSwitchBinding(SwitchId::SelectionDown, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
		addSwitchBinding(SwitchId::SelectionLeft, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
		addSwitchBinding(SwitchId::SelectionRight, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);

		// Some reason signin/license need both X/Y and SignIn/License bound, odd
		addSwitchBinding(SwitchId::SignIn, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
		addSwitchBinding(SwitchId::License, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
		addSwitchBinding(SwitchId::X, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
		addSwitchBinding(SwitchId::Y, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);

		// Mod actions.
		addModBinding(ModAction::OverlayToggle, SDL_SCANCODE_F11);
		addModBinding(ModAction::OpenChat, SDL_SCANCODE_Y);
		addModBinding(ModAction::MusicNext, SDL_SCANCODE_X);
		addModBinding(ModAction::MusicNext, SDL_GAMEPAD_BUTTON_BACK);
		addModBinding(ModAction::MusicPrevious, SDL_SCANCODE_Z);
	}

	//
	// INI parsing
	//
	struct IniEntry
	{
		std::string section;
		std::string key;
		std::string value;
	};

	// text -> entries. Knows nothing about actions or bindings.
	static std::vector<IniEntry> parseIniLines(std::istream& in)
	{
		std::vector<IniEntry> entries;
		std::string section;
		std::string line;

		while (std::getline(in, line))
		{
			line = Util::trim(line);
			if (line.empty() || line.front() == '#' || line.front() == ';')
				continue;

			if (line.front() == '[' && line.back() == ']')
			{
				section = line.substr(1, line.size() - 2);
				continue;
			}

			auto delim = line.find('=');
			if (delim == std::string::npos)
				continue;

			entries.push_back({ section,
				Util::trim(line.substr(0, delim)),
				Util::trim(line.substr(delim + 1)) });
		}

		return entries;
	}

	struct ActionRef
	{
		using Kind = ActionKind;

		Kind kind = Kind::Switch;
		int index = -1;
		bool negate = false;
	};

	// "Steering-" -> { volume, 0, negated }
	static std::optional<ActionRef> parseActionName(std::string_view name)
	{
		ActionRef ref;

		if (!name.empty() && name.back() == '-')
		{
			ref.negate = true;
			name.remove_suffix(1);
		}

		const std::string trimmed(name);

		for (int i = 0; i < int(std::size(volumeNames)); i++)
			if (!stricmp(trimmed.c_str(), volumeNames[i].c_str()))
			{
				ref.kind = ActionRef::Kind::Volume;
				ref.index = i;
				return ref;
			}

		for (int i = 0; i < int(SwitchId::Count); i++)
			if (!stricmp(trimmed.c_str(), switchNames[i].c_str()))
			{
				ref.kind = ActionRef::Kind::Switch;
				ref.index = i;
				return ref;
			}

		for (int i = 0; i < int(ModAction::Count); i++)
			if (!stricmp(trimmed.c_str(), modNames[i].c_str()))
			{
				ref.kind = ActionRef::Kind::Mod;
				ref.index = i;
				return ref;
			}

		return std::nullopt;
	}

	// "LS-X" / "Return" -> InputBinding (negate applied by the caller)
	static std::optional<InputBinding> parseBindingValue(const std::string& value, InputDeviceKind profile, bool keyboard)
	{
		if (keyboard)
		{
			SDL_Scancode scancode = SDL_GetScancodeFromName(value.c_str());
			if (scancode == SDL_SCANCODE_UNKNOWN)
				return std::nullopt;
			return InputBinding(scancode);
		}

		if (profile == InputDeviceKind::Gamepad)
		{
			if (auto axis = InputNames::axisFromIni(value))
				return InputBinding(*axis);
			if (auto button = InputNames::buttonFromIni(value))
				return InputBinding(*button);
			return std::nullopt;
		}

		int index = -1;
		int rest = 0;
		char direction = '+';
		if (sscanf_s(value.c_str(), "Axis-%d@%d:%c", &index, &rest, &direction, 1) == 3 && index >= 0 &&
			rest >= -32768 && rest <= 32767 && (direction == '+' || direction == '-'))
			return InputBinding::joystickAxis(index, true, Sint16(rest), direction == '+' ? 1 : -1);
		if (sscanf_s(value.c_str(), "Axis-%d", &index) == 1 && index >= 0)
			return InputBinding::joystickAxis(index, false);
		if (sscanf_s(value.c_str(), "Button-%d", &index) == 1 && index >= 0)
			return InputBinding::joystickButton(index);
		if (value.starts_with("Hat-"))
		{
			const size_t separator = value.find('-', 4);
			if (separator != std::string::npos)
			{
				const std::string indexText = value.substr(4, separator - 4);
				char* end = nullptr;
				const long parsed = std::strtol(indexText.c_str(), &end, 10);
				if (end != indexText.c_str() && *end == '\0' && parsed >= 0)
					if (auto mask = InputNames::hatFromIni(value.substr(separator + 1)))
						return InputBinding::joystickHat(int(parsed), *mask);
			}
		}

		return std::nullopt;
	}

	void addBinding(const ActionRef& ref, const InputBinding& binding)
	{
		switch (ref.kind)
		{
		case ActionRef::Kind::Volume: volumeBindings[ref.index].add(binding); break;
		case ActionRef::Kind::Mod:    modBindings[ref.index].add(binding); break;
		default:                      switchBindings[ref.index].add(binding); break;
		}
	}

	bool readBindingIni(const std::filesystem::path& iniPath)
	{
		if (!std::filesystem::exists(iniPath))
			return false;

		spdlog::info(__FUNCTION__ " - reading INI from {}", iniPath.string());

		std::ifstream file(iniPath);
		if (!file || !file.is_open())
		{
			spdlog::error(__FUNCTION__ " - failed to read INI, using defaults");
			return false;
		}

		const auto entries = parseIniLines(file);
		file.close();

		int key_binds = 0;
		int pad_binds = 0;
		int joystick_binds = 0;
		for (const auto& entry : entries)
		{
			if (!stricmp(entry.section.c_str(), "Keyboard"))
				key_binds++;
			else if (!stricmp(entry.section.c_str(), "Gamepad"))
				pad_binds++;
			else if (!stricmp(entry.section.c_str(), "Joystick"))
				joystick_binds++;
		}

		if (key_binds <= 0 && pad_binds <= 0 && joystick_binds <= 0)
		{
			spdlog::error(__FUNCTION__ " - failed to read binds from INI, using defaults");
			return false;
		}

		spdlog::info(__FUNCTION__ " - {} key binds, {} gamepad binds, {} joystick binds",
			key_binds, pad_binds, joystick_binds);

		// we have binds, reset any of our defaults
		for (auto& binding : volumeBindings)
			binding.clear();
		for (auto& binding : switchBindings)
			binding.clear();
		for (auto& binding : modBindings)
			binding.clear();

		for (const auto& entry : entries)
		{
			const bool keyboard = !stricmp(entry.section.c_str(), "Keyboard");
			const bool joystick = !stricmp(entry.section.c_str(), "Joystick");
			if (!keyboard && !joystick && stricmp(entry.section.c_str(), "Gamepad"))
				continue; // unknown section

			auto action = parseActionName(entry.key);
			if (!action)
			{
				spdlog::error(__FUNCTION__ ": failed to parse binding action for {} = {}", entry.key, entry.value);
				continue;
			}

			auto binding = parseBindingValue(entry.value,
				joystick ? InputDeviceKind::Joystick : InputDeviceKind::Gamepad, keyboard);
			if (!binding)
			{
				spdlog::error(__FUNCTION__ ": failed to parse binding for {} = {}", entry.key, entry.value);
				continue;
			}

			binding->negate = action->negate;
			addBinding(*action, *binding);
		}

		return true;
	}

	// Writes every binding of one source kind (pad or keyboard) for one section.
	void writeBindingSection(std::ostream& file, InputBinding::Kind profileKind)
	{
		auto writeAction = [&](const std::string& name, const InputAction& action)
		{
			for (const auto& bind : action.bindings())
			{
				const bool matches = profileKind == InputBinding::Kind::Key ? bind.isKeyboard()
					: profileKind == InputBinding::Kind::JoystickAxis ? bind.isJoystick() : bind.isGamepad();
				if (!matches)
					continue;

				const std::string direction = bind.isNegated() ? "-" : "";
				file << name << direction << " = " << bind.iniName() << "\n";
			}
		};

		for (int i = 0; i < int(std::size(volumeNames)); ++i)
			writeAction(volumeNames[i], volumeBindings[i]);

		for (int i = 0; i < int(SwitchId::Count); ++i)
			writeAction(switchNames[i], switchBindings[i]);

		for (int i = 0; i < int(ModAction::Count); ++i)
			writeAction(modNames[i], modBindings[i]);
	}

	bool saveBindingIni(const std::filesystem::path& iniPath)
	{
		std::ofstream file(iniPath, std::ios::out | std::ios::trunc);
		if (!file.is_open())
		{
			spdlog::error(__FUNCTION__ ": failed to open file for writing: {}", iniPath.string());
			return false;
		}

		file << "# These bindings are used when UseNewInput is enabled inside OutRun2006Tweaks.ini\n";
		file << "# With that enabled, you can use in-game Controls > Configuration dialog to change these during gameplay\n";
		file << "# (editing this file manually can allow more advanced config, such as binding multiple inputs to a single action)\n";
		file << "# If this file doesn't exist or is empty, bindings will be reset to default.\n";
		file << "#\n";
		file << "# Actions with a negative symbol after them ('Steering-') either treat the input as a negative value, or only trigger the action on negative inputs\n";
		file << "# Analog actions bound to digital inputs, eg. 'Steering- = DPad-Left', will make DPad-Left send a negative Steering value, making it move to the left\n";
		file << "# Digital actions bound to analog inputs, eg. 'Gear Down- = RS-Y', will only trigger the action when RS-Y is negative\n";
		file << "# Analog -> analog actions can also be inverted by adding a negative to them\n\n";

		file << "[Gamepad]\n";
		writeBindingSection(file, InputBinding::Kind::PadAxis);

		file << "\n[Joystick]\n";
		writeBindingSection(file, InputBinding::Kind::JoystickAxis);

		file << "\n[Keyboard]\n";
		writeBindingSection(file, InputBinding::Kind::Key);

		file.close();
		spdlog::info(__FUNCTION__": saved to INI file: {}", iniPath.string());

		return true;
	}

	void pumpSdlEvents()
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
			switch (event.type)
			{
			case SDL_EVENT_GAMEPAD_ADDED:
				onControllerAdded(event.gdevice.which);
				break;
			case SDL_EVENT_JOYSTICK_ADDED:
				onControllerAdded(event.jdevice.which);
				break;
			case SDL_EVENT_GAMEPAD_REMOVED:
				onControllerRemoved(event.gdevice.which);
				break;
			case SDL_EVENT_JOYSTICK_REMOVED:
				onControllerRemoved(event.jdevice.which);
				break;
			case SDL_EVENT_QUIT:
				PostQuitMessage(0);
				break;
			}
	}

	// Reads every volume binding into the cache the game later queries.
	// Skipped entirely while the binding dialog is up, so a stick being waved
	// around to pick a bind doesn't steer the car.
	void updateVolumes(SDL_Gamepad* gamepad, SDL_Joystick* joystick)
	{
		for (size_t i = 0; i < volumeBindings.size(); ++i)
		{
			auto& vol = volumeBindings[i].update(gamepad, joystick);
			if (i == int(ADChannel::Steering))
				physicalSteering = std::clamp(vol.currentValue, -1.0f, 1.0f);

			if (Overlay::IsBindingDialogActive || Overlay::IsActive) [[unlikely]]
				continue;

			volumes[i] = vol;

			// Steering runs through the game's own sensitivity curve so that
			// the in-game sensitivity setting still applies.
			if (i == 0 && !Settings::BypassGameSensitivity)
			{
				int cur = ceil(volumes[i].currentValue * 127.0f);
				int prev = ceil(volumes[i].previousValue * 127.0f);
				volumes[i].currentValue = Sumo_CalcSteerSensitivity_wrapper(cur, prev) / 127.0f;
				volumeBindings[i].setState(volumes[i]);
			}
		}
	}

	// Collapses the switch bindings into a bitmask, one bit per SwitchId.
	uint32_t readSwitchMask(SDL_Gamepad* gamepad, SDL_Joystick* joystick)
	{
		uint32_t mask = 0;
		for (size_t i = 0; i < switchBindings.size(); ++i)
		{
			auto& switchState = switchBindings[i].update(gamepad, joystick);
			if (switchState.isPressed())
			{
				mask |= (1 << i);
				lastInputSource_ = switchState.lastSourceType;
			}
		}
		return mask;
	}

	void update()
	{
		pumpSdlEvents();

		auto* gamepad = getPrimaryGamepad();
		auto* joystick = getPrimaryJoystick();

		switch_previous = switch_current;

		// Whatever opened the binding dialog (or started a listen) is still
		// physically held down right now. Passing that press on would instantly
		// re-trigger whatever we just opened, so latch a suppression flag and
		// swallow input until the user has released everything.
		if (isListeningForInput == ListenState::Listening)
			suppressOverlayUntilRelease = true;
		if (Overlay::IsBindingDialogActive)
			suppressGameUntilRelease = true;

		updateVolumes(gamepad, joystick);
		switch_current = readSwitchMask(gamepad, joystick);

		// Everything released - safe to start passing input on again.
		if (switch_current == 0) [[likely]]
		{
			suppressOverlayUntilRelease = false;
			suppressGameUntilRelease = false;
		}

		// Two consumers, suppressed independently. The overlay is masked first,
		// so anything hidden from the overlay is hidden from the game as well.
		if (suppressOverlayUntilRelease) [[unlikely]]
			switch_current = 0;

		switch_overlay = switch_current;

		// The overlay being open used to stop update() running at all, which is
		// what kept the game from seeing input through it. Mod actions have to be
		// read while it is open, so it runs either way now and the game is held
		// off here instead.
		if (suppressGameUntilRelease || Overlay::IsBindingDialogActive || Overlay::IsActive) [[unlikely]]
			switch_current = 0;

		updateModActions(gamepad, joystick);

		updateRawDInputState();
	}

	// Mod actions take the overlay's suppression but not the game's, so the
	// overlay toggle can still close the overlay. The binding dialog owns every
	// input while it is up, so nothing fires under it. States keep updating
	// regardless, so a key held across the dialog closing reads as held rather
	// than newly pressed.
	void updateModActions(SDL_Gamepad* gamepad, SDL_Joystick* joystick)
	{
		modActionsDeaf = suppressOverlayUntilRelease || Overlay::IsBindingDialogActive;

		for (size_t i = 0; i < modBindings.size(); ++i)
			modStates[i] = modBindings[i].update(gamepad, joystick);
	}

	// Rebuilds the raw DirectInput masks from the bindings. Called once per
	// update so that a held button produces a single press edge.
	void updateRawDInputState()
	{
		uint32_t buttons = 0;
		for (const auto& mapping : RawButtonMap)
			if (switch_current & (1u << int(mapping.switchId)))
				buttons |= mapping.rawBit;

		if (volumes[int(ADChannel::Acceleration)].currentValue >= RawTriggerThreshold)
			buttons |= RawTriggerRight;
		if (volumes[int(ADChannel::Brake)].currentValue >= RawTriggerThreshold)
			buttons |= RawTriggerLeft;

		raw_pressed = buttons & ~raw_buttons;
		raw_released = raw_buttons & ~buttons;
		raw_buttons = buttons;

		applyRawDInputState();
	}

	// Copies the cached masks into the game's state. Also called after ReadIO,
	// which rebuilds them from whatever device DInputUpdate polled. Copying
	// rather than recomputing means running twice in a frame cannot swallow a
	// press edge.
	void applyRawDInputState()
	{
		if (!Game::dinput_state)
			return;

		Game::dinput_state->buttons_4 = raw_buttons;
		Game::dinput_state->pressed_8 = raw_pressed;
		Game::dinput_state->released_C = raw_released;
	}

	// Table lookups by kind, for the bindings UI which walks all three.
	InputAction& actionFor(ActionKind kind, int index)
	{
		switch (kind)
		{
		case ActionKind::Volume: return volumeBindings[index];
		case ActionKind::Mod:    return modBindings[index];
		default:                 return switchBindings[index];
		}
	}

	static const std::string& actionName(ActionKind kind, int index)
	{
		switch (kind)
		{
		case ActionKind::Volume: return volumeNames[index];
		case ActionKind::Mod:    return modNames[index];
		default:                 return switchNames[index];
		}
	}

	// What to call a mod action's binding in a prompt. Prefers a keyboard one:
	// that is what a reader can press without a pad plugged in, and the pad
	// labels depend on which pad is connected.
	std::string modActionDisplayName(ModAction action)
	{
		const auto& bindings = modBindings[size_t(action)].bindings();
		if (bindings.empty())
			return "(unbound)";

		const InputBinding* pick = &bindings.front();
		for (const auto& binding : bindings)
			if (binding.isKeyboard())
			{
				pick = &binding;
				break;
			}

		auto padType = SDL_GAMEPAD_TYPE_XBOX360;
		if (auto* primary = getPrimaryGamepad())
			padType = SDL_GetGamepadType(primary);

		return pick->displayName(padType);
	}

	// Whether a mod action's binding is down right now. Deliberately not an edge:
	// this updates once per game tick while callers read on their own cadence,
	// which for the overlay is once per rendered frame, so an edge taken here
	// would be seen twice above tick rate and missed below it. Callers compare
	// against their own previous value instead, which is right at any rate.
	//
	// False while the binding dialog is up, so a key being bound never also fires
	// what it is bound to.
	bool modActionHeld(ModAction action) const
	{
		if (modActionsDeaf) [[unlikely]]
			return false;

		return modStates[size_t(action)].isPressed();
	}

	const InputAction& modAction(ModAction action) const { return modBindings[size_t(action)]; }
	InputAction& modAction(ModAction action) { return modBindings[size_t(action)]; }
	static const std::string& modActionName(ModAction action) { return modNames[size_t(action)]; }

	void setVibration(WORD left, WORD right)
	{
		auto* controller = getPrimaryGamepad();
		if (!controller)
			return;

		std::lock_guard<std::mutex> lock(mtx);
		SDL_RumbleGamepad(controller, left, right, 1000);

		// TODO: SDL_RumbleGamepadTriggers doesn't appear to work with any backend?
		// Disabling this code for now, we'll rely on the old ImpulseVibration / DetourDeviceIoControl method instead.
#if 0
		if (Settings::ImpulseVibrationMode != 0)
		{
			int impulseLeft = float(left);
			int impulseRight = float(right);

			if (Settings::ImpulseVibrationMode == 2) // Swap L/R
			{
				impulseLeft = float(right);
				impulseRight = float(left);
			}
			else if (Settings::ImpulseVibrationMode == 3) // Merge L/R by using whichever is highest
			{
				impulseLeft = impulseRight = max(left, right);
			}
			impulseLeft = impulseLeft * Settings::ImpulseVibrationLeftMultiplier;
			impulseRight = impulseRight * Settings::ImpulseVibrationRightMultiplier;
			SDL_RumbleGamepadTriggers(controller, Uint16(ceil(impulseLeft)), Uint16(ceil(impulseRight)), 1000);
		}
#endif
	}

	// Add sources to bindings
	template <typename... Args>
	void addVolumeBinding(ADChannel id, Args&&... args)
	{
		volumeBindings[int(id)].add(InputBinding(std::forward<Args>(args)...));
	}

	template <typename... Args>
	void addSwitchBinding(SwitchId id, Args&&... args)
	{
		switchBindings[int(id)].add(InputBinding(std::forward<Args>(args)...));
	}
	template <typename... Args>
	void addModBinding(ModAction id, Args&&... args)
	{
		modBindings[int(id)].add(InputBinding(std::forward<Args>(args)...));
	}

	bool anyInputPressed()
	{
		int key_count = 0;
		const bool* key_state = SDL_GetKeyboardState(&key_count);
		for (int i = 0; i < key_count; i++)
			if (key_state[i])
				return true;

		auto* controller = getPrimaryGamepad();
		auto* joystick = getPrimaryJoystick();

		if (controller)
		{
			for (int i = SDL_GAMEPAD_BUTTON_SOUTH; i < SDL_GAMEPAD_BUTTON_COUNT; i++)
				if (SDL_GetGamepadButton(controller, static_cast<SDL_GamepadButton>(i)))
					return true;

			for (int i = SDL_GAMEPAD_AXIS_LEFTX; i < SDL_GAMEPAD_AXIS_COUNT; i++)
			{
				float value = SDL_GetGamepadAxis(controller, static_cast<SDL_GamepadAxis>(i)) / 32768.0f;
				if (std::abs(value) > 0.5f)
					return true;
			}
		}

		if (joystick)
		{
			for (int i = 0; i < SDL_GetNumJoystickButtons(joystick); ++i)
				if (SDL_GetJoystickButton(joystick, i))
					return true;
			for (int i = 0; i < SDL_GetNumJoystickHats(joystick); ++i)
				if (SDL_GetJoystickHat(joystick, i) != SDL_HAT_CENTERED)
					return true;
		}

		return false;
	}

	InputSourceType lastInputSource() { return lastInputSource_; }

	//
	// Handlers for games original input functions
	//
	int GetVolume(ADChannel volumeId)
	{
		const auto& state = volumes[int(volumeId)];
		if (volumeId == ADChannel::Steering)
			return int(ceil(state.currentValue * 127.0f));

		return int(ceil(state.currentValue * 255.0f));
	}

	int GetVolumeOld(ADChannel volumeId)
	{
		const auto& state = volumes[int(volumeId)];
		if (volumeId == ADChannel::Steering)
			return int(ceil(state.previousValue * 127.0f));

		return int(ceil(state.previousValue * 255.0f));
	}

	float physicalSteeringNormalized() const
	{
		return physicalSteering;
	}

	bool SwitchOn(uint32_t switches)
	{
		return (switch_previous & switches) != switches && (switch_current & switches) == switches;
	}

	bool SwitchNow(uint32_t switches)
	{
		return (switch_current & switches) == switches;
	}

	friend class InputBindingsUI;

	static InputManager instance;
};

constexpr uint32_t StartSwitchMask = 1 << int(SwitchId::Start);

void InputManager_Update();
bool InputManager_ModActionHeld(ModAction action);
std::string InputManager_ModActionDisplayName(ModAction action);
void InputManager_SetVibration(WORD left, WORD right);
float InputManager_GetPhysicalSteering();
SDL_Joystick* InputManager_GetPrimaryJoystick();
SDL_JoystickID InputManager_GetPrimaryJoystickId();
