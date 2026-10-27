#include "NyxPCH.h"
#include "VolumetricClouds.h"

#include "CloudLayer.h"
#include "VolumetricCloudsComponent.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace Nyx
{
	namespace
	{
		constexpr double DegreesToRadians = 3.14159265358979323846 / 180.0;

		// Between 0 and 1, for a value of any size
		double Wrap(double value)
		{
			const double wrapped = value - std::floor(value);
			return wrapped >= 1.0 ? 0.0 : wrapped;
		}

		glm::vec2 Wrap(const glm::dvec2& value)
		{
			return glm::vec2(static_cast<float>(Wrap(value.x)), static_cast<float>(Wrap(value.y)));
		}

		// The Bayer matrices: the frame each pixel of a square is marched in
		constexpr std::array<uint32_t, VolumetricCloudUpdateFrames> Bayer4 = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
		constexpr std::array<uint32_t, 4> Bayer2 = { 0, 2, 3, 1 };
	}

	SceneViewVolumetricCloudsSettings MakeSceneViewVolumetricCloudsSettings(const VolumetricCloudsComponent& clouds, const glm::vec3& cameraPosition, double worldTime)
	{
		SceneViewVolumetricCloudsSettings band;
		band.bClouds = clouds.bClouds;
		if (!band.bClouds)
		{
			return band;
		}
		band.Seed = clouds.Seed;
		band.Bottom = std::clamp(clouds.BottomAltitude, 0.0f, 20000.0f) * 0.001f;
		band.Top = std::clamp(clouds.TopAltitude, clouds.BottomAltitude + 100.0f, 30000.0f) * 0.001f;
		band.Top = std::max(band.Top, band.Bottom + 0.1f);
		band.Coverage = std::clamp(clouds.Coverage, 0.0f, 1.0f);
		band.CoverageVariation = std::clamp(clouds.CoverageVariation, 0.0f, 1.0f);
		band.Type = std::clamp(clouds.Type, 0.0f, 1.0f);
		band.TypeVariation = std::clamp(clouds.TypeVariation, 0.0f, 1.0f);
		band.Density = std::clamp(clouds.Density, 0.0f, 10.0f);
		band.ShapeRepeat = std::clamp(clouds.CloudSize, 100.0f, 50000.0f) * 0.001f * VolumetricCloudNoiseCells;
		band.DetailRepeat = std::clamp(clouds.DetailSize, 10.0f, 5000.0f) * 0.001f * VolumetricCloudNoiseCells;
		band.WeatherRepeat = std::clamp(clouds.WeatherSize, 1000.0f, 500000.0f) * 0.001f * VolumetricCloudNoiseCells;
		band.Detail = std::clamp(clouds.Detail, 0.0f, 1.0f);
		band.Billowiness = std::clamp(clouds.Billowiness, 0.0f, 1.0f);
		band.EdgeSoftness = std::clamp(clouds.EdgeSoftness, 0.01f, 1.0f);
		band.Wispiness = std::clamp(clouds.Wispiness, 0.0f, 1.0f);
		band.Cellularity = std::clamp(clouds.Cellularity, 0.0f, 1.0f);
		band.Brightness = std::clamp(clouds.Brightness, 0.0f, 10.0f);
		band.SilverLining = std::clamp(clouds.SilverLining, 0.0f, 10.0f);
		band.SilverLiningSpread = std::clamp(clouds.SilverLiningSpread, 0.0f, 1.0f);
		band.Powder = std::clamp(clouds.Powder, 0.0f, 1.0f);
		band.MultipleScattering = std::clamp(clouds.MultipleScattering, 0.0f, 1.0f);
		band.Ambient = std::clamp(clouds.Ambient, 0.0f, 10.0f);
		band.SunlitTint = glm::clamp(clouds.SunlitTint, glm::vec3(0.0f), glm::vec3(4.0f));
		band.ShadowTint = glm::clamp(clouds.ShadowTint, glm::vec3(0.0f), glm::vec3(4.0f));
		band.AerialPerspective = std::clamp(clouds.AerialPerspective, 0.0f, 10.0f);
		band.Translucency = std::clamp(clouds.Translucency, 0.0f, 1.0f);
		band.Steps = std::clamp(clouds.Steps, 16u, 512u);
		band.LightSteps = std::clamp(clouds.LightSteps, 1u, 16u);
		band.MaxDistance = std::clamp(clouds.MaxDistance, 1000.0f, 500000.0f) * 0.001f;
		band.BlockSize = clouds.Update == EVolumetricCloudsUpdate::Blocks4x4 ? 4u : (clouds.Update == EVolumetricCloudsUpdate::Blocks2x2 ? 2u : 1u);
		band.bFullResolution = clouds.Resolution == EVolumetricCloudsResolution::Full;
		band.Debug = static_cast<uint32_t>(clouds.DebugView);

		// Round the up axis from +X towards -Z, as the time of day's sunrise
		const double angle = static_cast<double>(clouds.WindDirectionDegrees) * DegreesToRadians;
		const float speed = std::clamp(clouds.WindSpeed, 0.0f, 200.0f) * 0.001f;
		band.WindVelocity = glm::vec2(static_cast<float>(std::cos(angle)), static_cast<float>(-std::sin(angle))) * speed;

		const glm::dvec2 camera(cameraPosition.x, cameraPosition.z);
		band.WeatherOrigin = Wrap(GetVolumetricCloudPatternPoint(camera, worldTime, band.WindVelocity, band.WeatherRepeat));
		band.ShapeOrigin = Wrap(GetVolumetricCloudPatternPoint(camera, worldTime, band.WindVelocity, band.ShapeRepeat));
		band.DetailOrigin = Wrap(GetVolumetricCloudPatternPoint(camera, worldTime, band.WindVelocity, band.DetailRepeat));
		return band;
	}

	glm::dvec2 GetVolumetricCloudPatternPoint(const glm::dvec2& worldXZ, double worldTime, const glm::vec2& windVelocity, double repeat)
	{
		// Kilometres over the ground, less how far the wind carried the noise
		return (worldXZ * 0.001 - glm::dvec2(windVelocity) * worldTime) / repeat;
	}

	CloudBandSpan GetCloudBandSpan(float planetRadius, float height, float cosZenith, float bottom, float top)
	{
		CloudBandSpan span;
		const CloudShellHits low = GetCloudShellHits(planetRadius, height, cosZenith, bottom);
		const CloudShellHits high = GetCloudShellHits(planetRadius, height, cosZenith, top);
		if (height < bottom)
		{
			// Out through the bottom shell, unless the ground comes first, then out through the top
			if (low.Count == 0 || high.Count == 0)
			{
				return span;
			}
			span.Enter = low.Distances[0];
			span.Exit = high.Distances[0];
		}
		else if (height < top)
		{
			// From the camera to the bottom shell, if the ray goes down into it, else the top
			span.Enter = 0.0f;
			span.Exit = low.Count > 0 ? low.Distances[0] : high.Distances[0];
		}
		else
		{
			// In through the top shell, out through the bottom or past the planet's curve through the
			// top again
			if (high.Count == 0)
			{
				return span;
			}
			span.Enter = high.Distances[0];
			span.Exit = low.Count > 0 ? low.Distances[0] : (high.Count > 1 ? high.Distances[1] : high.Distances[0]);
		}
		span.bHit = span.Exit > span.Enter;
		return span;
	}

	glm::uvec2 GetVolumetricCloudUpdateOffset(uint32_t frame, uint32_t blockSize)
	{
		if (blockSize <= 1)
		{
			return glm::uvec2(0);
		}
		const uint32_t* matrix = blockSize == 2 ? Bayer2.data() : Bayer4.data();
		const uint32_t size = blockSize == 2 ? 2u : 4u;
		const uint32_t order = frame % (size * size);
		for (uint32_t index = 0; index < size * size; ++index)
		{
			if (matrix[index] == order)
			{
				return glm::uvec2(index % size, index / size);
			}
		}
		return glm::uvec2(0);
	}

	uint32_t GetVolumetricCloudBlockSize(uint32_t blockSize, float shift)
	{
		if (shift > VolumetricCloudFasterShift)
		{
			return 1;
		}
		if (shift > VolumetricCloudFastShift)
		{
			return std::min(blockSize, 2u);
		}
		return blockSize;
	}

	float GetVolumetricCloudScreenShift(const glm::mat4& previousSkyViewProjection, const glm::mat4& skyViewProjection, float height, const glm::vec3& moved, float distance)
	{
		const glm::mat4 inverse = glm::inverse(skyViewProjection);
		float most = 0.0f;
		for (const glm::vec2& ndc : { glm::vec2(0.0f), glm::vec2(-1.0f, -1.0f), glm::vec2(1.0f, -1.0f), glm::vec2(-1.0f, 1.0f), glm::vec2(1.0f, 1.0f) })
		{
			// The way the point looks now, where that way was on the picture before
			const glm::vec4 way = inverse * glm::vec4(ndc, 1.0f, 1.0f);
			const glm::vec4 before = previousSkyViewProjection * glm::vec4(glm::vec3(way) / way.w, 1.0f);
			if (before.w <= 0.0f)
			{
				return std::numeric_limits<float>::max();
			}
			most = std::max(most, glm::length(glm::vec2(before) / before.w - ndc) * 0.5f * height);
		}
		// A move across the view at the nearest distance, by the picture's pixels a radian (half its
		// height over the angle from its middle to its top)
		const glm::vec4 middle = inverse * glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
		const glm::vec4 top = inverse * glm::vec4(0.0f, 1.0f, 1.0f, 1.0f);
		const float angle = std::acos(std::clamp(glm::dot(glm::normalize(glm::vec3(middle) / middle.w), glm::normalize(glm::vec3(top) / top.w)), -1.0f, 1.0f));
		const float pixelsPerRadian = 0.5f * height / std::max(angle, 1e-4f);
		return most + glm::length(moved) / std::max(distance, 0.01f) * pixelsPerRadian;
	}

	bool IsVolumetricCloudsLookChanged(const VolumetricCloudsLook& marched, const VolumetricCloudsLook& wanted)
	{
		const SceneViewVolumetricCloudsSettings& a = marched.Settings;
		const SceneViewVolumetricCloudsSettings& b = wanted.Settings;
		if (a.Wispiness != b.Wispiness || a.Cellularity != b.Cellularity || a.SunlitTint != b.SunlitTint || a.AerialPerspective != b.AerialPerspective || a.Translucency != b.Translucency || a.BlockSize != b.BlockSize ||
			a.bFullResolution != b.bFullResolution)
		{
			return true;
		}
		if (a.bClouds != b.bClouds || a.Seed != b.Seed || a.Bottom != b.Bottom || a.Top != b.Top || a.Coverage != b.Coverage || a.CoverageVariation != b.CoverageVariation ||
			a.Type != b.Type || a.TypeVariation != b.TypeVariation || a.Density != b.Density || a.ShapeRepeat != b.ShapeRepeat || a.DetailRepeat != b.DetailRepeat ||
			a.WeatherRepeat != b.WeatherRepeat || a.Detail != b.Detail || a.Billowiness != b.Billowiness || a.EdgeSoftness != b.EdgeSoftness || a.Brightness != b.Brightness ||
			a.SilverLining != b.SilverLining || a.SilverLiningSpread != b.SilverLiningSpread || a.Powder != b.Powder || a.MultipleScattering != b.MultipleScattering ||
			a.Ambient != b.Ambient || a.ShadowTint != b.ShadowTint || a.Steps != b.Steps || a.LightSteps != b.LightSteps || a.MaxDistance != b.MaxDistance ||
			a.Debug != b.Debug)
		{
			return true;
		}
		// By the sine of the angle between, which is exact for small angles where the cosine isn't
		const float lengths = glm::length(marched.TowardsSun) * glm::length(wanted.TowardsSun);
		if (lengths <= 0.0f)
		{
			return glm::length(marched.TowardsSun) != glm::length(wanted.TowardsSun);
		}
		const float sine = glm::length(glm::cross(marched.TowardsSun, wanted.TowardsSun)) / lengths;
		return sine > std::sin(VolumetricCloudSunTolerance * static_cast<float>(DegreesToRadians)) || glm::dot(marched.TowardsSun, wanted.TowardsSun) < 0.0f;
	}
}
