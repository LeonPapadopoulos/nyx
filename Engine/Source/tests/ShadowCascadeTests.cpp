// The directional light's cascades without a GPU (ShadowCascades.h): the splits from the near
// plane to the shadow distance, even, logarithmic and between; the sphere around each slice of the
// camera's view holds the slice's corners, is hardly bigger than it must be, and keeps its radius
// however the camera turns; the light's view of a sphere holds it, its depth reversed and reaching
// back to what casts into it; and its texels stay where they are in the world as the camera moves
// and turns, so still shadows don't shimmer.
#include "CameraProjection.h"
#include "ShadowCascades.h"
#include "TestSupport.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	using namespace Nyx;
	using Nyx::Tests::Require;

	constexpr float FovY = glm::radians(60.0f);
	constexpr float Aspect = 16.0f / 9.0f;
	constexpr float NearPlane = 0.1f;

	ShadowCamera CameraAt(const glm::vec3& position, const glm::vec3& eulerDegrees)
	{
		TransformComponent transform;
		transform.Position = position;
		transform.Rotation = glm::quat(glm::radians(eulerDegrees));
		return MakeShadowCamera(GetCameraView(transform), MakePerspectiveProjection(FovY, Aspect, NearPlane, 1000.0f), NearPlane);
	}

	// The slice's corners: at `depth` along the camera's view, to its edges
	std::vector<glm::vec3> SliceCorners(const ShadowCamera& camera, float nearDepth, float farDepth)
	{
		std::vector<glm::vec3> corners;
		for (const float depth : { nearDepth, farDepth })
		{
			for (const float x : { -1.0f, 1.0f })
			{
				for (const float y : { -1.0f, 1.0f })
				{
					const glm::vec3 local(x * depth * camera.TanHalfFovY * camera.AspectRatio, y * depth * camera.TanHalfFovY, -depth);
					corners.push_back(glm::vec3(camera.CameraToWorld * glm::vec4(local, 1.0f)));
				}
			}
		}
		return corners;
	}

	glm::vec3 ToClip(const ShadowCascade& cascade, const glm::vec3& point)
	{
		const glm::vec4 clip = cascade.ViewProjection * glm::vec4(point, 1.0f);
		return glm::vec3(clip) / clip.w;
	}

	void TestSplits()
	{
		for (uint32_t count = 1; count <= MaxShadowCascades; ++count)
		{
			const std::array<float, MaxShadowCascades + 1> splits = ComputeCascadeSplits(NearPlane, 80.0f, count, 0.7f);
			Require(splits[0] == NearPlane && splits[count] == 80.0f, "The splits don't run from the near plane to the shadow distance");
			for (uint32_t index = 1; index <= count; ++index)
			{
				Require(splits[index] > splits[index - 1], "The splits don't rise");
			}
		}

		// Even: as far apart; logarithmic: each the same times the one before
		const std::array<float, MaxShadowCascades + 1> even = ComputeCascadeSplits(1.0f, 81.0f, 4, 0.0f);
		for (uint32_t index = 1; index <= 4; ++index)
		{
			Require(std::abs(even[index] - (1.0f + 20.0f * static_cast<float>(index))) < 1e-3f, "Even splits aren't evenly apart");
		}
		const std::array<float, MaxShadowCascades + 1> logarithmic = ComputeCascadeSplits(1.0f, 81.0f, 4, 1.0f);
		for (uint32_t index = 1; index <= 4; ++index)
		{
			Require(std::abs(logarithmic[index] - std::pow(3.0f, static_cast<float>(index))) < 1e-3f, "Logarithmic splits aren't each 3 times the one before: " + std::to_string(logarithmic[index]));
		}

		// Counts out of range are clamped
		Require(ComputeCascadeSplits(NearPlane, 50.0f, 0, 0.5f)[1] == 50.0f, "No cascades isn't one cascade");
		Require(ComputeCascadeSplits(NearPlane, 50.0f, 9, 0.5f)[4] == 50.0f, "Nine cascades aren't four");
	}

	void TestSpheres()
	{
		const ShadowCamera camera = CameraAt(glm::vec3(3.0f, 2.0f, -5.0f), glm::vec3(-20.0f, 35.0f, 0.0f));
		const std::array<float, MaxShadowCascades + 1> splits = ComputeCascadeSplits(NearPlane, 120.0f, 4, 0.75f);
		for (uint32_t cascade = 0; cascade < 4; ++cascade)
		{
			const BoundingSphere sphere = FitCascadeSphere(camera, splits[cascade], splits[cascade + 1]);
			float farthest = 0.0f;
			for (const glm::vec3& corner : SliceCorners(camera, splits[cascade], splits[cascade + 1]))
			{
				farthest = std::max(farthest, glm::length(corner - sphere.Center));
			}
			Require(farthest <= sphere.Radius + 1e-3f, "Cascade " + std::to_string(cascade) + "'s sphere doesn't hold its slice's corners");
			// The smallest that does, but for the rounding up
			Require(sphere.Radius - farthest < 1.0f / 32.0f, "Cascade " + std::to_string(cascade) + "'s sphere is bigger than it must be: " + std::to_string(sphere.Radius) + " for " + std::to_string(farthest));
		}

		// Turned and moved, the same radius
		const BoundingSphere first = FitCascadeSphere(camera, 5.0f, 20.0f);
		for (const glm::vec3& euler : { glm::vec3(0.0f), glm::vec3(-89.0f, 0.0f, 0.0f), glm::vec3(10.0f, 170.0f, 5.0f), glm::vec3(-33.3f, -71.7f, 0.0f) })
		{
			const BoundingSphere turned = FitCascadeSphere(CameraAt(glm::vec3(-40.0f, 7.5f, 12.25f), euler), 5.0f, 20.0f);
			Require(turned.Radius == first.Radius, "The sphere's radius changes as the camera turns: " + std::to_string(turned.Radius) + " for " + std::to_string(first.Radius));
		}

		// A long thin slice of a wide view: the centre is at the far end
		const BoundingSphere far = FitCascadeSphere(camera, 0.1f, 100.0f);
		const glm::vec3 forward = -glm::normalize(glm::vec3(camera.CameraToWorld[2]));
		const float centerDepth = glm::dot(far.Center - glm::vec3(camera.CameraToWorld[3]), forward);
		Require(centerDepth <= 100.0f + 1e-3f && centerDepth > 50.0f, "A wide slice's sphere isn't centred towards its far end");
	}

	void TestCascades()
	{
		const glm::vec3 towardsLight = glm::normalize(glm::vec3(0.4f, 1.0f, 0.2f));
		const BoundingSphere sphere{ glm::vec3(10.0f, 1.0f, -20.0f), 12.0f };
		const ShadowCascade cascade = MakeShadowCascade(sphere, towardsLight, 2048, 50.0f);
		Require(std::abs(cascade.TexelSize - 24.0f / 2048.0f) < 1e-6f, "A texel isn't the diameter over the resolution");

		// The sphere is inside: across, and in depth
		for (const glm::vec3& offset : { glm::vec3(1, 0, 0), glm::vec3(-1, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, -1, 0), glm::vec3(0, 0, 1), glm::vec3(0, 0, -1), glm::normalize(glm::vec3(1, 1, 1)) })
		{
			const glm::vec3 clip = ToClip(cascade, sphere.Center + offset * sphere.Radius * 0.999f);
			Require(std::abs(clip.x) <= 1.0f && std::abs(clip.y) <= 1.0f && clip.z >= 0.0f && clip.z <= 1.0f, "A point of the sphere is outside its cascade");
		}
		// Reversed depth: nearer the light is greater. What is 50 m towards the light still casts
		// into it; what is further isn't in it.
		const float centerDepth = ToClip(cascade, sphere.Center).z;
		Require(ToClip(cascade, sphere.Center + towardsLight * 5.0f).z > centerDepth, "Nearer the light isn't the greater depth");
		Require(std::abs(ToClip(cascade, sphere.Center + towardsLight * 50.0f).z - 1.0f) < 1e-4f, "The cascade's near plane isn't as far towards the light as the casters reach");
		Require(std::abs(ToClip(cascade, sphere.Center - towardsLight * sphere.Radius).z) < 1e-4f, "The cascade's far plane isn't at the sphere's far side");
		// Without casters beyond it, the near plane is the sphere's near side
		const ShadowCascade bare = MakeShadowCascade(sphere, towardsLight, 2048, 0.0f);
		Require(std::abs(ToClip(bare, sphere.Center + towardsLight * sphere.Radius).z - 1.0f) < 1e-4f, "Without casters, the near plane isn't at the sphere's near side");

		// The casters' reach: a box above the sphere towards the light, and one below
		const BoundingBox above{ sphere.Center + towardsLight * 30.0f - glm::vec3(1.0f), sphere.Center + towardsLight * 30.0f + glm::vec3(1.0f) };
		Require(GetCasterReach(above, sphere.Center, towardsLight) > 30.0f, "A box towards the light doesn't reach as far");
		const BoundingBox below{ sphere.Center - towardsLight * 30.0f - glm::vec3(1.0f), sphere.Center - towardsLight * 30.0f + glm::vec3(1.0f) };
		Require(GetCasterReach(below, sphere.Center, towardsLight) == 0.0f, "A box away from the light reaches towards it");
	}

	// Where a point of the world lands in the shadow map, in texels across its first axis, and how
	// far into its texel
	float TexelOffset(const ShadowCascade& cascade, const glm::vec3& point, uint32_t resolution)
	{
		const float texel = (ToClip(cascade, point).x * 0.5f + 0.5f) * static_cast<float>(resolution);
		return texel - std::floor(texel);
	}

	void TestTexelSnapping()
	{
		const glm::vec3 towardsLight = glm::normalize(glm::vec3(-0.3f, 0.9f, 0.45f));
		const glm::vec3 still(4.2f, 0.3f, -7.9f);
		constexpr uint32_t Resolution = 1024;

		// The camera walks and turns: the cascade moves, but a still point stays as far into its
		// texel, so it is drawn by the same texels' worth of the map
		float reference = -1.0f;
		for (int step = 0; step < 40; ++step)
		{
			const float t = static_cast<float>(step);
			const ShadowCamera camera = CameraAt(glm::vec3(0.37f * t, 1.7f + 0.01f * t, -0.53f * t), glm::vec3(-10.0f - 0.5f * t, 3.1f * t, 0.0f));
			const BoundingSphere sphere = FitCascadeSphere(camera, 0.1f, 15.0f);
			const ShadowCascade cascade = MakeShadowCascade(sphere, towardsLight, Resolution, 20.0f);
			const float offset = TexelOffset(cascade, still, Resolution);
			if (reference < 0.0f)
			{
				reference = offset;
			}
			const float drift = std::min(std::abs(offset - reference), 1.0f - std::abs(offset - reference));
			Require(drift < 0.02f, "A still point drifts within its texel as the camera moves: by " + std::to_string(drift) + " at step " + std::to_string(step));
		}

		// Without snapping, it would drift: the sphere's centre moved by a third of a texel
		const BoundingSphere sphere{ glm::vec3(0.0f), 8.0f };
		const float texel = 2.0f * sphere.Radius / static_cast<float>(Resolution);
		const glm::vec3 across = glm::vec3(glm::inverse(MakeLightView(towardsLight)) * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
		const ShadowCascade before = MakeShadowCascade(sphere, towardsLight, Resolution, 0.0f);
		const ShadowCascade after = MakeShadowCascade(BoundingSphere{ sphere.Center + across * texel / 3.0f, sphere.Radius }, towardsLight, Resolution, 0.0f);
		Require(std::abs(TexelOffset(before, still, Resolution) - TexelOffset(after, still, Resolution)) < 1e-3f, "A third of a texel's move changes where a still point is in its texel");
	}

	void TestShadowCamera()
	{
		const ShadowCamera camera = CameraAt(glm::vec3(1.0f, 2.0f, 3.0f), glm::vec3(-30.0f, 45.0f, 0.0f));
		Require(std::abs(camera.TanHalfFovY - std::tan(FovY * 0.5f)) < 1e-5f, "The camera's field of view isn't found in its projection");
		Require(std::abs(camera.AspectRatio - Aspect) < 1e-4f, "The camera's aspect ratio isn't found in its projection");
		Require(glm::length(glm::vec3(camera.CameraToWorld[3]) - glm::vec3(1.0f, 2.0f, 3.0f)) < 1e-4f, "The camera isn't where its view says");
	}
}

int main()
{
	try
	{
		TestSplits();
		TestSpheres();
		TestCascades();
		TestTexelSnapping();
		TestShadowCamera();
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All shadow cascade tests passed\n";
	return 0;
}
