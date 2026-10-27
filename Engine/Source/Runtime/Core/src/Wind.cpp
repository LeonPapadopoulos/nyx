#include "NyxPCH.h"
#include "Wind.h"

#include <algorithm>
#include <cmath>

namespace Nyx
{
	// Every step here is as nyx.wind's (Engine/Shaders/Library/nyx/wind.slang) does it, in the same
	// order: only additions, multiplications, floors and integer hashes, each rounded alone (no fused
	// multiply-adds), so the CPU and the GPU agree to the bit. Change both together.
	namespace
	{
		uint32_t HashCell(uint32_t x, uint32_t y)
		{
			uint32_t h = (x * 0x8DA6B343u) ^ (y * 0xD8163841u);
			h ^= h >> 16;
			h *= 0x7FEB352Du;
			h ^= h >> 15;
			h *= 0x846CA68Bu;
			h ^= h >> 16;
			return h;
		}

		// 16 bits as 0 to 1, below 1
		float Unit16(uint32_t bits)
		{
			return static_cast<float>(bits & 0xFFFFu) * (1.0f / 65536.0f);
		}

		// A cell's index as the hash takes it, a whole float to its integer's bits
		uint32_t CellIndex(float whole)
		{
			return static_cast<uint32_t>(static_cast<int32_t>(whole));
		}

		// The floor, exactly as Floor() and the GPU's floor() give it (the sign of a zero too),
		// without the library call MSVC makes for it without SSE4.1
		float Floor(float x)
		{
			if (!(std::abs(x) < 8388608.0f))
			{
				// Whole already (or not a number)
				return x;
			}
			// Truncated towards 0, one less below 0; a zero keeps the sign x has (-0.0 stays -0.0)
			const float truncated = static_cast<float>(static_cast<int32_t>(x));
			return truncated > x ? truncated - 1.0f : (truncated == 0.0f ? x * 0.0f : truncated);
		}

		float Smooth(float t)
		{
			return t * t * (3.0f - 2.0f * t);
		}

		float Saturate(float value)
		{
			return std::min(std::max(value, 0.0f), 1.0f);
		}

		float Along(const WindPattern& pattern, const glm::vec3& position)
		{
			const float x = position.x * pattern.Direction.x;
			const float z = position.z * pattern.Direction.y;
			return x + z;
		}

		float Across(const WindPattern& pattern, const glm::vec3& position)
		{
			const float z = position.z * pattern.Direction.x;
			const float x = position.x * pattern.Direction.y;
			return z - x;
		}

		float Gust(const WindPattern& pattern, const glm::vec3& position, float phase)
		{
			const float a = Along(pattern, position) * pattern.GustsPerMetre - phase;
			const float front = Floor(a);
			const float u = a - front;
			// A front arrives sharply (u falling from 1 to 0.8 as it passes) and dies away slowly
			const float t = u > 0.8f ? (1.0f - u) * 5.0f : u * 1.25f;
			const float shape = Smooth(t);

			// Its strength, from patch to patch along the front
			const float c = Across(pattern, position) * pattern.GustPatchesPerMetre;
			const float patch = Floor(c);
			const float blend = Smooth(c - patch);
			const uint32_t index = CellIndex(front) & (WindGustCycles - 1u);
			const float s0 = Unit16(HashCell(index, CellIndex(patch)));
			const float s1 = Unit16(HashCell(index, CellIndex(patch) + 1u));
			const float strength = s0 + (s1 - s0) * blend;
			return shape * strength;
		}

		float Lerp(float a, float b, float w)
		{
			return a + (b - a) * w;
		}

		// The eddies' swirl at a point, each part -1 to 1
		glm::vec2 Eddies(const WindPattern& pattern, const glm::vec2& move, const glm::vec3& position)
		{
			const float offsetX = pattern.EddyOffset.x + move.x;
			const float offsetZ = pattern.EddyOffset.y + move.y;
			const float qx = (position.x - offsetX) * pattern.EddiesPerMetre;
			const float qz = (position.z - offsetZ) * pattern.EddiesPerMetre;
			const float cellX = Floor(qx);
			const float cellZ = Floor(qz);
			const float wx = Smooth(qx - cellX);
			const float wz = Smooth(qz - cellZ);
			const uint32_t x0 = CellIndex(cellX) & (WindEddyCycles - 1u);
			const uint32_t z0 = CellIndex(cellZ) & (WindEddyCycles - 1u);
			const uint32_t x1 = (x0 + 1u) & (WindEddyCycles - 1u);
			const uint32_t z1 = (z0 + 1u) & (WindEddyCycles - 1u);
			const auto corner = [](uint32_t h) { return glm::vec2(Unit16(h) * 2.0f - 1.0f, Unit16(h >> 16) * 2.0f - 1.0f); };
			const glm::vec2 c00 = corner(HashCell(x0, z0));
			const glm::vec2 c10 = corner(HashCell(x1, z0));
			const glm::vec2 c01 = corner(HashCell(x0, z1));
			const glm::vec2 c11 = corner(HashCell(x1, z1));
			const float ax = Lerp(c00.x, c10.x, wx);
			const float az = Lerp(c00.y, c10.y, wx);
			const float bx = Lerp(c01.x, c11.x, wx);
			const float bz = Lerp(c01.y, c11.y, wx);
			return glm::vec2(Lerp(ax, bx, wz), Lerp(az, bz, wz));
		}

		// The fronts' phase and how far the eddies have moved since the state's time, at `time`
		struct WindMoment
		{
			float Phase = 0.0f;
			glm::vec2 Move{ 0.0f };
		};

		WindMoment GetMoment(const WindState& wind, float time)
		{
			WindMoment moment;
			moment.Phase = wind.GustPhase + wind.GustRate * (time - wind.Time);
			const float since = time - wind.Time;
			moment.Move = glm::vec2(wind.EddyVelocity.x * since, wind.EddyVelocity.y * since);
			return moment;
		}

		// The front passing, of the pattern faded to and, while it fades, of the one faded from
		float BlendedGust(const WindState& wind, const glm::vec3& position, float phase)
		{
			const float to = Gust(wind.To, position, phase);
			if (wind.PatternBlend >= 1.0f)
			{
				return to;
			}
			const float from = Gust(wind.From, position, phase);
			return Lerp(from, to, wind.PatternBlend);
		}

		glm::vec2 BlendedEddies(const WindState& wind, const glm::vec3& position, const glm::vec2& move)
		{
			const glm::vec2 to = Eddies(wind.To, move, position);
			if (wind.PatternBlend >= 1.0f)
			{
				return to;
			}
			const glm::vec2 from = Eddies(wind.From, move, position);
			return glm::vec2(Lerp(from.x, to.x, wind.PatternBlend), Lerp(from.y, to.y, wind.PatternBlend));
		}
	}

	float GetWindHeightFactor(const WindState& wind, float y)
	{
		const float height = y - wind.GroundHeight;
		const float q = Saturate(height * wind.InverseReferenceHeight);
		const float near = wind.GroundFactor + (1.0f - wind.GroundFactor) * (q * (2.0f - q));
		const float e = Saturate(height * wind.InverseAloftSpan - wind.AloftStart);
		return near + wind.Aloft * (e * (2.0f - e));
	}

	float SampleWindGust(const WindState& wind, const glm::vec3& position, float time)
	{
		if (!(wind.Speed > 0.0f))
		{
			return 0.0f;
		}
		return BlendedGust(wind, position, GetMoment(wind, time).Phase);
	}

	namespace
	{
		glm::vec3 SampleAt(const WindState& wind, const glm::vec3& position, const WindMoment& moment)
		{
			const float speed = wind.Speed * GetWindHeightFactor(wind, position.y);
			const float gusting = speed * (1.0f + wind.Gustiness * BlendedGust(wind, position, moment.Phase));
			const glm::vec2 swirl = BlendedEddies(wind, position, moment.Move);
			const float swirling = speed * wind.Turbulence;
			const float x = wind.Direction.x * gusting + swirl.x * swirling;
			const float z = wind.Direction.y * gusting + swirl.y * swirling;
			return glm::vec3(x, 0.0f, z);
		}
	}

	glm::vec3 SampleWind(const WindState& wind, const glm::vec3& position, float time)
	{
		if (!(wind.Speed > 0.0f))
		{
			return glm::vec3(0.0f);
		}
		return SampleAt(wind, position, GetMoment(wind, time));
	}

	void AddWind(const WindState& wind, std::span<const glm::vec3> positions, float time, std::span<glm::vec3> inOutVelocities)
	{
		if (!(wind.Speed > 0.0f))
		{
			return;
		}
		const WindMoment moment = GetMoment(wind, time);
		const size_t count = std::min(positions.size(), inOutVelocities.size());
		for (size_t index = 0; index < count; ++index)
		{
			inOutVelocities[index] += SampleAt(wind, positions[index], moment);
		}
	}
}
