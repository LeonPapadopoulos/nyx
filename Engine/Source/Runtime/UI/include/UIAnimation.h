#pragma once

#include <imgui.h>

#include <cstddef>

namespace Nyx::UI
{
	// Values eased over frames, for the states of painted widgets: how hovered or pressed a
	// button is, where a toggle's knob is. Each id has its own value. It moves towards its target
	// once per frame, by the same share per second whatever the frame rate: after 1 / speed
	// seconds, about two thirds of the way are done.
	//
	// The first call for an id starts at the target, so nothing animates when a widget first
	// shows. Ids not asked for in AnimationForgetFrames frames are forgotten.
	//
	//     const float hovered = Nyx::UI::Animate(ImHashStr("Hovered", 0, id), bHovered ? 1.0f : 0.0f, theme.AnimationSpeed);
	float Animate(ImGuiID id, float target, float speed);

	constexpr int AnimationForgetFrames = 60;

	// How many values are kept, e.g. for tests
	size_t GetAnimationCount();
}
