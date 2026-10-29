#pragma once

#include "CloudModifierComponent.h"
#include "Entity.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Nyx
{
	class Registry;

	// Cloud modifiers (CloudModifierComponent; Docs/Design/lightning.md, step 4) as plain math: a
	// shape's strength at the world's time, the shapes a frame changes the clouds by, the few a view
	// takes, and what they do at a point, as nyx.volumetricclouds does it to every density sample. No
	// GPU, so tests check it as the shaders use it.

	// The most shapes a view's volumetric clouds are changed by at once (ViewConstants.CloudModifiers)
	inline constexpr uint32_t MaxCloudModifiers = 8;

	// How much of a shape is there at the world's time `time`, 0 to 1: none before its start, growing
	// over Fade In, all of it for its Duration (for ever with 0), fading over Fade Out after
	float GetCloudModifierFade(const CloudModifier& modifier, float time);

	// A shape as a frame changes the clouds by it: round `Center` (the world's x and z, metres),
	// between Bottom and Top (metres above the world's height 0; the whole band when Top isn't above
	// Bottom), its Strength at the moment (a swirl's: the angle it has turned the clouds by at its
	// radius, in radians, in Angle)
	struct RenderCloudModifier
	{
		Nyx::Entity SourceEntity{};
		ECloudModifierShape Shape = ECloudModifierShape::ClearColumn;
		glm::vec2 Center{ 0.0f };
		float Radius = 0.0f;
		float Width = 0.0f;
		float Strength = 0.0f;
		float Angle = 0.0f;
		float Bottom = 0.0f;
		float Top = 0.0f;
		// How high its entity is, for the editor's overlays of a shape over the whole band
		float Height = 0.0f;
	};

	// A component's shape as a frame changes the clouds by it, round `center`, at `time`; none while
	// it is gone, or does nothing (no strength, no size)
	std::optional<RenderCloudModifier> MakeRenderCloudModifier(const CloudModifier& modifier, const glm::vec3& center, float time, Entity entity = {});

	// The world's cloud modifiers' shapes at its time `time`, appended to `outModifiers`, in the order
	// the world holds them, each round its entity's position
	void ExtractCloudModifiers(const Registry& world, float time, std::vector<RenderCloudModifier>& outModifiers);

	// The shapes a view's clouds take: the MaxCloudModifiers whose edge is nearest `camera` (the
	// world's x and z), into `outModifiers` (cleared first), ties by the order given
	void PickViewCloudModifiers(std::span<const RenderCloudModifier> modifiers, const glm::vec2& camera, std::vector<RenderCloudModifier>& outModifiers);

	// What the shapes do at a point (the world's x and z, and its height above the world's height 0,
	// in metres): what the clouds' coverage and density there are multiplied by (Keep, the columns'
	// clearing), what is added to the coverage (Add, the rings'; the density times 1 + it), and
	// where the clouds' shapes are looked up instead (the swirls')
	struct CloudModifierEffect
	{
		float Keep = 1.0f;
		float Add = 0.0f;
		glm::vec2 Lookup{ 0.0f };
	};
	CloudModifierEffect GetCloudModifierEffect(std::span<const RenderCloudModifier> modifiers, const glm::vec2& point, float height);
}
