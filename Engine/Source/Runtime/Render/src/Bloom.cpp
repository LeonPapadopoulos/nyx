#include "NyxPCH.h"
#include "Bloom.h"

#include "EnvironmentComponent.h"

#include <algorithm>

namespace Nyx
{
	glm::uvec2 GetBloomLevelSize(uint32_t width, uint32_t height, uint32_t level)
	{
		const uint32_t shift = std::min(level + 1, 31u);
		return glm::max(glm::uvec2(width >> shift, height >> shift), glm::uvec2(1u));
	}

	uint32_t GetBloomLevelCount(uint32_t width, uint32_t height, uint32_t wanted)
	{
		wanted = std::clamp(wanted, 1u, MaxBloomLevels);
		uint32_t count = 1;
		while (count < wanted)
		{
			const glm::uvec2 next = GetBloomLevelSize(width, height, count);
			if (next.x < MinBloomLevelSize || next.y < MinBloomLevelSize)
			{
				break;
			}
			++count;
		}
		return count;
	}

	float GetBloomFactor(float brightness, float threshold, float knee)
	{
		knee = std::max(knee, 1e-4f);
		const float soft = std::clamp(brightness - threshold + knee, 0.0f, 2.0f * knee);
		const float curve = soft * soft / (4.0f * knee + 1e-5f);
		return std::max(curve, brightness - threshold) / std::max(brightness, 1e-5f);
	}

	SceneViewBloomSettings MakeSceneViewBloomSettings(const EnvironmentComponent& environment)
	{
		SceneViewBloomSettings bloom;
		bloom.bBloom = environment.bBloom && environment.BloomIntensity > 0.0f;
		bloom.Threshold = std::max(environment.BloomThreshold, 0.0f);
		bloom.Knee = std::max(environment.BloomKnee, 0.0f);
		bloom.Intensity = std::max(environment.BloomIntensity, 0.0f);
		bloom.Radius = std::clamp(environment.BloomRadius, 0.0f, 1.0f);
		bloom.Levels = static_cast<uint32_t>(std::clamp<int32_t>(environment.BloomLevels, 1, static_cast<int32_t>(MaxBloomLevels)));
		return bloom;
	}
}
