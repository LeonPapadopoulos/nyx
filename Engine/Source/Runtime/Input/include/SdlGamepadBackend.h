#pragma once

#include "InputBackend.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

struct SDL_Gamepad;

namespace Nyx::Input
{
	// Gamepads through SDL3, with only its gamepad parts: Xbox pads (XInput, Windows.Gaming.Input,
	// GameInput), DualShock 4 and DualSense (HIDAPI), and whatever else SDL knows. SDL has no window
	// here; GLFW keeps it, and the input system decides about focus (the Unfocused context), so pads
	// report while another window has the focus.
	//
	// Buttons come as SDL events, so presses shorter than a frame aren't lost; sticks and triggers
	// as events too; gyro and accelerometer once per frame, their latest values.
	class SdlGamepadBackend final : public IInputBackend
	{
	public:
		SdlGamepadBackend();
		~SdlGamepadBackend() override;

		SdlGamepadBackend(const SdlGamepadBackend&) = delete;
		SdlGamepadBackend& operator=(const SdlGamepadBackend&) = delete;

		// False if SDL couldn't start its gamepad subsystem; the backend then reports nothing
		bool IsAvailable() const
		{
			return bAvailable;
		}

		const char* GetName() const override
		{
			return "SDL";
		}

		void Poll(std::vector<InputEvent>& outEvents) override;
		void ApplyOutput(InputDeviceId device, const DeviceOutput& output) override;

	private:
		struct Pad
		{
			SDL_Gamepad* Handle = nullptr;
			uint32_t SdlId = 0;
			InputDeviceInfo Info;
			glm::vec3 LeftStick{ 0.0f };
			glm::vec3 RightStick{ 0.0f };
			glm::vec3 Gyro{ 0.0f };
			glm::vec3 Accel{ 0.0f };
			bool bGyroChanged = false;
			bool bAccelChanged = false;
		};

		Pad* FindPad(uint32_t sdlId);
		Pad* FindPad(InputDeviceId device);

		void OpenPad(uint32_t sdlId, uint64_t timeUs, std::vector<InputEvent>& outEvents);
		void ClosePad(uint32_t sdlId, uint64_t timeUs, std::vector<InputEvent>& outEvents);

		// An SDL event time (nanoseconds of SDL_GetTicksNS()) in microseconds of NowUs()
		uint64_t ToNowUs(uint64_t sdlNanoseconds) const;

		bool bAvailable = false;
		int64_t ClockOffsetUs = 0;
		std::vector<Pad> Pads;
	};
}
