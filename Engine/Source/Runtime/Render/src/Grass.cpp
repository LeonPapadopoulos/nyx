#include "NyxPCH.h"
#include "Grass.h"

#include "GrassFieldComponent.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Nyx
{
	namespace
	{
		constexpr float MinDensity = 1.0f;
		constexpr float MaxDensity = 200.0f;
		constexpr float MaxRange = 500.0f;
		// Where, as a share of Range, the last blades begin to go
		constexpr float LastBladesFrom = 0.7f;
		constexpr uint32_t MaterialBits = 32;

		float SlopeCosine(float degrees)
		{
			return std::cos(std::clamp(degrees, 0.0f, 90.0f) * std::numbers::pi_v<float> / 180.0f);
		}
	}

	GrassSettings MakeGrassSettings(const GrassFieldComponent& component)
	{
		GrassSettings settings;
		settings.bGrass = component.bGrass;
		settings.Seed = component.Seed;
		settings.CellSize = 1.0f / std::sqrt(std::clamp(component.Density, MinDensity, MaxDensity));
		settings.Range = std::clamp(component.Range, 1.0f, MaxRange);
		settings.FullDensityDistance = std::clamp(component.FullDensityDistance, 0.5f, settings.Range);
		settings.NormalYRange = glm::vec2(SlopeCosine(std::max(component.SlopeRange.x, component.SlopeRange.y)), SlopeCosine(std::min(component.SlopeRange.x, component.SlopeRange.y)));
		settings.HeightRange = glm::vec2(std::min(component.HeightRange.x, component.HeightRange.y), std::max(component.HeightRange.x, component.HeightRange.y));
		if (!component.TerrainMaterials.empty())
		{
			settings.MaterialMask = 0;
			for (const uint32_t material : component.TerrainMaterials)
			{
				if (material < MaterialBits)
				{
					settings.MaterialMask |= 1u << material;
				}
			}
		}
		return settings;
	}

	float GetGrassDrawnShare(const GrassSettings& settings, float distance)
	{
		if (distance >= settings.Range)
		{
			return 0.0f;
		}
		const float falling = std::min(settings.FullDensityDistance / std::max(distance, 1.0e-3f), 1.0f);
		const float last = std::clamp((settings.Range - distance) / (settings.Range * (1.0f - LastBladesFrom)), 0.0f, 1.0f);
		return falling * last * last * (3.0f - 2.0f * last);
	}

	uint32_t GetGrassGridLevelForShare(float share)
	{
		uint32_t level = 0;
		float next = 0.25f;
		while (level + 1 < GrassGridLevels && share <= next)
		{
			++level;
			next *= 0.25f;
		}
		return level;
	}

	uint32_t GetGrassCellLevel(int32_t column, int32_t row)
	{
		const uint32_t bits = static_cast<uint32_t>(column) | static_cast<uint32_t>(row);
		uint32_t level = 0;
		while (level + 1 < GrassGridLevels && (bits & (1u << level)) == 0)
		{
			++level;
		}
		return level;
	}

	float GetGrassRank(uint32_t level, float random)
	{
		const float top = 1.0f / static_cast<float>(1u << (2 * level));
		const float bottom = level + 1 < GrassGridLevels ? top * 0.25f : 0.0f;
		return bottom + (top - bottom) * random;
	}
}
