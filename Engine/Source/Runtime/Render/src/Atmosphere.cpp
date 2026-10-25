#include "NyxPCH.h"
#include "Atmosphere.h"

#include "EnvironmentComponent.h"
#include "SkyAtmosphere.h"

#include <algorithm>
#include <cmath>

namespace Nyx
{
	namespace
	{
		constexpr float Pi = 3.14159265358979f;

		// What the air takes from the sun's red, green and blue a mass of air more than overhead:
		// about Rayleigh's scattering, which takes blue most, with a little haze
		constexpr glm::vec3 AirExtinction{ 0.05f, 0.11f, 0.25f };

		float SmoothStep(float edge0, float edge1, float x)
		{
			const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		}

		// The fog along a ray `reach` metres long that starts `startHeight` up and climbs `dirY` a
		// metre: the density integrated along it, exactly, as it thins (or thickens) exponentially
		// with the height. The exponents are held below 80, so a ray deep under the fog's height
		// is very deep, not infinite or nothing.
		float FogDepthAlong(float density, float falloff, float fogHeight, float startHeight, float dirY, float reach)
		{
			const float atStart = -falloff * (startHeight - fogHeight);
			const float x = falloff * dirY * reach;
			if (std::abs(x) < 1e-3f)
			{
				// (1 - e^-x) / x, near 1
				return density * std::exp(std::min(atStart, 80.0f)) * reach * (1.0f - x * 0.5f + x * x / 6.0f);
			}
			const float atEnd = atStart - x;
			const float larger = std::max(atStart, atEnd);
			return density * std::exp(std::min(larger, 80.0f)) * (std::exp(atStart - larger) - std::exp(atEnd - larger)) / (falloff * dirY);
		}
	}

	glm::vec3 GetSunTransmittance(float sinElevation, float reddening)
	{
		sinElevation = std::clamp(sinElevation, -1.0f, 1.0f);
		// Once it is below the horizon by more than its own glow, the sun is gone
		const float aboveHorizon = SmoothStep(-0.05f, 0.0f, sinElevation);
		if (reddening <= 0.0f)
		{
			return glm::vec3(aboveHorizon);
		}

		// Kasten and Young's air mass: 1 overhead, about 38 on the horizon; held there below it
		const float elevation = std::max(std::asin(sinElevation), 0.0f);
		const float degrees = elevation * 180.0f / Pi;
		const float airMass = 1.0f / (std::sin(elevation) + 0.50572f * std::pow(degrees + 6.07995f, -1.6364f));
		return glm::exp(-AirExtinction * reddening * std::max(airMass - 1.0f, 0.0f)) * aboveHorizon;
	}

	float GetDaylight(float sinElevation, float nightBrightness)
	{
		const float night = std::clamp(nightBrightness, 0.0f, 1.0f);
		return night + (1.0f - night) * SmoothStep(-0.1f, 0.2f, sinElevation);
	}

	glm::vec3 GetChromaticity(const glm::vec3& color)
	{
		const float luminance = glm::dot(glm::max(color, glm::vec3(0.0f)), glm::vec3(0.2126f, 0.7152f, 0.0722f));
		return luminance > 1e-6f ? glm::max(color, glm::vec3(0.0f)) / luminance : glm::vec3(1.0f);
	}

	SceneViewAtmosphere MakeSceneViewAtmosphere(const EnvironmentComponent& environment, const glm::vec3& towardsLight, const glm::vec3& lightColor, const glm::vec3& cameraPosition)
	{
		SceneViewAtmosphere atmosphere;
		atmosphere.SunColor = lightColor;
		const float sinElevation = std::clamp(towardsLight.y, -1.0f, 1.0f);
		const SceneViewSkyAtmosphere sky = MakeSceneViewSkyAtmosphere(environment, towardsLight, lightColor, cameraPosition);
		if (sky.bAtmosphere)
		{
			// The air's own: the sun's light through it to the camera; the sky and its light are what
			// it scatters, untinted, and its glow round the sun is in them
			atmosphere.SunColor = GetSunLightThroughAtmosphere(sky, lightColor);
		}
		else if (environment.bSkyFollowsSun)
		{
			atmosphere.SunColor = lightColor * GetSunTransmittance(sinElevation, std::max(environment.SunReddening, 0.0f));
			// The sky takes the sun's hue, not its brightness: that is the daylight's
			const glm::vec3 hue = GetChromaticity(atmosphere.SunColor);
			atmosphere.SkyTint = glm::mix(glm::vec3(1.0f), hue, std::clamp(environment.SunTint, 0.0f, 1.0f))
				* GetDaylight(sinElevation, environment.NightBrightness);
			atmosphere.SunGlowColor = hue * std::max(environment.SunGlow, 0.0f) * SmoothStep(-0.05f, 0.05f, sinElevation);
			atmosphere.SunGlowLobe = std::clamp(environment.SunGlowLobe, 0.0f, 0.99f);
		}

		if (environment.bFog && environment.FogDensity > 0.0f)
		{
			atmosphere.FogDensity = environment.FogDensity;
			atmosphere.FogHeightFalloff = std::max(environment.FogHeightFalloff, 0.0f);
			atmosphere.FogHeight = environment.FogHeight;
			atmosphere.FogStartDistance = std::max(environment.FogStartDistance, 0.0f);
			atmosphere.FogMaxOpacity = std::clamp(environment.FogMaxOpacity, 0.0f, 1.0f);
			atmosphere.FogColor = glm::max(environment.FogColor, glm::vec3(0.0f)) * atmosphere.SkyTint;
			atmosphere.FogSunColor = atmosphere.SunColor * std::max(environment.FogSunScattering, 0.0f);
			atmosphere.FogSunLobe = std::clamp(environment.FogSunLobe, 0.0f, 0.95f);
		}
		return atmosphere;
	}

	float GetHeightFogDepth(const SceneViewAtmosphere& atmosphere, const glm::vec3& from, const glm::vec3& to)
	{
		const glm::vec3 ray = to - from;
		const float distance = glm::length(ray);
		if (atmosphere.FogDensity <= 0.0f || distance <= atmosphere.FogStartDistance)
		{
			return 0.0f;
		}

		// From where the fog starts along the ray
		const glm::vec3 direction = ray / distance;
		const float startHeight = from.y + direction.y * atmosphere.FogStartDistance;
		return FogDepthAlong(atmosphere.FogDensity, atmosphere.FogHeightFalloff, atmosphere.FogHeight, startHeight, direction.y, distance - atmosphere.FogStartDistance);
	}

	float GetHeightFogTransmittance(const SceneViewAtmosphere& atmosphere, const glm::vec3& from, const glm::vec3& to)
	{
		return std::max(std::exp(-GetHeightFogDepth(atmosphere, from, to)), 1.0f - atmosphere.FogMaxOpacity);
	}

	glm::vec3 GetHeightFogInscattering(const SceneViewAtmosphere& atmosphere, const glm::vec3& direction, const glm::vec3& towardsLight)
	{
		return atmosphere.FogColor + atmosphere.FogSunColor * GetHenyeyGreenstein(glm::dot(direction, towardsLight), atmosphere.FogSunLobe);
	}

	float GetHenyeyGreenstein(float cosine, float g)
	{
		const float g2 = g * g;
		const float denominator = std::max(1.0f + g2 - 2.0f * g * cosine, 1e-6f);
		return (1.0f - g2) / (4.0f * Pi * denominator * std::sqrt(denominator));
	}
}
