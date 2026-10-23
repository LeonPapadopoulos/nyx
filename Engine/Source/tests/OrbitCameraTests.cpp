// The orbit camera (UpdateOrbitCameras()) on a world without a window: it starts from where the
// camera looks, keeps its distance and offset from a moving target, follows the target where it is
// drawn between steps, smooths its following at any frame rate, clamps its pitch and its zoom, wraps
// its yaw, leaves the camera alone without a target, and takes a live edit of its distance. The
// ground helpers turn a stick's input into a move relative to it. The component is saved with the
// scene, without its runtime values.
#include "ComponentRegistration.h"
#include "GuidComponent.h"
#include "Log.h"
#include "OrbitCameraComponent.h"
#include "OrbitCameras.h"
#include "PreviousTransformComponent.h"
#include "SceneSerializer.h"
#include "TestSupport.h"
#include "TransformComponent.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
	using namespace Nyx;
	using Nyx::Tests::Require;

	constexpr float FrameSeconds = 1.0f / 144.0f;

	bool Near(const glm::vec3& a, const glm::vec3& b, float tolerance = 1e-4f)
	{
		return glm::length(a - b) < tolerance;
	}

	std::string Describe(const glm::vec3& v)
	{
		return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
	}

	// A target with a guid, and a camera behind it looking along -Z, its pitch `degrees`
	struct Rig
	{
		Registry World;
		Entity Target;
		Entity Camera;

		explicit Rig(float degrees = -20.0f, const OrbitCameraComponent& orbit = OrbitCameraComponent{ .FollowSmoothing = 0.0f })
		{
			Target = World.CreateEntity();
			World.Add<GuidComponent>(Target, GuidComponent{ EntityGuid::Generate() });
			World.Add<TransformComponent>(Target, TransformComponent{});
			Camera = World.CreateEntity();
			World.Add<TransformComponent>(Camera,
				TransformComponent{ .Position = glm::vec3(0.0f, 3.0f, 5.0f), .Rotation = glm::angleAxis(glm::radians(degrees), glm::vec3(1.0f, 0.0f, 0.0f)) });
			World.Add<OrbitCameraComponent>(Camera, orbit).Target = World.Get<GuidComponent>(Target).Guid;
		}

		OrbitCameraComponent& Orbit()
		{
			return World.Get<OrbitCameraComponent>(Camera);
		}

		const TransformComponent& CameraTransform()
		{
			return World.Get<TransformComponent>(Camera);
		}

		glm::vec3 LooksAlong()
		{
			return CameraTransform().Rotation * glm::vec3(0.0f, 0.0f, -1.0f);
		}

		// The point the camera looks at: where it is plus its distance along where it looks
		glm::vec3 LooksAt()
		{
			return CameraTransform().Position + LooksAlong() * Orbit().CurrentDistance;
		}

		void Frame(std::optional<float> interpolation = std::nullopt)
		{
			UpdateOrbitCameras(World, FrameSeconds, interpolation);
		}
	};

	void TestItStartsFromWhereTheCameraLooks()
	{
		Rig rig(-20.0f);
		rig.Frame();
		const OrbitCameraComponent& orbit = rig.Orbit();
		Require(std::abs(orbit.Yaw) < 1e-5f && std::abs(orbit.Pitch - glm::radians(-20.0f)) < 1e-5f, "It should start with the camera's yaw and pitch");
		Require(orbit.CurrentDistance == orbit.Distance, "It should start at its distance");
		Require(Near(rig.LooksAt(), orbit.TargetOffset), "It should look at the target's offset point " + Describe(rig.LooksAt()));
		Require(rig.CameraTransform().Position.z > 0.0f && rig.CameraTransform().Position.y > orbit.TargetOffset.y,
			"Looking down along -Z, it should be behind the target and above it");
	}

	void TestItKeepsItsDistanceAndOffsetFromAMovingTarget()
	{
		Rig rig;
		rig.Frame();
		const glm::vec3 offsetFromTarget = rig.CameraTransform().Position;
		for (int frame = 1; frame <= 100; ++frame)
		{
			const glm::vec3 target(0.05f * frame, 0.01f * frame, -0.1f * frame);
			rig.World.Get<TransformComponent>(rig.Target).Position = target;
			rig.Frame();
			Require(Near(rig.CameraTransform().Position - target, offsetFromTarget), "It should move with its target");
			Require(Near(rig.LooksAt(), target + rig.Orbit().TargetOffset), "It should keep looking at the target's offset point");
			Require(std::abs(glm::length(rig.CameraTransform().Position - (target + rig.Orbit().TargetOffset)) - rig.Orbit().Distance) < 1e-4f,
				"It should keep its distance");
		}
	}

	void TestItFollowsTheTargetWhereItIsDrawn()
	{
		Rig rig;
		// The target moved from x = 0 to 1 in its last step
		TransformComponent& target = rig.World.Get<TransformComponent>(rig.Target);
		target.Position = glm::vec3(1.0f, 0.0f, 0.0f);
		PreviousTransformComponent& previous = rig.World.Add<PreviousTransformComponent>(rig.Target);
		SetPreviousTransform(previous, glm::vec3(0.0f), target.Rotation, target);

		rig.Frame(0.25f);
		Require(Near(rig.LooksAt(), glm::vec3(0.25f, 0.0f, 0.0f) + rig.Orbit().TargetOffset), "A quarter into the step, it should look where the target is drawn");
		rig.Frame(0.75f);
		Require(Near(rig.LooksAt(), glm::vec3(0.75f, 0.0f, 0.0f) + rig.Orbit().TargetOffset), "It should move on with the target as it is drawn");
		rig.Frame(std::nullopt);
		Require(Near(rig.LooksAt(), glm::vec3(1.0f, 0.0f, 0.0f) + rig.Orbit().TargetOffset), "Without drawing between steps, where the target is");
	}

	void TestFollowingIsSmoothedAtAnyFrameRate()
	{
		// The target jumps 1 m; after 0.1 s, with a smoothing of 0.1 s, the camera has come 1 - 1/e of
		// the way, whether at 30 or at 240 frames a second
		for (const int framesPerSecond : { 30, 240 })
		{
			Rig rig(-20.0f, OrbitCameraComponent{ .FollowSmoothing = 0.1f });
			UpdateOrbitCameras(rig.World, 1.0f / framesPerSecond, std::nullopt);
			rig.World.Get<TransformComponent>(rig.Target).Position = glm::vec3(1.0f, 0.0f, 0.0f);
			for (int frame = 0; frame < framesPerSecond / 10; ++frame)
			{
				UpdateOrbitCameras(rig.World, 1.0f / framesPerSecond, std::nullopt);
			}
			const float come = rig.Orbit().FollowPoint.x;
			Require(std::abs(come - (1.0f - std::exp(-1.0f))) < 1e-3f,
				"At " + std::to_string(framesPerSecond) + " frames a second it should have come 63% of the way, not " + std::to_string(come));
		}
	}

	void TestPitchIsClamped()
	{
		Rig rig;
		rig.Frame();
		rig.Orbit().LookDelta = glm::vec2(0.0f, -10.0f);
		rig.Frame();
		Require(std::abs(rig.Orbit().Pitch - glm::radians(rig.Orbit().MinPitchDegrees)) < 1e-5f, "Looking far down should stop at the min pitch");
		Require(rig.Orbit().LookDelta == glm::vec2(0.0f), "The look input should be used up");
		Require(Near(rig.LooksAt(), rig.Orbit().TargetOffset), "Over the target, it should still look at it");

		rig.Orbit().LookDelta = glm::vec2(0.0f, 10.0f);
		rig.Frame();
		Require(std::abs(rig.Orbit().Pitch - glm::radians(rig.Orbit().MaxPitchDegrees)) < 1e-5f, "Looking far up should stop at the max pitch");
		Require(rig.CameraTransform().Position.y < rig.Orbit().TargetOffset.y, "Looking up, it should be below the point it looks at");

		// The sensitivity scales the turn
		rig.Orbit().LookSensitivity = 0.5f;
		rig.Orbit().LookDelta = glm::vec2(0.0f, -0.2f);
		rig.Frame();
		Require(std::abs(rig.Orbit().Pitch - (glm::radians(rig.Orbit().MaxPitchDegrees) - 0.1f)) < 1e-5f, "Half the sensitivity should turn half as far");
	}

	void TestZoomIsClamped()
	{
		Rig rig;
		rig.Frame();
		rig.Orbit().ZoomDelta = 2.0f;
		rig.Frame();
		Require(std::abs(rig.Orbit().CurrentDistance - (rig.Orbit().Distance - 2.0f * rig.Orbit().ZoomStep)) < 1e-5f, "Two steps in should come two zoom steps closer");
		Require(rig.Orbit().ZoomDelta == 0.0f, "The zoom input should be used up");

		rig.Orbit().ZoomDelta = 100.0f;
		rig.Frame();
		Require(rig.Orbit().CurrentDistance == rig.Orbit().MinDistance, "Zooming far in should stop at the min distance");
		rig.Orbit().ZoomDelta = -100.0f;
		rig.Frame();
		Require(rig.Orbit().CurrentDistance == rig.Orbit().MaxDistance, "Zooming far out should stop at the max distance");
		Require(std::abs(glm::length(rig.CameraTransform().Position - rig.Orbit().TargetOffset) - rig.Orbit().MaxDistance) < 1e-4f,
			"The camera should be that far away");

		// A live edit of the distance counts
		rig.Orbit().Distance = 3.0f;
		rig.Frame();
		Require(rig.Orbit().CurrentDistance == 3.0f, "A changed distance should put it there");
	}

	void TestYawWraps()
	{
		Rig rig;
		rig.Frame();
		for (int frame = 0; frame < 50; ++frame)
		{
			rig.Orbit().LookDelta = glm::vec2(0.5f, 0.0f);
			rig.Frame();
			Require(rig.Orbit().Yaw >= -glm::pi<float>() && rig.Orbit().Yaw <= glm::pi<float>(), "The yaw should stay within half a turn either way");
		}
		// 25 radians to the right: 25 - 8 pi turned left, about -0.133 radians
		const float expected = std::remainder(-25.0f, glm::two_pi<float>());
		Require(std::abs(rig.Orbit().Yaw - expected) < 1e-4f, "The yaw should wrap, not lose the turns' remainder");
		Require(Near(rig.LooksAlong(), glm::angleAxis(expected, glm::vec3(0.0f, 1.0f, 0.0f)) * glm::angleAxis(rig.Orbit().Pitch, glm::vec3(1.0f, 0.0f, 0.0f)) * glm::vec3(0.0f, 0.0f, -1.0f)),
			"The camera should look the way its yaw and pitch say");

		// A turn to the right looks to the right
		Rig right;
		right.Frame();
		right.Orbit().LookDelta = glm::vec2(glm::half_pi<float>(), 0.0f);
		right.Frame();
		Require(right.LooksAlong().x > 0.9f, "A quarter turn to the right should look along +X " + Describe(right.LooksAlong()));
	}

	void TestWithoutATargetItIsLeftAlone()
	{
		Rig rig;
		rig.Orbit().Target = EntityGuid{};
		const TransformComponent before = rig.CameraTransform();
		rig.Frame();
		Require(rig.CameraTransform().Position == before.Position && rig.CameraTransform().Rotation == before.Rotation,
			"Without a target, the camera should stay where it is");

		rig.Orbit().Target = rig.World.Get<GuidComponent>(rig.Target).Guid;
		rig.World.DestroyEntity(rig.Target);
		rig.Frame();
		Require(rig.CameraTransform().Position == before.Position, "With its target gone, the camera should stay where it is");
	}

	void TestGroundMoves()
	{
		Require(Near(GetGroundForward(0.0f), glm::vec3(0.0f, 0.0f, -1.0f)) && Near(GetGroundRight(0.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
			"Unturned, forward is -Z and right +X");
		Require(Near(GetGroundForward(glm::half_pi<float>()), glm::vec3(-1.0f, 0.0f, 0.0f)), "Turned a quarter to the left, forward is -X");

		// Whatever the pitch, forward on the ground is where the camera looks, flattened
		Rig rig(-60.0f);
		rig.Frame();
		rig.Orbit().LookDelta = glm::vec2(0.7f, 0.0f);
		rig.Frame();
		const glm::vec3 flat = glm::normalize(glm::vec3(rig.LooksAlong().x, 0.0f, rig.LooksAlong().z));
		Require(Near(GetGroundForward(rig.Orbit().Yaw), flat), "Forward on the ground should be where the camera looks, flattened");
		Require(Near(GetGroundMove(rig.Orbit().Yaw, glm::vec2(0.0f, 1.0f)), flat), "The stick pushed up should walk where the camera looks");

		const glm::vec3 diagonal = GetGroundMove(0.0f, glm::vec2(1.0f, 1.0f));
		Require(std::abs(glm::length(diagonal) - 1.0f) < 1e-5f && Near(diagonal, glm::normalize(glm::vec3(1.0f, 0.0f, -1.0f))),
			"W and D together should walk diagonally, no faster than straight");
		Require(Near(GetGroundMove(0.0f, glm::vec2(0.5f, 0.0f)), glm::vec3(0.5f, 0.0f, 0.0f)), "A half-pushed stick should walk at half speed");
	}

	void TestScenesKeepOrbitCameras(const std::filesystem::path& directory)
	{
		Nyx::RegisterComponentTypes();
		Rig rig(-20.0f, OrbitCameraComponent{ .Distance = 6.0f, .MaxPitchDegrees = 10.0f });
		rig.Frame();
		const EntityGuid target = rig.Orbit().Target;

		const std::filesystem::path path = directory / "Orbit.nyxscene";
		Require(SceneSerializer::SaveToFile(rig.World, path), "Saving a scene with an orbit camera failed");
		Registry loaded;
		Require(SceneSerializer::LoadFromFile(path, loaded), "Loading a scene with an orbit camera failed");

		int cameras = 0;
		loaded.Each<OrbitCameraComponent>([&](Entity, OrbitCameraComponent& orbit)
			{
				++cameras;
				Require(orbit.Target == target && orbit.Distance == 6.0f && orbit.MaxPitchDegrees == 10.0f, "The orbit camera's values should be kept");
				Require(!orbit.bStarted && orbit.Yaw == 0.0f, "Where it was while the game ran shouldn't be saved");
			});
		Require(cameras == 1, "The orbit camera should be saved and loaded");
	}
}

int main()
{
	Nyx::Logger::Get().Init();

	try
	{
		Nyx::Tests::TestDirectory directory("NyxOrbitCameraTests");
		TestItStartsFromWhereTheCameraLooks();
		TestItKeepsItsDistanceAndOffsetFromAMovingTarget();
		TestItFollowsTheTargetWhereItIsDrawn();
		TestFollowingIsSmoothedAtAnyFrameRate();
		TestPitchIsClamped();
		TestZoomIsClamped();
		TestYawWraps();
		TestWithoutATargetItIsLeftAlone();
		TestGroundMoves();
		TestScenesKeepOrbitCameras(directory.Path);
	}
	catch (const std::exception& error)
	{
		std::cerr << "Orbit camera test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All orbit camera tests passed.\n";
	return 0;
}
