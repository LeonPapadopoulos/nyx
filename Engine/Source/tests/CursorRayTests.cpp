// The cursor ray: a world point drawn by the renderer's view and projection lands on a pixel whose
// ray passes through the point, for cameras moved and turned, at the picture's centre and corners,
// and just past the near plane, where a box is drawn and hit; the primary camera found as the
// renderer finds it; the physics pick along a ray, which ignores a body or sensors when asked and
// finds nothing past everything; an anchor made where it hit.
#include "CameraProjection.h"
#include "GameWorld.h"
#include "GuidComponent.h"
#include "Log.h"
#include "PhysicsComponents.h"
#include "PhysicsPick.h"
#include "SurfaceAnchor.h"
#include "TestSupport.h"
#include "TransformComponent.h"

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace
{
	using namespace Nyx;
	using Nyx::Tests::Require;

	constexpr float Tolerance = 1e-3f;

	bool Near(const glm::vec3& a, const glm::vec3& b, float tolerance = Tolerance)
	{
		return glm::length(a - b) < tolerance;
	}

	bool Near(const glm::vec2& a, const glm::vec2& b, float tolerance = Tolerance)
	{
		return glm::length(a - b) < tolerance;
	}

	std::string ToString(const glm::vec3& v)
	{
		return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
	}

	// The pixel the renderer draws a world point on, with the view and projection it draws with
	glm::vec2 ProjectToPixel(const TransformComponent& transform, const CameraComponent& camera,
		const glm::vec2& viewportSize, const glm::vec3& point)
	{
		const glm::mat4 viewProjection =
			GetCameraProjection(camera, GetAspectRatio(viewportSize.x, viewportSize.y)) * GetCameraView(transform);
		const glm::vec4 clip = viewProjection * glm::vec4(point, 1.0f);
		const glm::vec2 ndc = glm::vec2(clip) / clip.w;
		return (ndc + 1.0f) * 0.5f * viewportSize;
	}

	// How far the point is from the ray's line, and that it is in front of the ray's origin
	void RequireOnRay(const Ray& ray, const glm::vec3& point, const std::string& what)
	{
		Require(std::abs(glm::length(ray.Direction) - 1.0f) < 1e-5f, what + ": the direction should be unit length");
		const glm::vec3 toPoint = point - ray.Origin;
		Require(glm::dot(toPoint, ray.Direction) > 0.0f, what + ": the point should be ahead of the ray");
		const float offRay = glm::length(glm::cross(toPoint, ray.Direction));
		Require(offRay < Tolerance, what + ": the ray should pass through " + ToString(point) + " (" + std::to_string(offRay) + " off it)");
	}

	TransformComponent MovedAndTurnedCamera()
	{
		TransformComponent transform;
		transform.Position = glm::vec3(3.0f, 2.0f, -5.0f);
		transform.Rotation = glm::angleAxis(glm::radians(40.0f), glm::vec3(0.0f, 1.0f, 0.0f)) *
			glm::angleAxis(glm::radians(-20.0f), glm::vec3(1.0f, 0.0f, 0.0f)) *
			glm::angleAxis(glm::radians(10.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		return transform;
	}

	void TestProjectedPointsLieOnTheirPixelsRays()
	{
		const CameraComponent camera{ .FovYDegrees = 50.0f, .NearPlane = 0.3f, .FarPlane = 500.0f };
		const glm::vec2 viewports[] = { glm::vec2(1280.0f, 720.0f), glm::vec2(600.0f, 800.0f) };
		const TransformComponent cameras[] = { TransformComponent{}, MovedAndTurnedCamera() };
		// In front of the camera, in its own space: ahead, off to the sides, near and far
		const glm::vec3 localPoints[] = { glm::vec3(0.0f, 0.0f, -10.0f), glm::vec3(1.5f, -0.7f, -4.0f),
			glm::vec3(-6.0f, 3.0f, -20.0f), glm::vec3(0.1f, 0.2f, -0.5f), glm::vec3(30.0f, -10.0f, -300.0f) };

		for (const TransformComponent& transform : cameras)
		{
			const glm::vec3 forward = transform.Rotation * glm::vec3(0.0f, 0.0f, -1.0f);
			for (const glm::vec2& viewport : viewports)
			{
				for (const glm::vec3& local : localPoints)
				{
					const glm::vec3 point = transform.Position + transform.Rotation * local;
					const std::string what = "Point " + ToString(point) + " in a " + std::to_string(int(viewport.x)) + "x" +
						std::to_string(int(viewport.y)) + " view";

					const glm::vec2 pixel = ProjectToPixel(transform, camera, viewport, point);
					const Ray ray = ScreenPointToRay(transform, camera, viewport, pixel);
					RequireOnRay(ray, point, what);
					Require(std::abs(glm::dot(ray.Origin - transform.Position, forward) - camera.NearPlane) < 1e-4f,
						what + ": the ray should start on the near plane");
				}
			}
		}
	}

	// Back from the world to the picture: the pixel each point projects to, and none behind the camera
	void TestWorldPointsShowOnTheirPixels()
	{
		const CameraComponent camera{ .FovYDegrees = 50.0f, .NearPlane = 0.3f, .FarPlane = 500.0f };
		const glm::vec2 viewport(1280.0f, 720.0f);
		const TransformComponent transform = MovedAndTurnedCamera();
		const glm::vec3 localPoints[] = { glm::vec3(0.0f, 0.0f, -10.0f), glm::vec3(1.5f, -0.7f, -4.0f), glm::vec3(30.0f, -10.0f, -300.0f) };
		for (const glm::vec3& local : localPoints)
		{
			const glm::vec3 point = transform.Position + transform.Rotation * local;
			const std::optional<glm::vec2> pixel = WorldToScreenPoint(transform, camera, viewport, point);
			Require(pixel.has_value(), "A point ahead of the camera should show");
			Require(glm::length(*pixel - ProjectToPixel(transform, camera, viewport, point)) < 0.01f,
				"It should show where the renderer's matrices put it: " + ToString(point));
			RequireOnRay(ScreenPointToRay(transform, camera, viewport, *pixel), point, "Its pixel's ray");
		}

		const glm::vec3 behind = transform.Position + transform.Rotation * glm::vec3(0.5f, 0.0f, 3.0f);
		Require(!WorldToScreenPoint(transform, camera, viewport, behind), "A point behind the camera should show nowhere");
	}

	void TestTheCentreAndCorners()
	{
		const CameraComponent camera{ .FovYDegrees = 60.0f };
		const glm::vec2 viewport(1600.0f, 900.0f);
		const float tanHalfFov = std::tan(glm::radians(camera.FovYDegrees) * 0.5f);
		const float aspect = viewport.x / viewport.y;

		for (const TransformComponent& transform : { TransformComponent{}, MovedAndTurnedCamera() })
		{
			const glm::quat toLocal = glm::inverse(transform.Rotation);

			const Ray centre = ScreenPointToRay(transform, camera, viewport, viewport * 0.5f);
			Require(Near(toLocal * centre.Direction, glm::vec3(0.0f, 0.0f, -1.0f), 1e-5f),
				"The picture's centre should look along the camera's forward axis");

			// Pixel (0, 0) is the top left corner: up and to the left, by half the field of view up
			// and half of it times the aspect ratio across
			const glm::vec2 corners[] = { glm::vec2(0.0f), glm::vec2(viewport.x, 0.0f), glm::vec2(0.0f, viewport.y), viewport };
			for (const glm::vec2& corner : corners)
			{
				const Ray ray = ScreenPointToRay(transform, camera, viewport, corner);
				const glm::vec3 local = toLocal * ray.Direction;
				const glm::vec2 expected(corner.x == 0.0f ? -aspect * tanHalfFov : aspect * tanHalfFov,
					corner.y == 0.0f ? tanHalfFov : -tanHalfFov);
				Require(Near(glm::vec2(local) / -local.z, expected, 1e-5f),
					"Corner (" + std::to_string(corner.x) + ", " + std::to_string(corner.y) + ") should look through that corner of the view");

				// And a point along it is drawn on that corner
				Require(Near(ProjectToPixel(transform, camera, viewport, ray.Origin + ray.Direction * 25.0f), corner, 0.05f),
					"A point along a corner's ray should be drawn on that corner");
			}
		}
	}

	// A box whose front face is just past the near plane, at 1.2 times it: inside Vulkan's clip
	// volume (0 <= z <= w), so drawn, and hit by the ray through where it is drawn
	void TestJustPastTheNearPlane()
	{
		const CameraComponent camera{ .NearPlane = 0.3f, .FarPlane = 500.0f };
		const glm::vec2 viewport(1280.0f, 720.0f);
		const TransformComponent transform = MovedAndTurnedCamera();
		const float distance = camera.NearPlane * 1.2f;

		GameWorld world;
		Registry& registry = world.GetRegistry();
		const Entity box = registry.CreateEntity();
		registry.Add<TransformComponent>(box, TransformComponent{ .Position = transform.Position + transform.Rotation * glm::vec3(0.0f, 0.0f, -distance - 0.5f), .Rotation = transform.Rotation });
		registry.Add<BoxColliderComponent>(box, BoxColliderComponent{ .HalfExtents = glm::vec3(0.5f) });
		world.Step(StepTime{ 1.0 / 60.0, 0, 0.0 });

		const glm::vec3 point = transform.Position + transform.Rotation * glm::vec3(0.02f, -0.01f, -distance);
		const glm::mat4 viewProjection = GetCameraProjection(camera, GetAspectRatio(viewport.x, viewport.y)) * GetCameraView(transform);
		const glm::vec4 clip = viewProjection * glm::vec4(point, 1.0f);
		Require(clip.z > 0.0f && clip.z < clip.w, "A point at 1.2 times the near plane should be inside the clip volume (depth " + std::to_string(clip.z / clip.w) + ")");

		const Ray ray = ScreenPointToRay(transform, camera, viewport, ProjectToPixel(transform, camera, viewport, point));
		RequireOnRay(ray, point, "A point just past the near plane");
		const std::optional<PhysicsPick> pick = PickPhysicsBody(world, ray, camera.FarPlane);
		Require(pick && pick->HitEntity == box, "The ray should hit the box just past the near plane");
		Require(Near(pick->Point, point), "The ray should hit the box where it is drawn " + ToString(pick->Point));
	}

	void TestThePrimaryCameraIsFoundAsTheRendererFindsIt()
	{
		Registry world;
		Require(!FindPrimaryCamera(world), "A world without cameras has no primary camera");

		const Entity secondary = world.CreateEntity();
		world.Add<CameraComponent>(secondary, CameraComponent{ .bPrimary = false });
		world.Add<TransformComponent>(secondary, TransformComponent{ .Position = glm::vec3(9.0f) });
		const Entity primary = world.CreateEntity();
		world.Add<CameraComponent>(primary, CameraComponent{ .FovYDegrees = 45.0f });
		world.Add<TransformComponent>(primary, TransformComponent{ .Position = glm::vec3(1.0f, 2.0f, 3.0f) });
		const Entity laterPrimary = world.CreateEntity();
		world.Add<CameraComponent>(laterPrimary, CameraComponent{});

		const std::optional<WorldCamera> found = FindPrimaryCamera(world);
		Require(found && found->Owner == primary, "The first camera marked primary should be found");
		Require(found->Camera.FovYDegrees == 45.0f && found->Transform.Position == glm::vec3(1.0f, 2.0f, 3.0f),
			"The camera should come with its values and its entity's transform");

		world.Get<CameraComponent>(primary).bPrimary = false;
		const std::optional<WorldCamera> withoutTransform = FindPrimaryCamera(world);
		Require(withoutTransform && withoutTransform->Owner == laterPrimary &&
				withoutTransform->Transform.Position == glm::vec3(0.0f),
			"A primary camera without a transform should be at the origin");
	}

	// A floor whose top is at y = 0 and a 1 m box standing on it at the origin, both static; a
	// camera off to the side and above, looking at the box's front face
	struct PickScene
	{
		GameWorld World;
		Entity Floor;
		Entity Box;
		TransformComponent CameraTransform;
		CameraComponent Camera;
		glm::vec2 Viewport{ 1280.0f, 720.0f };

		PickScene()
		{
			Registry& registry = World.GetRegistry();
			Floor = registry.CreateEntity();
			registry.Add<TransformComponent>(Floor, TransformComponent{ .Position = glm::vec3(0.0f, -0.5f, 0.0f) });
			registry.Add<BoxColliderComponent>(Floor, BoxColliderComponent{ .HalfExtents = glm::vec3(20.0f, 0.5f, 20.0f) });

			Box = registry.CreateEntity();
			registry.Add<GuidComponent>(Box, GuidComponent{ EntityGuid::Generate() });
			registry.Add<TransformComponent>(Box, TransformComponent{ .Position = glm::vec3(0.0f, 0.5f, 0.0f) });
			registry.Add<BoxColliderComponent>(Box, BoxColliderComponent{ .HalfExtents = glm::vec3(0.5f) });

			CameraTransform.Position = glm::vec3(1.0f, 2.0f, 6.0f);
			CameraTransform.Rotation =
				glm::quatLookAt(glm::normalize(glm::vec3(0.0f, 0.5f, 0.0f) - CameraTransform.Position), glm::vec3(0.0f, 1.0f, 0.0f));

			// The bodies are made before the first step
			World.Step(StepTime{ 1.0 / 60.0, 0, 0.0 });
		}

		// The ray through the pixel the point is drawn on
		Ray RayThrough(const glm::vec3& point) const
		{
			return ScreenPointToRay(CameraTransform, Camera, Viewport, ProjectToPixel(CameraTransform, Camera, Viewport, point));
		}
	};

	// On the box's front face, which faces +Z
	const glm::vec3 PointOnTheBox(0.2f, 0.7f, 0.5f);

	void TestAPickFindsTheBoxDrawnThere()
	{
		const PickScene scene;
		const Ray ray = scene.RayThrough(PointOnTheBox);
		const std::optional<PhysicsPick> pick = PickPhysicsBody(scene.World, ray, scene.Camera.FarPlane);

		Require(pick.has_value(), "The ray through the box should hit something");
		Require(pick->HitEntity == scene.Box, "The ray should hit the box's entity");
		Require(Near(pick->Point, PointOnTheBox), "The ray should hit the box where it is drawn " + ToString(pick->Point));
		Require(Near(pick->Normal, glm::vec3(0.0f, 0.0f, 1.0f)), "The normal should be the front face's " + ToString(pick->Normal));
		Require(std::abs(pick->Distance - glm::length(PointOnTheBox - ray.Origin)) < Tolerance,
			"The distance should be from the ray's origin to the point");
	}

	void TestIgnoringTheBoxFindsTheFloorBehindIt()
	{
		const PickScene scene;
		const Ray ray = scene.RayThrough(PointOnTheBox);
		const QueryFilter filter{ .Ignore = scene.World.GetBodies().FindBody(scene.Box) };
		const std::optional<PhysicsPick> pick = PickPhysicsBody(scene.World, ray, scene.Camera.FarPlane, filter);

		Require(pick.has_value() && pick->HitEntity == scene.Floor, "Ignoring the box, the ray should hit the floor behind it");
		Require(std::abs(pick->Point.y) < Tolerance && pick->Point.z < -0.5f, "The floor should be hit behind the box " + ToString(pick->Point));
		Require(Near(pick->Normal, glm::vec3(0.0f, 1.0f, 0.0f)), "The floor's normal should point up");
	}

	void TestSensorsCountOnlyWhenAsked()
	{
		PickScene scene;
		// Halfway between the camera and the box, made outside PhysicsBodies, so no entity's
		BodyDesc sensor;
		sensor.Shape.HalfExtents = glm::vec3(0.3f);
		sensor.Position = (scene.CameraTransform.Position + PointOnTheBox) * 0.5f;
		sensor.Motion = EBodyMotion::Static;
		sensor.bSensor = true;
		const BodyHandle sensorBody = scene.World.GetPhysics().CreateBody(sensor);
		Require(sensorBody.IsValid(), "The sensor should be made");

		const Ray ray = scene.RayThrough(PointOnTheBox);
		const std::optional<PhysicsPick> past = PickPhysicsBody(scene.World, ray, scene.Camera.FarPlane);
		Require(past && past->HitEntity == scene.Box, "A pick should pass sensors by default");

		const std::optional<PhysicsPick> withSensors =
			PickPhysicsBody(scene.World, ray, scene.Camera.FarPlane, QueryFilter{ .bSensors = true });
		Require(withSensors.has_value() && withSensors->Distance < past->Distance,
			"A pick asked for sensors should find the sensor before the box");
		Require(!withSensors->HitEntity.IsValid(), "A body no entity owns should give a null entity");
	}

	void TestMissingEverythingFindsNothing()
	{
		const PickScene scene;
		// The middle of the top edge looks over the horizon
		const Ray ray = ScreenPointToRay(scene.CameraTransform, scene.Camera, scene.Viewport, glm::vec2(scene.Viewport.x * 0.5f, 0.0f));
		Require(ray.Direction.y > 0.0f, "The top edge should look up into the sky");
		Require(!PickPhysicsBody(scene.World, ray, scene.Camera.FarPlane), "A ray past everything should find nothing");

		// Nor what lies beyond the distance given
		Require(!PickPhysicsBody(scene.World, scene.RayThrough(PointOnTheBox), 2.0f), "A pick should end at its distance");
	}

	void TestAnAnchorWhereThePickHitFollowsTheBox()
	{
		PickScene scene;
		Registry& registry = scene.World.GetRegistry();
		const std::optional<PhysicsPick> pick = PickPhysicsBody(scene.World, scene.RayThrough(PointOnTheBox), scene.Camera.FarPlane);
		Require(pick.has_value(), "The ray through the box should hit it");

		const SurfaceAnchor anchor = MakeSurfaceAnchor(registry, *pick);
		Require(anchor.Host == registry.Get<GuidComponent>(scene.Box).Guid, "The anchor should be on the box");

		const std::optional<SurfaceFrame> here = ResolveSurfaceAnchor(registry, anchor);
		Require(here && Near(here->Position, pick->Point) && Near(here->Normal, pick->Normal),
			"The anchor should resolve to where the pick hit");
		Require(Near(here->Tangent, glm::vec3(1.0f, 0.0f, 0.0f)), "On a face looking along Z, the tangent should be world X");

		// The box moved and turned: the anchor goes with it
		TransformComponent& box = registry.Get<TransformComponent>(scene.Box);
		const glm::vec3 local = PointOnTheBox - box.Position;
		box.Position = glm::vec3(-3.0f, 1.0f, 2.0f);
		box.Rotation = glm::angleAxis(glm::radians(70.0f), glm::normalize(glm::vec3(1.0f, 2.0f, 0.5f)));

		const std::optional<SurfaceFrame> moved = ResolveSurfaceAnchor(registry, anchor);
		Require(moved.has_value(), "The anchor should still resolve");
		Require(Near(moved->Position, box.Position + box.Rotation * local), "The anchor's point should move with the box " + ToString(moved->Position));
		Require(Near(moved->Normal, box.Rotation * glm::vec3(0.0f, 0.0f, 1.0f)), "The anchor's normal should turn with the box");
		Require(Near(moved->Tangent, box.Rotation * glm::vec3(1.0f, 0.0f, 0.0f)), "The anchor's tangent should turn with the box");
	}
}

int main()
{
	Nyx::Logger::Get().Init();

	try
	{
		TestProjectedPointsLieOnTheirPixelsRays();
		TestWorldPointsShowOnTheirPixels();
		TestTheCentreAndCorners();
		TestJustPastTheNearPlane();
		TestThePrimaryCameraIsFoundAsTheRendererFindsIt();
		TestAPickFindsTheBoxDrawnThere();
		TestIgnoringTheBoxFindsTheFloorBehindIt();
		TestSensorsCountOnlyWhenAsked();
		TestMissingEverythingFindsNothing();
		TestAnAnchorWhereThePickHitFollowsTheBox();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Cursor ray test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All cursor ray tests passed.\n";
	return 0;
}
