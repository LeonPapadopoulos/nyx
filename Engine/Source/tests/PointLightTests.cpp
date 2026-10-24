// Point lights without a GPU (PointLights.h): the falloff, inverse square softened to 0 at the
// range; a world's lights as a frame draws them, those that give no light left out, a moving one
// between its last two steps; and a view's culling of them: spheres of light inside, outside and
// across the frustum's planes, behind the camera and past its far plane, and past the most a view
// lights its pixels with, those reaching nearest the camera kept in the world's order.
#include "CameraProjection.h"
#include "Entity.h"
#include "PointLightComponent.h"
#include "PointLights.h"
#include "PreviousTransformComponent.h"
#include "SceneCulling.h"
#include "TestSupport.h"
#include "TransformComponent.h"

#include <glm/glm.hpp>

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	using namespace Nyx;
	using Nyx::Tests::Require;

	bool Near(float a, float b, float tolerance = 1e-5f)
	{
		return std::abs(a - b) <= tolerance;
	}

	// At the origin looking along -Z, 90 degrees up and across, from 0.1 to 100 m
	Frustum MakeTestFrustum()
	{
		return MakeFrustum(MakePerspectiveProjection(glm::radians(90.0f), 1.0f, 0.1f, 100.0f));
	}

	RenderPointLight Light(const glm::vec3& position, float range)
	{
		RenderPointLight light;
		light.Position = position;
		light.WorldPosition = position;
		light.Range = range;
		light.Color = glm::vec3(1.0f);
		return light;
	}

	void TestFalloff()
	{
		// The inverse square, nearly, well inside a long range
		Require(Near(GetPointLightFalloff(1.0f, 1000.0f), 1.0f, 1e-6f), "At a metre from a far-reaching light, its whole intensity");
		Require(Near(GetPointLightFalloff(2.0f, 1000.0f), 0.25f, 1e-5f), "At 2 m, a quarter");
		// Never brighter than a small bulb's distance
		Require(Near(GetPointLightFalloff(0.0f, 10.0f), GetPointLightFalloff(MinPointLightDistance, 10.0f)), "At the light, as at the bulb's size");
		Require(GetPointLightFalloff(0.0f, 10.0f) < 101.0f, "At the light, finite");

		// 0 at and past the range, more than 0 just inside it, falling all the way
		Require(GetPointLightFalloff(8.0f, 8.0f) == 0.0f && GetPointLightFalloff(9.0f, 8.0f) == 0.0f, "Nothing at or past the range");
		Require(GetPointLightFalloff(7.9f, 8.0f) > 0.0f, "Something just inside the range");
		float previous = GetPointLightFalloff(0.1f, 8.0f);
		for (float distance = 0.2f; distance < 8.0f; distance += 0.1f)
		{
			const float falloff = GetPointLightFalloff(distance, 8.0f);
			Require(falloff < previous, "The light falls with the distance (at " + std::to_string(distance) + " m)");
			previous = falloff;
		}
		// Softly: near the range it is far below the plain inverse square, and its slope goes to 0
		const float edge = GetPointLightFalloff(7.6f, 8.0f) * 7.6f * 7.6f;
		Require(edge > 0.0f && edge < 0.05f, "Near the range, the window has nearly closed (" + std::to_string(edge) + ")");
		Require(GetPointLightFalloff(7.99f, 8.0f) < 1e-5f, "At the range it reaches 0 without a step");
		// The window (1 - (d / r)^4)^2 at half the range
		Require(Near(GetPointLightFalloff(4.0f, 8.0f) * 16.0f, (1.0f - 1.0f / 16.0f) * (1.0f - 1.0f / 16.0f)), "The window at half the range");
		Require(GetPointLightFalloff(1.0f, 0.0f) == 0.0f && GetPointLightFalloff(0.0f, -1.0f) == 0.0f, "No range, no light");
	}

	void TestExtraction()
	{
		Registry world;
		const auto add = [&](const glm::vec3& position, const PointLightComponent& light)
		{
			const Entity entity = world.CreateEntity();
			world.Add<TransformComponent>(entity, TransformComponent{ .Position = position });
			world.Add<PointLightComponent>(entity, light);
			return entity;
		};
		const Entity white = add(glm::vec3(1.0f, 2.0f, 3.0f), PointLightComponent{});
		add(glm::vec3(0.0f), PointLightComponent{ .Intensity = 0.0f });
		add(glm::vec3(0.0f), PointLightComponent{ .Range = 0.0f });
		add(glm::vec3(0.0f), PointLightComponent{ .Color = glm::vec3(0.0f) });
		add(glm::vec3(0.0f), PointLightComponent{ .Color = glm::vec3(-1.0f, -1.0f, -1.0f) });
		const Entity orange = add(glm::vec3(5.0f, 0.0f, 0.0f), PointLightComponent{ .Color = glm::vec3(1.0f, 0.5f, 0.0f), .Intensity = 4.0f, .Range = 3.0f });

		std::vector<RenderPointLight> lights;
		ExtractPointLights(world, std::nullopt, lights);
		Require(lights.size() == 2, "Only the lights that give light are drawn, not " + std::to_string(lights.size()));
		Require(lights[0].SourceEntity == white && lights[0].Position == glm::vec3(1.0f, 2.0f, 3.0f) && lights[0].Range == 8.0f, "The white light where its entity is");
		Require(lights[1].SourceEntity == orange && lights[1].Color == glm::vec3(4.0f, 2.0f, 0.0f) && lights[1].Range == 3.0f, "The colour times the intensity");
		Require(lights[0].PickingId == 0, "No picking ids given");

		// Moving with its steps: between them where the world is drawn so, picked where it is
		PreviousTransformComponent previous;
		SetPreviousTransform(previous, glm::vec3(3.0f, 0.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), world.Get<TransformComponent>(orange));
		world.Add<PreviousTransformComponent>(orange, previous);
		ExtractPointLights(world, 0.5f, lights);
		Require(glm::all(glm::lessThan(glm::abs(lights[1].Position - glm::vec3(4.0f, 0.0f, 0.0f)), glm::vec3(1e-5f))), "Halfway between its last two steps");
		Require(lights[1].WorldPosition == glm::vec3(5.0f, 0.0f, 0.0f), "Picked where it is");
		ExtractPointLights(world, std::nullopt, lights);
		Require(lights[1].Position == glm::vec3(5.0f, 0.0f, 0.0f), "Where it is in a world drawn as it is");
	}

	void TestCulling()
	{
		const Frustum frustum = MakeTestFrustum();
		const glm::vec3 camera(0.0f);
		const std::vector<RenderPointLight> lights = {
			Light(glm::vec3(0.0f, 0.0f, -10.0f), 2.0f),	   // 0: ahead
			Light(glm::vec3(0.0f, 0.0f, 10.0f), 2.0f),	   // 1: behind, out of reach
			Light(glm::vec3(0.0f, 0.0f, 1.5f), 2.0f),	   // 2: behind, but its light reaches past the camera
			Light(glm::vec3(30.0f, 0.0f, -10.0f), 5.0f),   // 3: far to the right
			Light(glm::vec3(12.0f, 0.0f, -10.0f), 5.0f),   // 4: right of the edge, its light across it
			Light(glm::vec3(0.0f, 0.0f, -104.0f), 5.0f),   // 5: past the far plane, its light across it
			Light(glm::vec3(0.0f, 0.0f, -110.0f), 5.0f),   // 6: past the far plane, out of reach
			Light(glm::vec3(0.0f, -20.0f, -10.0f), 5.0f),  // 7: below
		};
		std::vector<ViewPointLight> visible;
		const PointLightCullStats stats = CullPointLights(lights, frustum, camera, MaxViewPointLights, visible);
		Require(stats.Lights == 8 && stats.Culled == 4 && stats.Dropped == 0,
			"4 of 8 lights culled, none dropped, not " + std::to_string(stats.Culled) + " and " + std::to_string(stats.Dropped));
		Require(visible.size() == 4, "4 lights in view");
		const glm::vec3 expected[] = { lights[0].Position, lights[2].Position, lights[4].Position, lights[5].Position };
		for (size_t index = 0; index < visible.size(); ++index)
		{
			Require(visible[index].Position == expected[index], "The lights in view in the world's order");
		}
		Require(Near(visible[0].InverseRangeSquared, 0.25f) && visible[0].Color == glm::vec3(1.0f), "1 / range², and the colour");

		Require(IsSphereVisible(frustum, glm::vec3(0.0f, 0.0f, -10.0f), 0.0f), "A point ahead is in view");
		Require(!IsSphereVisible(frustum, glm::vec3(0.0f, 0.0f, 10.0f), 9.9f), "A sphere wholly behind isn't");

		// No lights, none in view
		Require(CullPointLights({}, frustum, camera, MaxViewPointLights, visible).Lights == 0 && visible.empty(), "No lights");
	}

	void TestTooManyLights()
	{
		const Frustum frustum = MakeTestFrustum();
		// Ten lights in a row ahead, the farthest first, each 1 m in range
		std::vector<RenderPointLight> lights;
		for (int index = 0; index < 10; ++index)
		{
			lights.push_back(Light(glm::vec3(0.0f, 0.0f, -20.0f + index * 1.5f), 1.0f));
		}
		std::vector<ViewPointLight> visible;
		PointLightCullStats stats = CullPointLights(lights, frustum, glm::vec3(0.0f), 4, visible);
		Require(stats.Culled == 0 && stats.Dropped == 6 && visible.size() == 4, "4 kept, 6 dropped");
		// The four nearest, in the world's order
		for (size_t index = 0; index < visible.size(); ++index)
		{
			Require(visible[index].Position == lights[6 + index].Position, "The nearest kept, in the world's order");
		}

		// A light whose range takes in the camera comes before a nearer, smaller one
		lights = { Light(glm::vec3(0.0f, 0.0f, -2.0f), 0.5f), Light(glm::vec3(0.0f, 0.0f, -6.0f), 10.0f) };
		stats = CullPointLights(lights, frustum, glm::vec3(0.0f), 1, visible);
		Require(stats.Dropped == 1 && visible.size() == 1 && visible[0].Position == lights[1].Position, "The light the camera is inside kept");

		// Ties keep the world's order
		lights = { Light(glm::vec3(1.0f, 0.0f, -5.0f), 1.0f), Light(glm::vec3(-1.0f, 0.0f, -5.0f), 1.0f), Light(glm::vec3(0.0f, 1.0f, -5.0f), 1.0f) };
		CullPointLights(lights, frustum, glm::vec3(0.0f), 2, visible);
		Require(visible.size() == 2 && visible[0].Position == lights[0].Position && visible[1].Position == lights[1].Position, "Ties in the world's order");
	}
}

int main()
{
	try
	{
		TestFalloff();
		TestExtraction();
		TestCulling();
		TestTooManyLights();
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All point light tests passed\n";
	return 0;
}
