#include "NyxPCH.h"
#include "LightShafts.h"

#include "Atmosphere.h"
#include "EnvironmentComponent.h"

#include <algorithm>
#include <cmath>

namespace Nyx
{
	namespace
	{
		float Smoothstep(float edge0, float edge1, float x)
		{
			const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		}

		float Luminance(const glm::vec3& color)
		{
			return glm::dot(glm::max(color, glm::vec3(0.0f)), glm::vec3(0.2126f, 0.7152f, 0.0722f));
		}
	}

	glm::uvec2 GetLightShaftsSize(uint32_t width, uint32_t height)
	{
		return glm::max(glm::uvec2(width / 2, height / 2), glm::uvec2(1u));
	}

	float GetLightShaftsScreenFade(const glm::vec2& ndc)
	{
		const float edge = std::max(std::abs(ndc.x), std::abs(ndc.y));
		return 1.0f - Smoothstep(1.0f, LightShaftsScreenFadeEnd, edge);
	}

	float GetLightShaftsSunFade(const glm::vec3& lightColor, const glm::vec3& sunLightColor)
	{
		const float given = Luminance(sunLightColor);
		if (given <= 1e-6f)
		{
			return 0.0f;
		}
		return Smoothstep(0.0f, LightShaftsSunFadeEnd, Luminance(lightColor) / given);
	}

	float GetLightShaftsDecay(float length, float density)
	{
		// decay^(samples · length / density) = the falloff: the samples cover `density` of the way
		return std::pow(LightShaftsTenthFalloff, density / (static_cast<float>(LightShaftsSamples) * std::max(length, 1e-3f)));
	}

	SceneViewLightShaftsSettings MakeSceneViewLightShaftsSettings(const EnvironmentComponent& environment, const glm::mat4& viewProjection, const glm::vec3& towardsLight,
		const glm::vec3& lightColor, const glm::vec3& sunLightColor)
	{
		SceneViewLightShaftsSettings shafts;
		shafts.bLightShafts = environment.bLightShafts && environment.LightShaftsIntensity > 0.0f;
		shafts.Intensity = std::clamp(environment.LightShaftsIntensity, 0.0f, 16.0f);
		shafts.Density = std::clamp(environment.LightShaftsDensity, 0.05f, 1.0f);
		shafts.Decay = GetLightShaftsDecay(std::clamp(environment.LightShaftsLength, 0.01f, 10.0f), shafts.Density);
		shafts.DepthThreshold = std::max(environment.LightShaftsDepthThreshold, 0.0f);
		if (!shafts.bLightShafts)
		{
			return shafts;
		}

		// Where the sun is on the picture: the point at infinity towards it, so the camera's place
		// doesn't matter, only where it looks. Behind the camera (or level with it), none.
		const glm::vec4 clip = viewProjection * glm::vec4(towardsLight, 0.0f);
		if (clip.w <= 1e-6f)
		{
			return shafts;
		}
		const glm::vec2 ndc = glm::vec2(clip) / clip.w;
		const float fade = GetLightShaftsScreenFade(ndc) * GetLightShaftsSunFade(lightColor, sunLightColor);
		shafts.SunUv = ndc * 0.5f + 0.5f;
		// The sun's colour as it reaches the camera, at luminance 1, tinted
		shafts.Color = GetChromaticity(lightColor) * glm::max(environment.LightShaftsTint, glm::vec3(0.0f)) * shafts.Intensity * fade;
		return shafts;
	}
}
