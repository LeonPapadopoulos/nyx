#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <span>

namespace Nyx
{
	// The pattern of the wind's gust fronts and eddies: which way the fronts lie and how they are
	// spaced, how big the eddies are and how far the wind has carried them. A wind that turns or
	// changes its spacing fades from one pattern to the next (WindState::PatternBlend) instead of
	// turning the pattern about the world's origin, which would sweep fronts far away over the land.
	struct WindPattern
	{
		// The way its fronts travel, in the world's x and z, normalised (the wind's when it was made)
		glm::vec2 Direction{ 1.0f, 0.0f };
		// Its fronts a metre along it (1 / their spacing), and the patches of their strength a metre
		// across it (1 / their width)
		float GustsPerMetre = 0.0f;
		float GustPatchesPerMetre = 0.0f;
		// How far the wind has carried its eddies by WindState::Time, in metres, wrapped below
		// WindEddyCycles of them; and its eddies a metre (1 / their size)
		glm::vec2 EddyOffset{ 0.0f };
		float EddiesPerMetre = 0.0f;
		float Padding = 0.0f;
	};

	// The prevailing wind of a world at a moment (Docs/Design/wind.md): the one contract everything
	// that moves with the wind reads, and nothing else of the wind. Grass and trees, particles,
	// clouds and a game's own air read it; the world's wind (WindComponent) makes it now, a weather
	// system will later. Plain numbers, read only through SampleWind() here and nyx::sampleWind() in
	// the shader library (nyx.wind), which give the same results to the bit: both only add,
	// multiply, floor and hash, every division made beforehand into the numbers below.
	//
	// The wind blows along Direction at Speed at its reference height: slower near the ground,
	// faster aloft. Gust fronts cross the land, bands across the wind that strengthen it as they
	// pass, each front strong in some places along it and weak in others; eddies of turbulence,
	// carried by the wind, swirl it about. Its numbers are as they are at Time; asked at another
	// time, its fronts and eddies have moved on at their rates.
	struct WindState
	{
		// Where it blows to, in the world's x and z, normalised; and how fast at the reference
		// height between gusts, in metres a second (0: calm, and nothing else counts)
		glm::vec2 Direction{ 1.0f, 0.0f };
		float Speed = 0.0f;
		// How much faster at the peak of the strongest front, as a part of Speed: 0 steady
		float Gustiness = 0.0f;

		// How many fronts have passed a place by Time (each pattern's along its own way), wrapped
		// below WindGustCycles, and how many pass a second; the eddies' speed as a part of Speed;
		// how far it has faded from the pattern From to the pattern To, 1 only To's (a steady wind:
		// From is ignored), 0 only From's
		float GustPhase = 0.0f;
		float GustRate = 0.0f;
		float Turbulence = 0.0f;
		float PatternBlend = 1.0f;
		// How fast the wind carries the eddies, in metres a second
		glm::vec2 EddyVelocity{ 0.0f };
		// Always 0: the shaders XOR each product's bits with it, a number the compiler can't know,
		// so the GPU rounds every product alone, as the CPU does (nyx.wind's windMul())
		float Exact = 0.0f;
		float Padding1 = 0.0f;
		WindPattern From;
		WindPattern To;

		// The height profile, over the height above GroundHeight (metres): at the ground the speed
		// is GroundFactor of Speed, rising to all of it at the reference height (1 /
		// InverseReferenceHeight above the ground); above that it grows by up to Aloft more of it,
		// reached AloftSpan above the reference height, where AloftStart is the reference height /
		// AloftSpan and InverseAloftSpan 1 / AloftSpan. Aloft 0: no faster above.
		float GroundHeight = 0.0f;
		float GroundFactor = 1.0f;
		float InverseReferenceHeight = 0.0f;
		float Aloft = 0.0f;
		float AloftStart = 0.0f;
		float InverseAloftSpan = 0.0f;

		// The world's time, in seconds, the phases and offsets above are at
		float Time = 0.0f;
		float Padding2 = 0.0f;
	};
	// Laid out in whole 16 bytes, so the uniform, scalar and storage layouts are all the same
	static_assert(sizeof(WindPattern) == 32, "WindPattern must match nyx.wind's WindPattern");
	static_assert(sizeof(WindState) == 144, "WindState must match nyx.wind's WindState");

	// How many fronts and eddies the patterns repeat after: the phase and the offsets are wrapped by
	// whole repeats, so they stay small and exact however long the wind blows
	inline constexpr uint32_t WindGustCycles = 1024;
	inline constexpr uint32_t WindEddyCycles = 1024;

	// The wind's velocity at `position` (metres, in the world) at the world's `time` (seconds), in
	// metres a second: along the ground (y is 0), as fast as the height profile, the front passing
	// and the eddies make it
	glm::vec3 SampleWind(const WindState& wind, const glm::vec3& position, float time);

	// SampleWind() at each of `positions`, added to `inOutVelocities` (as many): the same numbers, the
	// moment's phases worked out once, for many points at once (an emitter's particles)
	void AddWind(const WindState& wind, std::span<const glm::vec3> positions, float time, std::span<glm::vec3> inOutVelocities);

	// How strongly a gust front is blowing at `position` at `time`: 0 between fronts, up to 1 at the
	// peak of the strongest; the wind there is (1 + Gustiness × this) times as fast
	float SampleWindGust(const WindState& wind, const glm::vec3& position, float time);

	// The height profile alone: what Speed is multiplied by at the height `y`
	float GetWindHeightFactor(const WindState& wind, float y);
}
