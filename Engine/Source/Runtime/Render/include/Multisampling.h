#pragma once

#include <cstdint>

namespace Nyx
{
	// Multisample anti-aliasing of the scene views as plain math (Docs/Design/anti-aliasing.md): how
	// many samples a pixel the scene pass draws with. No GPU, so tests check it as the renderer
	// uses it (VulkanViewportTarget, IRenderer::SetMsaaSamples()).

	// The samples a pixel unless set otherwise: edges smooth at a cost a desktop GPU hardly feels
	inline constexpr uint32_t DefaultMsaaSamples = 4;
	// The most asked for; more cost memory and time for edges the eye can't tell apart
	inline constexpr uint32_t MaxMsaaSamples = 8;

	// The samples to draw with when `requested` are asked for: 1, 2, 4 or 8, a number between
	// rounded down to one of those, 0 as 1, more than 8 as 8; then the most the device can do of
	// those, `supported` holding a bit for each count it can (bit n for 2^n samples, as
	// vk::SampleCountFlags), not more than asked. At least 1.
	uint32_t ChooseMsaaSamples(uint32_t requested, uint32_t supported);
}
