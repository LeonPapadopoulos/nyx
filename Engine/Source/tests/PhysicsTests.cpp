// Physics (PhysicsWorld): bodies falling and coming to rest, mass, forces at a point, teleporting,
// kinematic moves, the same results from the same inputs, and bodies made and destroyed.
#include "Log.h"
#include "PhysicsWorld.h"
#include "TestSupport.h"

#include <glm/glm.hpp>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using namespace Nyx;
	using Nyx::Tests::Require;

	constexpr float Step = 1.0f / 60.0f;

	bool Near(float a, float b, float tolerance)
	{
		return std::abs(a - b) <= tolerance;
	}

	// A static floor whose top is at y = 0
	BodyHandle AddFloor(PhysicsWorld& world)
	{
		BodyDesc floor;
		floor.Shape.HalfExtents = glm::vec3(20.0f, 0.5f, 20.0f);
		floor.Position = glm::vec3(0.0f, -0.5f, 0.0f);
		floor.Motion = EBodyMotion::Static;
		return world.CreateBody(floor);
	}

	BodyHandle AddBox(PhysicsWorld& world, const glm::vec3& position, float mass = 0.0f)
	{
		BodyDesc box;
		box.Position = position;
		box.Mass = mass;
		return world.CreateBody(box);
	}

	void RunSteps(PhysicsWorld& world, int steps)
	{
		for (int step = 0; step < steps; ++step)
		{
			world.Step(Step);
		}
	}

	void TestABoxFallsAndRests()
	{
		PhysicsWorld world;
		AddFloor(world);
		const BodyHandle box = AddBox(world, glm::vec3(0.0f, 5.0f, 0.0f));
		Require(box.IsValid(), "The box should be made");

		RunSteps(world, 30);
		Require(world.GetPose(box).Position.y < 5.0f, "The box should fall");
		Require(world.GetLinearVelocity(box).y < 0.0f, "The falling box should move down");

		RunSteps(world, 300);
		const BodyPose pose = world.GetPose(box);
		// Jolt lets resting bodies sink up to its penetration slop, 2 cm, before pushing them out
		Require(pose.Position.y > 0.475f && pose.Position.y < 0.505f,
			"The box should rest on the floor, its centre half a metre up (" + std::to_string(pose.Position.y) + ")");
		Require(Near(pose.Position.x, 0.0f, 0.01f) && Near(pose.Position.z, 0.0f, 0.01f), "The box should land straight down");
		Require(!world.IsAwake(box), "A box at rest should fall asleep");
	}

	void TestMass()
	{
		PhysicsWorld world;
		const BodyHandle water = AddBox(world, glm::vec3(0.0f));
		Require(Near(world.GetMass(water), 1000.0f, 1.0f), "A cubic metre box should weigh as much as water by default");

		const BodyHandle light = AddBox(world, glm::vec3(3.0f, 0.0f, 0.0f), 50.0f);
		Require(Near(world.GetMass(light), 50.0f, 0.01f), "A given mass should be the body's mass");

		const BodyHandle floor = AddFloor(world);
		Require(world.GetMass(floor) == 0.0f, "A static body has no mass to speak of");
	}

	void TestForceAtAPointTurns()
	{
		PhysicsWorld world(PhysicsSettings{ .Gravity = glm::vec3(0.0f) });
		const BodyHandle centred = AddBox(world, glm::vec3(-5.0f, 0.0f, 0.0f), 1.0f);
		const BodyHandle offset = AddBox(world, glm::vec3(5.0f, 0.0f, 0.0f), 1.0f);

		world.AddForce(centred, glm::vec3(10.0f, 0.0f, 0.0f));
		// Pushed along +X at its top: it turns about -Z
		world.AddForceAtPoint(offset, glm::vec3(10.0f, 0.0f, 0.0f), glm::vec3(5.0f, 0.5f, 0.0f));
		world.Step(Step);

		Require(world.GetLinearVelocity(centred).x > 0.0f, "A force should move the body");
		Require(glm::length(world.GetAngularVelocity(centred)) < 1e-4f, "A force at the centre of mass shouldn't turn the body");
		Require(world.GetAngularVelocity(offset).z < -1e-3f, "A force off the centre of mass should turn the body");

		world.AddImpulse(centred, glm::vec3(0.0f, 2.0f, 0.0f));
		Require(Near(world.GetLinearVelocity(centred).y, 2.0f, 1e-3f), "An impulse of 2 N s should give a 1 kg body 2 m/s at once");
	}

	void TestTeleportAndKinematicMoves()
	{
		PhysicsWorld world;
		const BodyHandle box = AddBox(world, glm::vec3(0.0f, 5.0f, 0.0f));
		RunSteps(world, 20);

		world.Teleport(box, BodyPose{ glm::vec3(3.0f, 10.0f, -2.0f) });
		Require(glm::length(world.GetPose(box).Position - glm::vec3(3.0f, 10.0f, -2.0f)) < 1e-4f, "Teleport should put the body there");
		Require(glm::length(world.GetLinearVelocity(box)) < 1e-6f, "Teleport should stop the body");

		BodyDesc platformDesc;
		platformDesc.Motion = EBodyMotion::Kinematic;
		const BodyHandle platform = world.CreateBody(platformDesc);
		world.MoveKinematic(platform, BodyPose{ glm::vec3(0.0f, 1.0f, 0.0f) }, Step);
		world.Step(Step);
		Require(Near(world.GetPose(platform).Position.y, 1.0f, 1e-4f), "A kinematic body should reach where it was moved in one step");
	}

	// Twenty boxes falling onto each other, then the poses of all of them
	std::vector<BodyPose> SimulatePile()
	{
		PhysicsWorld world;
		AddFloor(world);
		std::vector<BodyHandle> boxes;
		for (int i = 0; i < 20; ++i)
		{
			boxes.push_back(AddBox(world, glm::vec3(0.3f * float(i % 3), 1.0f + 1.1f * float(i), 0.2f * float(i % 2))));
		}
		RunSteps(world, 240);

		std::vector<BodyPose> poses;
		for (const BodyHandle box : boxes)
		{
			poses.push_back(world.GetPose(box));
		}
		return poses;
	}

	void TestTheSameInputsGiveTheSameResults()
	{
		const std::vector<BodyPose> first = SimulatePile();
		const std::vector<BodyPose> second = SimulatePile();
		for (size_t i = 0; i < first.size(); ++i)
		{
			Require(first[i].Position == second[i].Position && first[i].Rotation == second[i].Rotation,
				"The same bodies stepped the same way should end in exactly the same place (box " + std::to_string(i) + ")");
		}
	}

	void TestBodiesComeAndGo()
	{
		PhysicsWorld world;
		BodyDesc desc;
		desc.UserData = 0x1234567890ull;
		desc.Shape = BodyShape{ .Kind = EShapeKind::Sphere, .Radius = 0.3f };
		const BodyHandle sphere = world.CreateBody(desc);
		desc.Shape = BodyShape{ .Kind = EShapeKind::Capsule, .Radius = 0.3f, .HalfHeight = 0.6f };
		const BodyHandle capsule = world.CreateBody(desc);

		Require(sphere.IsValid() && capsule.IsValid(), "Spheres and capsules should be made");
		Require(world.GetBodyCount() == 2, "The world should count its two bodies");
		Require(world.GetUserData(sphere) == 0x1234567890ull, "A body should keep its owner's number");

		world.DestroyBody(sphere);
		world.DestroyBody(BodyHandle{});
		Require(world.GetBodyCount() == 1, "A destroyed body should be gone; an invalid handle does nothing");
	}
}

int main()
{
	Nyx::Logger::Get().Init();

	try
	{
		TestABoxFallsAndRests();
		TestMass();
		TestForceAtAPointTurns();
		TestTeleportAndKinematicMoves();
		TestTheSameInputsGiveTheSameResults();
		TestBodiesComeAndGo();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Physics test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All physics tests passed.\n";
	return 0;
}
