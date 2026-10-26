#include "NyxPCH.h"
#include "CloudLayer.h"

#include "CloudLayerComponent.h"

#include <algorithm>
#include <cmath>

namespace Nyx
{
	namespace
	{
		constexpr double DegreesToRadians = 3.14159265358979323846 / 180.0;

		float SmoothStep(float edge0, float edge1, float x)
		{
			const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		}

		// Between 0 and the period, for a value of any size
		double Wrap(double value, double period)
		{
			const double wrapped = std::fmod(value, period);
			return wrapped < 0.0 ? wrapped + period : wrapped;
		}
	}

	SceneViewCloudLayerSettings MakeSceneViewCloudLayerSettings(const CloudLayerComponent& clouds, const glm::vec3& cameraPosition, double worldTime)
	{
		SceneViewCloudLayerSettings layer;
		layer.bClouds = clouds.bClouds;
		if (!layer.bClouds)
		{
			return layer;
		}
		layer.Seed = clouds.Seed;
		layer.Altitude = std::clamp(clouds.Altitude, 10.0f, 50000.0f) * 0.001f;
		layer.Thickness = std::clamp(clouds.Thickness, 1.0f, 5000.0f) * 0.001f;
		layer.Coverage = std::clamp(clouds.Coverage, 0.0f, 1.0f);
		layer.Type = std::clamp(clouds.Type, 0.0f, 1.0f);
		layer.Softness = std::clamp(clouds.Softness, 0.01f, 1.0f);
		layer.Detail = std::clamp(clouds.Detail, 0.0f, 1.0f);
		layer.Warp = std::clamp(clouds.Warp, 0.0f, 1.0f);
		layer.Brightness = std::clamp(clouds.Brightness, 0.0f, 10.0f);
		layer.Rim = std::clamp(clouds.Rim, 0.0f, 10.0f);
		layer.Ambient = std::clamp(clouds.Ambient, 0.0f, 10.0f);
		layer.TileSize = std::clamp(clouds.CloudletSize, 10.0f, 10000.0f) * 0.001f * CloudNoiseCells;
		layer.LightStrength = std::clamp(clouds.LightStrength, 0.0f, 4.0f);
		layer.bLightsWorld = clouds.bLightsWorld && layer.LightStrength > 0.0f;

		// Round the up axis from +X towards -Z, as the time of day's sunrise
		const double angle = static_cast<double>(clouds.WindDirectionDegrees) * DegreesToRadians;
		layer.WindAxis = glm::vec2(static_cast<float>(std::cos(angle)), static_cast<float>(-std::sin(angle)));

		const float windSpeed = std::clamp(clouds.WindSpeed, 0.0f, 200.0f);
		const glm::dvec2 origin = GetCloudPatternPoint(layer, glm::dvec2(cameraPosition.x, cameraPosition.z), worldTime, windSpeed);
		layer.Origin = glm::vec2(static_cast<float>(Wrap(origin.x, CloudNoisePeriod)), static_cast<float>(Wrap(origin.y, CloudNoisePeriod)));
		return layer;
	}

	glm::dvec2 GetCloudPatternPoint(const SceneViewCloudLayerSettings& clouds, const glm::dvec2& worldXZ, double worldTime, float windSpeed)
	{
		// The pattern's axes in the world's x and z: along the wind, and a quarter turn from it
		const glm::dvec2 along = glm::normalize(glm::dvec2(clouds.WindAxis));
		const glm::dvec2 across(-along.y, along.x);
		// Kilometres over the ground, the wind's way taken back by how far it carried the pattern
		const glm::dvec2 kilometres = worldXZ * 0.001;
		const double tile = static_cast<double>(clouds.TileSize);
		return glm::dvec2((glm::dot(kilometres, along) - static_cast<double>(windSpeed) * 0.001 * worldTime) / tile, glm::dot(kilometres, across) / tile);
	}

	CloudShellHits GetCloudShellHits(float planetRadius, float height, float cosZenith, float altitude)
	{
		CloudShellHits hits;
		// |o + t d|² = shell², o the camera planet-centred: t² + 2 b t + c = 0
		const float r = planetRadius + height;
		const float b = r * cosZenith;
		const float c = (height - altitude) * (2.0f * planetRadius + height + altitude);
		const float discriminant = b * b - c;
		if (c <= 0.0f)
		{
			// Below the shell (or on it): out through it once, the root without cancellation
			const float root = std::sqrt(std::max(discriminant, 0.0f));
			const float t = b > 0.0f ? -c / (b + root) : root - b;
			hits.Count = 1;
			hits.Distances[0] = std::max(t, 0.0f);
		}
		else if (b < 0.0f && discriminant >= 0.0f)
		{
			// Above it, looking down: in, and out on its far side
			const float root = std::sqrt(discriminant);
			hits.Count = 2;
			hits.Distances[0] = c / (root - b);
			hits.Distances[1] = root - b;
		}

		// Nothing behind the ground
		if (hits.Count > 0)
		{
			const float horizon = -std::sqrt(std::max(height, 0.0f) * (2.0f * planetRadius + std::max(height, 0.0f))) / r;
			if (cosZenith < horizon)
			{
				const float groundC = height * (2.0f * planetRadius + height);
				const float ground = groundC / std::max(-b + std::sqrt(std::max(b * b - groundC, 0.0f)), 1e-9f);
				while (hits.Count > 0 && hits.Distances[hits.Count - 1] > ground)
				{
					--hits.Count;
				}
			}
		}
		return hits;
	}

	bool IsCloudCaptureChanged(const SceneViewCloudCapture& built, const SceneViewCloudCapture& wanted)
	{
		if (built.bClouds != wanted.bClouds)
		{
			return true;
		}
		if (!wanted.bClouds)
		{
			return false;
		}
		const SceneViewCloudLayerSettings& a = built.Layer;
		const SceneViewCloudLayerSettings& b = wanted.Layer;
		if (built.NoiseSlot != wanted.NoiseSlot || a.Seed != b.Seed || a.Altitude != b.Altitude || a.Thickness != b.Thickness || a.Coverage != b.Coverage || a.Type != b.Type ||
			a.Softness != b.Softness || a.Detail != b.Detail || a.Warp != b.Warp || a.Brightness != b.Brightness || a.Rim != b.Rim || a.Ambient != b.Ambient || a.TileSize != b.TileSize ||
			a.WindAxis != b.WindAxis || a.LightStrength != b.LightStrength)
		{
			return true;
		}
		// The way round the period it moved the least
		const float period = static_cast<float>(CloudNoisePeriod);
		const glm::vec2 moved = glm::abs(b.Origin - a.Origin);
		const glm::vec2 least = glm::min(moved, glm::vec2(period) - moved);
		return glm::max(least.x, least.y) > CloudCaptureTolerance;
	}

	float GetCloudNearFade(float distance, float thickness)
	{
		return SmoothStep(0.25f, 2.0f, distance / std::max(thickness, 1e-6f));
	}
}
