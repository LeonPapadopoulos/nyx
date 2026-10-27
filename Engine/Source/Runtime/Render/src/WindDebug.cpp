#include "NyxPCH.h"
#include "WindDebug.h"

#include <cmath>

namespace Nyx
{
	namespace
	{
		glm::vec3 GustColor(float gust)
		{
			const glm::vec3 lull(0.2f, 0.55f, 1.0f);
			const glm::vec3 mid(1.0f, 0.85f, 0.2f);
			const glm::vec3 peak(1.0f, 0.15f, 0.1f);
			return gust < 0.5f ? glm::mix(lull, mid, gust * 2.0f) : glm::mix(mid, peak, gust * 2.0f - 1.0f);
		}
	}

	void DrawWindDebug(DebugDraw& drawing, const WindState& wind, const glm::vec3& camera, float groundHeight, float time, bool bGustFronts)
	{
		const float height = groundHeight + 1.0f;

		// The arrows, on whole points of the grid so they don't swim as the camera moves
		const glm::vec2 first = glm::floor((glm::vec2(camera.x, camera.z) - WindDebugArrowReach) / WindDebugArrowSpacing) * WindDebugArrowSpacing;
		const int32_t count = static_cast<int32_t>(2.0f * WindDebugArrowReach / WindDebugArrowSpacing) + 1;
		for (int32_t z = 0; z < count; ++z)
		{
			for (int32_t x = 0; x < count; ++x)
			{
				const glm::vec3 point(first.x + static_cast<float>(x) * WindDebugArrowSpacing, height, first.y + static_cast<float>(z) * WindDebugArrowSpacing);
				const glm::vec3 velocity = SampleWind(wind, point, time);
				if (glm::dot(velocity, velocity) < 1e-6f)
				{
					drawing.Cross(point, 0.1f, GustColor(0.0f));
					continue;
				}
				drawing.Arrow(point, point + velocity * WindDebugArrowSecondsLong, GustColor(SampleWindGust(wind, point, time)));
			}
		}

		if (!bGustFronts || !(wind.Speed > 0.0f) || !(wind.To.GustsPerMetre > 0.0f))
		{
			return;
		}

		// The fronts' crests, where each is strongest along the wind (Wind.cpp: a front peaks as the
		// part of the way to the next one falls to 0.8), those within reach of the camera: of the
		// pattern the wind is fading to
		const WindPattern& pattern = wind.To;
		const glm::vec2 along = pattern.Direction;
		const glm::vec2 across(-along.y, along.x);
		const glm::vec2 centre(camera.x, camera.z);
		const float phase = wind.GustPhase + wind.GustRate * (time - wind.Time);
		const float spacing = 1.0f / pattern.GustsPerMetre;
		const float centreAlong = glm::dot(centre, along);
		const float firstFront = std::floor((centreAlong - WindDebugFrontReach) * pattern.GustsPerMetre - phase);
		const float lastFront = std::ceil((centreAlong + WindDebugFrontReach) * pattern.GustsPerMetre - phase);
		constexpr float Step = 2.0f;
		for (float front = firstFront; front <= lastFront && front - firstFront < 64.0f; front += 1.0f)
		{
			const float crestAlong = (front + 0.8f + phase) * spacing;
			if (std::abs(crestAlong - centreAlong) > WindDebugFrontReach)
			{
				continue;
			}
			const glm::vec2 crest = along * crestAlong + across * glm::dot(centre, across);
			glm::vec3 last(0.0f);
			for (float side = -WindDebugFrontReach; side <= WindDebugFrontReach; side += Step)
			{
				const glm::vec2 ground = crest + across * side;
				const glm::vec3 point(ground.x, groundHeight + 0.2f, ground.y);
				const float gust = SampleWindGust(wind, point, time);
				if (side > -WindDebugFrontReach)
				{
					drawing.Line(last, point, GustColor(gust));
				}
				if (std::fmod(side + WindDebugFrontReach, 4.0f * Step) < 0.5f * Step)
				{
					drawing.Line(point, point + glm::vec3(0.0f, 0.2f + 2.0f * gust, 0.0f), GustColor(gust));
				}
				last = point;
			}
		}
	}
}
