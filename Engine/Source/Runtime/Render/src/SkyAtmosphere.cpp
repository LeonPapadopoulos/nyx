#include "NyxPCH.h"
#include "SkyAtmosphere.h"

#include "Atmosphere.h"
#include "EnvironmentComponent.h"

#include <algorithm>
#include <cmath>

namespace Nyx
{
	namespace
	{
		constexpr float Pi = 3.14159265358979f;

		// How bright the night sky is at a Night Brightness of 1, as a radiance per unit of the sun's
		// illuminance: about a clear day sky's high up (its zenith about 0.004, its mean above the
		// horizon about 0.04)
		constexpr float NightSkyRadiance = 0.01f;

		// How far the sun may turn before the sky view is made again, as the length between its two
		// directions: about 0.02 degrees
		constexpr float SunTurnTolerance = 3.5e-4f;

		float SmoothStep(float edge0, float edge1, float x)
		{
			const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		}

		// What a kilometre of air `height` kilometres up takes from the light, scattered or absorbed
		glm::vec3 GetExtinction(const SceneViewSkyAtmosphere& sky, float height)
		{
			height = std::max(height, 0.0f);
			const float rayleigh = std::exp(-height / sky.RayleighHeight);
			const float mie = std::exp(-height / sky.MieHeight);
			const float ozone = std::max(0.0f, 1.0f - std::abs(height - OzoneCenterHeight) / OzoneHalfWidth);
			return sky.RayleighScattering * rayleigh + (sky.MieScattering + sky.MieAbsorption) * mie + sky.OzoneAbsorption * ozone;
		}

		// How far a ray from `height` up, `cosZenith` from the up axis, goes to the top of the air;
		// the planet's radius squared taken away exactly, by the heights
		float GetDistanceToTop(const SceneViewSkyAtmosphere& sky, float height, float cosZenith)
		{
			const float r = sky.PlanetRadius + height;
			const float b = r * cosZenith;
			// r² - top², negative inside the air
			const float c = (height - sky.AtmosphereHeight) * (2.0f * sky.PlanetRadius + height + sky.AtmosphereHeight);
			return std::max(-b + std::sqrt(std::max(b * b - c, 0.0f)), 0.0f);
		}

		bool HitsGround(const SceneViewSkyAtmosphere& sky, float height, float cosZenith)
		{
			return cosZenith < GetHorizonCosine(sky.PlanetRadius, height);
		}

		bool SameAir(const SceneViewSkyAtmosphere& a, const SceneViewSkyAtmosphere& b)
		{
			return a.bAtmosphere == b.bAtmosphere && a.RayleighScattering == b.RayleighScattering && a.RayleighHeight == b.RayleighHeight && a.MieScattering == b.MieScattering
				&& a.MieAbsorption == b.MieAbsorption && a.MieHeight == b.MieHeight && a.MieAnisotropy == b.MieAnisotropy && a.OzoneAbsorption == b.OzoneAbsorption
				&& a.GroundAlbedo == b.GroundAlbedo && a.PlanetRadius == b.PlanetRadius && a.AtmosphereHeight == b.AtmosphereHeight;
		}
	}

	SceneViewSkyAtmosphere MakeSceneViewSkyAtmosphere(const EnvironmentComponent& environment, const glm::vec3& towardsLight, const glm::vec3& lightColor,
		const glm::vec3& cameraPosition)
	{
		SceneViewSkyAtmosphere sky;
		sky.bAtmosphere = environment.bSkyFollowsSun && environment.SkyMode == ESkyMode::Atmosphere;
		if (!sky.bAtmosphere)
		{
			return sky;
		}

		sky.RayleighScattering = glm::max(environment.RayleighScattering, glm::vec3(0.0f)) * std::max(environment.RayleighScatteringScale, 0.0f);
		sky.RayleighHeight = std::max(environment.RayleighHeight, 0.01f);
		sky.MieScattering = glm::max(environment.MieScattering, glm::vec3(0.0f)) * std::max(environment.MieScatteringScale, 0.0f);
		sky.MieAbsorption = glm::vec3(std::max(environment.MieAbsorptionScale, 0.0f));
		sky.MieHeight = std::max(environment.MieHeight, 0.01f);
		sky.MieAnisotropy = std::clamp(environment.MieAnisotropy, 0.0f, 0.95f);
		sky.OzoneAbsorption = glm::max(environment.OzoneAbsorption, glm::vec3(0.0f)) * std::max(environment.OzoneAbsorptionScale, 0.0f);
		sky.GroundAlbedo = glm::clamp(environment.GroundAlbedo, glm::vec3(0.0f), glm::vec3(1.0f));
		sky.PlanetRadius = std::max(environment.PlanetRadius, 1.0f);
		sky.AtmosphereHeight = std::max(environment.AtmosphereHeight, 1.0f);

		sky.TowardsSun = glm::length(towardsLight) > 0.0f ? glm::normalize(towardsLight) : glm::vec3(0.0f, 1.0f, 0.0f);
		sky.SunIlluminance = Pi * glm::max(lightColor, glm::vec3(0.0f));
		// Metres to kilometres; from a metre up to just under the top of the air
		sky.CameraHeight = std::clamp(cameraPosition.y * 0.001f, 0.001f, sky.AtmosphereHeight * 0.999f);

		sky.WarmthTint = GetSkyWarmthTint(std::clamp(environment.SkyWarmth, -1.0f, 1.0f));
		// The night's faint light, blue as the air's scattering, as the sun goes down (GetDaylight()'s
		// dusk); as bright against a day sky as the Night Brightness says
		const float night = 1.0f - SmoothStep(-0.1f, 0.2f, sky.TowardsSun.y);
		const float sunLuminance = glm::dot(sky.SunIlluminance, glm::vec3(0.2126f, 0.7152f, 0.0722f));
		sky.NightLight = GetChromaticity(sky.RayleighScattering) * (std::clamp(environment.NightBrightness, 0.0f, 1.0f) * NightSkyRadiance * sunLuminance * night);

		const float radius = glm::radians(std::clamp(environment.SunDiskSize, 0.01f, 20.0f) * 0.5f);
		sky.SunDiskCosine = std::cos(radius);
		sky.SunDiskColor = glm::max(lightColor, glm::vec3(0.0f)) * std::max(environment.SunDiskBrightness, 0.0f);
		return sky;
	}

	glm::vec3 GetAtmosphereTransmittance(const SceneViewSkyAtmosphere& sky, float height, float cosZenith)
	{
		cosZenith = std::clamp(cosZenith, -1.0f, 1.0f);
		if (HitsGround(sky, height, cosZenith))
		{
			return glm::vec3(0.0f);
		}
		return GetAtmosphereTransmittance(sky, height, cosZenith, GetDistanceToTop(sky, height, cosZenith));
	}

	glm::vec3 GetAtmosphereTransmittance(const SceneViewSkyAtmosphere& sky, float height, float cosZenith, float distance)
	{
		cosZenith = std::clamp(cosZenith, -1.0f, 1.0f);
		const float r = sky.PlanetRadius + height;
		// r² - planet², exactly, by the height
		const float c = height * (2.0f * sky.PlanetRadius + height);
		glm::vec3 depth(0.0f);
		const float step = std::max(distance, 0.0f) / static_cast<float>(TransmittanceSteps);
		for (int index = 0; index < TransmittanceSteps; ++index)
		{
			// The step's middle, and its height: (|p|² - planet²) / (|p| + planet)
			const float t = (static_cast<float>(index) + 0.5f) * step;
			const float q = c + t * (t + 2.0f * r * cosZenith);
			const float length = std::sqrt(std::max(sky.PlanetRadius * sky.PlanetRadius + q, 0.0f));
			depth += GetExtinction(sky, q / (length + sky.PlanetRadius)) * step;
		}
		return glm::exp(-depth);
	}

	float GetHorizonCosine(float planetRadius, float height)
	{
		height = std::max(height, 0.0f);
		return -std::sqrt(height * (2.0f * planetRadius + height)) / (planetRadius + height);
	}

	glm::vec3 GetSunLightThroughAtmosphere(const SceneViewSkyAtmosphere& sky, const glm::vec3& lightColor)
	{
		// The disk sets over its own width: by the part of it above the horizon, coloured as the
		// air at the horizon leaves it
		const float horizon = GetHorizonCosine(sky.PlanetRadius, sky.CameraHeight);
		const float radius = std::acos(std::clamp(sky.SunDiskCosine, -1.0f, 1.0f));
		const float aboveHorizon = std::asin(std::clamp(sky.TowardsSun.y, -1.0f, 1.0f)) - std::asin(horizon);
		const float visible = SmoothStep(-radius, radius, aboveHorizon);
		if (visible <= 0.0f)
		{
			return glm::vec3(0.0f);
		}
		const float cosZenith = std::max(sky.TowardsSun.y, horizon + 1e-4f);
		return lightColor * GetAtmosphereTransmittance(sky, sky.CameraHeight, cosZenith) * visible;
	}

	glm::vec3 GetSkyWarmthTint(float warmth)
	{
		const glm::vec3 tint = glm::exp2(glm::vec3(0.5f, 0.0f, -0.5f) * warmth);
		return tint / glm::dot(tint, glm::vec3(0.2126f, 0.7152f, 0.0722f));
	}

	SkyAtmosphereChanges GetSkyAtmosphereChanges(const SceneViewSkyAtmosphere& built, const SceneViewSkyAtmosphere& wanted)
	{
		SkyAtmosphereChanges changes;
		changes.bAir = !SameAir(built, wanted);
		changes.bSkyView = changes.bAir || glm::length(built.TowardsSun - wanted.TowardsSun) > SunTurnTolerance || built.SunIlluminance != wanted.SunIlluminance
			|| built.WarmthTint != wanted.WarmthTint || built.NightLight != wanted.NightLight
			|| std::abs(built.CameraHeight - wanted.CameraHeight) > GetSkyViewHeightTolerance(built.CameraHeight);
		return changes;
	}

	float GetSkyViewHeightTolerance(float height)
	{
		return 0.001f + 0.01f * std::max(height, 0.0f);
	}
}
