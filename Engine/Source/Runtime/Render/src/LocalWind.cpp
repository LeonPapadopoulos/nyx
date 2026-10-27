#include "NyxPCH.h"
#include "LocalWind.h"

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <cmath>

namespace Nyx
{
	LocalWindSettings ClampLocalWindSettings(const LocalWindSettings& settings)
	{
		LocalWindSettings clamped;
		clamped.Cells = std::clamp(settings.Cells, 8, 128);
		clamped.Layers = std::clamp(settings.Layers, 1, 32);
		clamped.CellSize = std::clamp(settings.CellSize, 0.05f, 64.0f);
		return clamped;
	}

	WindFieldGrid PlaceLocalWindGrid(const glm::vec3& camera, const LocalWindSettings& settings)
	{
		const LocalWindSettings clamped = ClampLocalWindSettings(settings);
		WindFieldGrid grid;
		grid.CellSize = clamped.CellSize;
		grid.Cells = glm::ivec3(clamped.Cells, clamped.Layers, clamped.Cells);
		const glm::vec3 below(0.5f * static_cast<float>(clamped.Cells), (2.0f / 3.0f) * static_cast<float>(clamped.Layers), 0.5f * static_cast<float>(clamped.Cells));
		grid.Origin = glm::floor(camera / clamped.CellSize - below) * clamped.CellSize;
		return grid;
	}

	bool PackLocalWind(std::span<const glm::vec3> velocities, std::vector<uint16_t>& outTexels)
	{
		outTexels.resize(velocities.size() * 4);
		bool bAny = false;
		const uint16_t zero = glm::packHalf1x16(0.0f);
		for (size_t index = 0; index < velocities.size(); ++index)
		{
			const glm::vec3& velocity = velocities[index];
			bAny = bAny || velocity != glm::vec3(0.0f);
			outTexels[index * 4 + 0] = glm::packHalf1x16(velocity.x);
			outTexels[index * 4 + 1] = glm::packHalf1x16(velocity.y);
			outTexels[index * 4 + 2] = glm::packHalf1x16(velocity.z);
			outTexels[index * 4 + 3] = zero;
		}
		return bAny;
	}
}
