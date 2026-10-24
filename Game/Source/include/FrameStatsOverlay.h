#pragma once

#include <glm/glm.hpp>

namespace Nyx
{
	class FrameStatsWindow;
}

namespace Nyx::Game
{
	// The frame's numbers over the game's picture, at `topLeft` in ImGui's screen coordinates: the
	// frame rate and time, the last second's average and worst, where the time went on the CPU and
	// the GPU, what was drawn, and a graph of the last seconds' frame times. Clicks go through it to
	// the game.
	void DrawFrameStatsOverlay(const Nyx::FrameStatsWindow& stats, const glm::vec2& topLeft);
}
