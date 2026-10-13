#pragma once

#include "InputTypes.h"

#include <vector>

namespace Nyx::Input
{
	// Where input comes from: GLFW for keyboard and mouse, virtual devices for tests. A backend
	// collects what happens (devices added and removed, controls changing) and hands it over once
	// per frame.
	class IInputBackend
	{
	public:
		virtual ~IInputBackend() = default;

		// "GLFW", "Virtual"
		virtual const char* GetName() const = 0;

		// Appends what happened since the last call, in the order it happened
		virtual void Poll(std::vector<InputEvent>& outEvents) = 0;

		// Makes the cursor what the frame asked for. Called at the end of every frame; mostly
		// with what it was already.
		virtual void ApplyCursor(const CursorRequest& /*request*/)
		{
		}
	};
}
