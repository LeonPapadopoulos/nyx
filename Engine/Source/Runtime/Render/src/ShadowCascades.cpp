#include "NyxPCH.h"
#include "ShadowCascades.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace Nyx
{
	ShadowCamera MakeShadowCamera(const glm::mat4& view, const glm::mat4& projection, float nearPlane)
	{
		// MakePerspectiveProjection(): [1][1] is 1 / tan(fovY / 2), turned for Vulkan's Y, and
		// [0][0] that over the aspect ratio
		ShadowCamera camera;
		camera.CameraToWorld = glm::inverse(view);
		camera.TanHalfFovY = 1.0f / std::max(std::abs(projection[1][1]), 1e-6f);
		camera.AspectRatio = std::abs(projection[1][1]) / std::max(std::abs(projection[0][0]), 1e-6f);
		camera.NearPlane = nearPlane;
		return camera;
	}

	std::array<float, MaxShadowCascades + 1> ComputeCascadeSplits(float nearPlane, float distance, uint32_t count, float blend)
	{
		count = std::clamp(count, 1u, MaxShadowCascades);
		const float nearest = std::max(nearPlane, 1e-3f);
		const float farthest = std::max(distance, nearest + 0.01f);
		blend = std::clamp(blend, 0.0f, 1.0f);

		std::array<float, MaxShadowCascades + 1> splits{};
		splits.fill(farthest);
		splits[0] = nearest;
		for (uint32_t index = 1; index < count; ++index)
		{
			const float part = static_cast<float>(index) / static_cast<float>(count);
			const float logarithmic = nearest * std::pow(farthest / nearest, part);
			const float even = nearest + (farthest - nearest) * part;
			splits[index] = even + (logarithmic - even) * blend;
		}
		return splits;
	}

	BoundingSphere FitCascadeSphere(const ShadowCamera& camera, float nearDepth, float farDepth)
	{
		// At depth d along the axis, the slice's corners are d * k off it. The centre at depth c is
		// as far from the near corners as from the far ones where
		// (c - n)^2 + (n k)^2 = (f - c)^2 + (f k)^2, i.e. c = (n + f)(1 + k^2) / 2; past the far
		// plane, the far corners alone decide, around the far plane's middle.
		const float k2 = camera.TanHalfFovY * camera.TanHalfFovY * (1.0f + camera.AspectRatio * camera.AspectRatio);
		const float n = std::max(nearDepth, 0.0f);
		const float f = std::max(farDepth, n);
		const float centerDepth = std::min(0.5f * (n + f) * (1.0f + k2), f);
		const float radius = std::sqrt((f - centerDepth) * (f - centerDepth) + f * f * k2);

		const glm::vec3 position(camera.CameraToWorld[3]);
		const glm::vec3 forward = -glm::normalize(glm::vec3(camera.CameraToWorld[2]));
		BoundingSphere sphere;
		sphere.Center = position + forward * centerDepth;
		// Rounded up a little, so that rounding in the matrices can't make it differ frame to frame
		sphere.Radius = std::ceil(radius * 64.0f) / 64.0f;
		return sphere;
	}

	glm::mat4 MakeLightView(const glm::vec3& towardsLight)
	{
		const glm::vec3 direction = glm::normalize(towardsLight);
		// Any up that isn't along the light
		const glm::vec3 up = std::abs(direction.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
		return glm::lookAtRH(glm::vec3(0.0f), -direction, up);
	}

	float GetCasterReach(const BoundingBox& bounds, const glm::vec3& center, const glm::vec3& towardsLight)
	{
		const glm::vec3 direction = glm::normalize(towardsLight);
		float reach = 0.0f;
		for (int corner = 0; corner < 8; ++corner)
		{
			const glm::vec3 point((corner & 1) ? bounds.Max.x : bounds.Min.x, (corner & 2) ? bounds.Max.y : bounds.Min.y, (corner & 4) ? bounds.Max.z : bounds.Min.z);
			reach = std::max(reach, glm::dot(point - center, direction));
		}
		return reach;
	}

	ShadowCascade MakeShadowCascade(const BoundingSphere& sphere, const glm::vec3& towardsLight, uint32_t resolution, float casterReach)
	{
		const glm::mat4 lightView = MakeLightView(towardsLight);
		const float radius = std::max(sphere.Radius, 1e-3f);
		const float texel = 2.0f * radius / static_cast<float>(std::max(resolution, 1u));

		// The centre in the light's view, moved to the nearest whole texel across it
		glm::vec3 center = glm::vec3(lightView * glm::vec4(sphere.Center, 1.0f));
		center.x = std::round(center.x / texel) * texel;
		center.y = std::round(center.y / texel) * texel;

		// The light looks along its view's -Z: towards the light is +Z. Depth runs from the near
		// side, as far towards the light as the casters reach, to the sphere's far side.
		const float nearDistance = -(center.z + std::max(radius, casterReach));
		const float farDistance = -(center.z - radius);
		// Reversed, as the scene's depth: the near plane's depth 1, the far plane's 0
		glm::mat4 projection = glm::orthoRH_ZO(center.x - radius, center.x + radius, center.y - radius, center.y + radius, farDistance, nearDistance);
		// Vulkan's Y points down the picture, as the camera's (MakePerspectiveProjection()). The whole
		// of clip Y turns: the box is off the light view's axis, so its offset turns with its scale,
		// or the map would cover the box mirrored about the axis, not the sphere.
		projection[1][1] *= -1.0f;
		projection[3][1] *= -1.0f;

		ShadowCascade cascade;
		cascade.ViewProjection = projection * lightView;
		cascade.Sphere = sphere;
		cascade.TexelSize = texel;
		return cascade;
	}
}
