#include "NyxPCH.h"
#include "AmbientOcclusion.h"

#include "EnvironmentComponent.h"

#include <algorithm>
#include <cmath>

namespace Nyx
{
	glm::uvec2 GetAmbientOcclusionSize(uint32_t width, uint32_t height)
	{
		return glm::max(glm::uvec2(width / 2, height / 2), glm::uvec2(1u));
	}

	glm::uvec2 GetAmbientOcclusionSamples(EAmbientOcclusionQuality quality)
	{
		switch (quality)
		{
		case EAmbientOcclusionQuality::Low:
			return glm::uvec2(2, 4);
		case EAmbientOcclusionQuality::High:
			return glm::uvec2(4, 8);
		case EAmbientOcclusionQuality::Medium:
		default:
			return glm::uvec2(3, 6);
		}
	}

	SceneViewAmbientOcclusionSettings MakeSceneViewAmbientOcclusionSettings(const EnvironmentComponent& environment)
	{
		SceneViewAmbientOcclusionSettings occlusion;
		occlusion.bAmbientOcclusion = environment.bAmbientOcclusion && environment.OcclusionIntensity > 0.0f && environment.OcclusionRadius > 0.0f;
		occlusion.Radius = std::clamp(environment.OcclusionRadius, 0.01f, 100.0f);
		occlusion.Intensity = std::clamp(environment.OcclusionIntensity, 0.0f, 8.0f);
		const glm::uvec2 samples = GetAmbientOcclusionSamples(environment.OcclusionQuality);
		occlusion.Slices = samples.x;
		occlusion.Steps = samples.y;
		return occlusion;
	}

	float GetSliceVisibility(float n, float h0, float h1)
	{
		constexpr float HalfPi = 1.57079632679490f;
		h0 = n + std::clamp(h0 - n, -HalfPi, HalfPi);
		h1 = n + std::clamp(h1 - n, -HalfPi, HalfPi);
		const float cosN = std::cos(n);
		const float sinN = std::sin(n);
		const float arc0 = (cosN + 2.0f * h0 * sinN - std::cos(2.0f * h0 - n)) * 0.25f;
		const float arc1 = (cosN + 2.0f * h1 * sinN - std::cos(2.0f * h1 - n)) * 0.25f;
		return arc0 + arc1;
	}
}
