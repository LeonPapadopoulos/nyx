#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace Nyx
{
	struct GrassFieldComponent;

	// The grass as plain math: its values in range, how many of its blades are drawn at a distance,
	// and which of them a tile keeps. No GPU, so tests check it as the renderer and the shaders use it.

	// The blades' grid is nested: of its cells, one in four is also a cell of the grid twice as wide,
	// and so on. A blade's rank says how far out it lasts: it is drawn where the share of the blades
	// drawn is above its rank. The cells of a wider grid have the lowest ranks, so the blades left at
	// any distance still lie evenly.
	inline constexpr uint32_t GrassGridLevels = 4;

	// The component's values, in range, as the renderer uses them
	struct GrassSettings
	{
		bool bGrass = true;
		uint32_t Seed = 1;
		// Metres between the cells of the blades' grid, one blade each, at the full density
		float CellSize = 0.13f;
		float FullDensityDistance = 20.0f;
		float Range = 100.0f;
		// The cosines of the slopes between which it grows: the normal's y, from the steepest up
		glm::vec2 NormalYRange{ 0.82f, 1.0f };
		glm::vec2 HeightRange{ -1000.0f, 1000.0f };
		// A bit for each material it grows on; all of them with none chosen
		uint32_t MaterialMask = ~0u;
	};

	GrassSettings MakeGrassSettings(const GrassFieldComponent& component);

	// The share of the blades drawn at a distance from the camera, 0 to 1: all up to Full Density
	// Distance, then falling as 1 / distance, which keeps about as many on each part of the picture,
	// and to none at Range
	float GetGrassDrawnShare(const GrassSettings& settings, float distance);

	// Which of the blades a tile whose nearest point has this share needs: only the cells of the grid
	// this many levels wider (0 all of them), each level a quarter of the last
	uint32_t GetGrassGridLevelForShare(float share);

	// The level of the grid a cell belongs to: the widest it is a cell of, up to the last
	uint32_t GetGrassCellLevel(int32_t column, int32_t row);

	// A blade's rank, 0 up to 1, from its level and a random number 0 up to 1: below 1 / 4^level and
	// at or above the next level's, so that a level's blades are the last to go
	float GetGrassRank(uint32_t level, float random);
}
