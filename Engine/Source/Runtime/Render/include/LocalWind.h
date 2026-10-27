#pragma once

#include "WindField.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace Nyx
{
	// The texture of a game's local air round a view's camera, as plain math (Docs/Design/wind.md):
	// where its cells are, and their air as the GPU samples it. Grass and trees bend in it on top of
	// the world's wind (nyx.worldwind's nyx::localWindAt()). No GPU, so tests check it as the renderer
	// fills it (VulkanLocalWind).

	// How big the texture is: its cells along x and z, its layers, and a cell's size in metres
	struct LocalWindSettings
	{
		int32_t Cells = 48;
		int32_t Layers = 12;
		float CellSize = 1.0f;
	};

	// The settings within what the texture takes: 8 to 128 cells, 1 to 32 layers, cells of 5 cm to
	// 64 m
	LocalWindSettings ClampLocalWindSettings(const LocalWindSettings& settings);

	// Where the texture's cells are for a camera at `camera`: centred on it along the ground, from
	// two thirds of its height below it to a third above (what bends is mostly below the eye), its
	// corner on a whole cell of the world, so the cells don't swim as the camera moves
	WindFieldGrid PlaceLocalWindGrid(const glm::vec3& camera, const LocalWindSettings& settings);

	// The air of every cell (in the grid's order) as the texture holds it, four half floats a texel
	// (x, y, z, 0); returns whether any is more than still
	bool PackLocalWind(std::span<const glm::vec3> velocities, std::vector<uint16_t>& outTexels);
}
