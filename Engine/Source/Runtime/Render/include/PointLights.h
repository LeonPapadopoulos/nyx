#pragma once

#include "Entity.h"
#include "SceneCulling.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Nyx
{
	class Registry;

	// Point lights (PointLightComponent) as plain math (Docs/Design/lights-and-particles.md): how a
	// light falls off, a world's lights as a frame draws them, and which of them a view lights its
	// pixels with. No GPU, so tests check them as the renderer uses them (nyx.lighting).

	// The most lights a view lights its pixels with: each pixel goes through all of them, so more
	// than a few dozen in view at once want tiles or clusters of lights instead (see the design note)
	inline constexpr uint32_t MaxViewPointLights = 256;

	// How big a light's icon in the editor's scene views is, in metres round it: the ball of lines
	// drawn there, which a click picks the light by
	inline constexpr float PointLightIconRadius = 0.2f;

	// How near a light a point counts as at most, in metres, so that the light at its own position
	// isn't infinitely bright: the size of a small bulb
	inline constexpr float MinPointLightDistance = 0.1f;

	// The part of a light's intensity that reaches `distance` metres from it, for a light of
	// `range` (nyx::pointLightFalloff()): the inverse square of the distance, as light from a point
	// spreads, times a window that brings it softly to 0 at the range, (1 - (d / r)^4)^2, so it never
	// stops at an edge. 1 at a metre for a long range; 0 at and past the range; never more than at
	// MinPointLightDistance.
	float GetPointLightFalloff(float distance, float range);

	// A point light as a frame draws it
	struct RenderPointLight
	{
		Nyx::Entity SourceEntity{};
		// Where the scene pass lights from: between its last two steps in a world drawn so, else
		// where its entity is
		glm::vec3 Position{ 0.0f };
		// Where its entity is, for picking and the editor's overlays
		glm::vec3 WorldPosition{ 0.0f };
		float Range = 0.0f;
		// The colour times the intensity
		glm::vec3 Color{ 0.0f };

		// Its entity's picking id in its world; 0 until given one
		uint32_t PickingId = 0;
	};

	// The world's point lights to draw, in the order the world holds them: those that give light, a
	// colour above black with an intensity and range above 0. With `interpolation`, each between its
	// entity's last two steps, as the meshes are (GetDrawnTransform()). No picking ids are given.
	void ExtractPointLights(const Registry& world, std::optional<float> interpolation, std::vector<RenderPointLight>& outLights);

	// One light as a view's pixels read it: PointLight in nyx/lighting.slang, std430
	struct ViewPointLight
	{
		glm::vec3 Position{ 0.0f };
		// 1 / range²: a pixel at d² * this >= 1 gets none of it
		float InverseRangeSquared = 0.0f;
		glm::vec3 Color{ 0.0f };
		// Free, e.g. for a shadow map's index later
		uint32_t Padding = 0;
	};
	static_assert(sizeof(ViewPointLight) == 32, "ViewPointLight must match PointLight in nyx/lighting.slang");

	// What a view's culling of the lights did
	struct PointLightCullStats
	{
		// The world's lights, those wholly outside the view, and those in it left out past `maxLights`
		uint32_t Lights = 0;
		uint32_t Culled = 0;
		uint32_t Dropped = 0;
	};

	// The lights a view lights its pixels with, into `outLights` (cleared first): those whose sphere
	// of light, their range round them, is not wholly outside `frustum`. Past `maxLights`, those
	// whose light reaches nearest the camera, at `camera`, are kept; ties by the world's order.
	PointLightCullStats CullPointLights(std::span<const RenderPointLight> lights, const Frustum& frustum, const glm::vec3& camera, uint32_t maxLights,
		std::vector<ViewPointLight>& outLights);

	// Whether a sphere is not wholly outside the frustum: on the inner side of each plane, or less
	// than its radius beyond it. Also true for a sphere near the frustum's corners that is outside
	// it, as the planes alone can't tell it apart.
	bool IsSphereVisible(const Frustum& frustum, const glm::vec3& center, float radius);
}
