#pragma once

#include "SceneCulling.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>

namespace Nyx
{
	// The directional light's cascaded shadow maps, as plain math: where the camera's view is cut
	// into cascades, the sphere around each slice, and the light's view of it, snapped to the
	// shadow map's texels. No GPU, so tests check it as the renderer uses it (VulkanShadows).
	//
	// Stable cascades: a slice's sphere has the same radius however the camera turns, so the
	// cascade's size in the world never changes, and its centre moves over the light's view in
	// whole texels only. A still scene's shadows don't shimmer as the camera moves and turns.

	inline constexpr uint32_t MaxShadowCascades = 4;

	// The camera the cascades cut up: its place and turn, its field of view and its near plane
	struct ShadowCamera
	{
		// From the camera's space to the world: looking along its -Z, up its +Y
		glm::mat4 CameraToWorld{ 1.0f };
		// tan(fovY / 2) and width over height
		float TanHalfFovY = 0.57735f;
		float AspectRatio = 16.0f / 9.0f;
		float NearPlane = 0.1f;
	};

	// The camera of a view as MakePerspectiveProjection() made its projection
	ShadowCamera MakeShadowCamera(const glm::mat4& view, const glm::mat4& projection, float nearPlane);

	// Where the cascades start and end, in metres from the camera along its view: `count` + 1 of
	// them, the first the near plane and the last `distance`. `blend` mixes splits evenly apart
	// (0) and logarithmic ones (1), which keep each cascade's texels about as big on the screen.
	std::array<float, MaxShadowCascades + 1> ComputeCascadeSplits(float nearPlane, float distance, uint32_t count, float blend);

	struct BoundingSphere
	{
		glm::vec3 Center{ 0.0f };
		float Radius = 0.0f;
	};

	// The smallest sphere around the part of the camera's view from `nearDepth` to `farDepth`
	// metres along it: its centre on the camera's axis, its radius the same whichever way the
	// camera looks
	BoundingSphere FitCascadeSphere(const ShadowCamera& camera, float nearDepth, float farDepth);

	// One cascade: the light's view of a sphere, `resolution` texels across it
	struct ShadowCascade
	{
		// From the world to the light's view, and on to Vulkan's clip space: x and y -1 to 1
		// over the sphere, y down as the picture's, depth reversed (1 nearest the light)
		glm::mat4 ViewProjection{ 1.0f };
		BoundingSphere Sphere;
		// Its slice of the camera's view, in metres from the camera
		float SplitNear = 0.0f;
		float SplitFar = 0.0f;
		// How wide a texel of its shadow map is in the world, in metres
		float TexelSize = 0.0f;
	};

	// The light's view of `sphere`, shining along -`towardsLight`: square, the sphere's diameter
	// across, its centre snapped to whole texels in the light's view so that the texels stay where
	// they are in the world as the sphere moves. Its depth reaches from beyond the sphere's far side
	// to `casterReach` metres before its centre towards the light, or the sphere's near side if that
	// is further, so that what lies between the light and the sphere casts into it.
	ShadowCascade MakeShadowCascade(const BoundingSphere& sphere, const glm::vec3& towardsLight, uint32_t resolution, float casterReach);

	// How far in front of `center` towards the light the furthest corner of `bounds` is, in
	// metres; 0 when none is. The cascade around `center` reaches as far (MakeShadowCascade()).
	float GetCasterReach(const BoundingBox& bounds, const glm::vec3& center, const glm::vec3& towardsLight);

	// The light's view, turned only: the same for every cascade and frame while the light doesn't
	// turn, so snapping to its texels keeps them in place
	glm::mat4 MakeLightView(const glm::vec3& towardsLight);
}
