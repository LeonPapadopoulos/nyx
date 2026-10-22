// Physics (PhysicsWorld): bodies falling and coming to rest, mass, forces at a point, teleporting,
// kinematic moves, the same results from the same inputs, bodies made and destroyed, queries,
// sensors, contact events and constraints. The debug view: the lines of each shape, colours by
// motion, contacts after a landing, constraints, and nothing when switched off.
#include "DebugDraw.h"
#include "Log.h"
#include "PhysicsWorld.h"
#include "TestSupport.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <iostream>
#include <iterator>
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

	BodyHandle AddStaticBox(PhysicsWorld& world, const glm::vec3& position, const glm::vec3& halfExtents, uint64_t userData = 0)
	{
		BodyDesc box;
		box.Shape.HalfExtents = halfExtents;
		box.Position = position;
		box.Motion = EBodyMotion::Static;
		box.UserData = userData;
		return world.CreateBody(box);
	}

	bool NearVec(const glm::vec3& a, const glm::vec3& b, float tolerance)
	{
		return glm::length(a - b) <= tolerance;
	}

	void TestRayCasts()
	{
		PhysicsWorld world;
		// Two walls across +X, the nearer one's face at x = 4.5
		const BodyHandle nearWall = AddStaticBox(world, glm::vec3(5.0f, 0.0f, 0.0f), glm::vec3(0.5f, 2.0f, 2.0f), 7);
		AddStaticBox(world, glm::vec3(10.0f, 0.0f, 0.0f), glm::vec3(0.5f, 2.0f, 2.0f));

		const std::optional<QueryHit> hit = world.RayCast(glm::vec3(0.0f), glm::vec3(2.0f, 0.0f, 0.0f), 100.0f);
		Require(hit.has_value() && hit->Body == nearWall, "A ray should hit the nearest box");
		Require(NearVec(hit->Point, glm::vec3(4.5f, 0.0f, 0.0f), 1e-3f), "The ray should hit the box's face");
		Require(NearVec(hit->Normal, glm::vec3(-1.0f, 0.0f, 0.0f), 1e-3f), "The normal should point back at the ray");
		Require(Near(hit->Distance, 4.5f, 1e-3f) && hit->UserData == 7, "The hit should say how far and whose body");

		Require(!world.RayCast(glm::vec3(0.0f), glm::vec3(-1.0f, 0.0f, 0.0f), 100.0f), "A ray aimed away should miss");
		Require(!world.RayCast(glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), 4.0f), "A ray too short should miss");

		const std::optional<QueryHit> past = world.RayCast(glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), 100.0f, QueryFilter{ .Ignore = nearWall });
		Require(past.has_value() && Near(past->Point.x, 9.5f, 1e-3f), "A ray should pass through the ignored body to the next");
		Require(!world.RayCast(glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), 100.0f, QueryFilter{ .bStatic = false }),
			"A ray for moving bodies only should pass static ones");
	}

	void TestShapeCastStopsAtAWall()
	{
		PhysicsWorld world;
		const BodyHandle wall = AddStaticBox(world, glm::vec3(5.0f, 0.0f, 0.0f), glm::vec3(0.5f, 2.0f, 2.0f));

		const std::optional<QueryHit> sphere =
			world.ShapeCast(BodyShape{ .Kind = EShapeKind::Sphere, .Radius = 0.5f }, BodyPose{}, glm::vec3(1.0f, 0.0f, 0.0f), 20.0f);
		Require(sphere.has_value() && sphere->Body == wall, "A sphere cast should hit the wall");
		// The sphere's centre stops half a metre before the face
		Require(Near(sphere->Distance, 4.0f, 0.02f), "The sphere should stop at the wall (" + std::to_string(sphere->Distance) + ")");
		Require(Near(sphere->Point.x, 4.5f, 0.02f), "The sphere should touch the wall's face");
		Require(NearVec(sphere->Normal, glm::vec3(-1.0f, 0.0f, 0.0f), 1e-2f), "The normal should face the sphere");

		const std::optional<QueryHit> box = world.ShapeCast(BodyShape{ .HalfExtents = glm::vec3(0.25f) }, BodyPose{ glm::vec3(0.0f, 1.0f, 0.0f) },
			glm::vec3(1.0f, 0.0f, 0.0f), 20.0f);
		Require(box.has_value() && Near(box->Distance, 4.25f, 0.02f), "A box cast should stop at the wall too");

		Require(!world.ShapeCast(BodyShape{}, BodyPose{}, glm::vec3(1.0f, 0.0f, 0.0f), 3.0f), "A cast too short should miss");
	}

	void TestOverlapFindsTheBodiesInside()
	{
		PhysicsWorld world(PhysicsSettings{ .Gravity = glm::vec3(0.0f) });
		const BodyHandle inside1 = AddBox(world, glm::vec3(0.0f));
		const BodyHandle inside2 = AddBox(world, glm::vec3(1.5f, 0.0f, 0.0f));
		AddBox(world, glm::vec3(10.0f, 0.0f, 0.0f));
		const BodyHandle wall = AddStaticBox(world, glm::vec3(0.0f, 0.0f, 2.0f), glm::vec3(1.0f));

		const BodyShape sphere{ .Kind = EShapeKind::Sphere, .Radius = 1.5f };
		const std::vector<BodyHandle> found = world.Overlap(sphere, BodyPose{});
		Require(found.size() == 3, "The sphere should overlap the two boxes and the wall, not the far box (" + std::to_string(found.size()) + ")");
		for (const BodyHandle body : { inside1, inside2, wall })
		{
			Require(std::find(found.begin(), found.end(), body) != found.end(), "Each body inside should be found");
		}

		const std::vector<BodyHandle> moving = world.Overlap(sphere, BodyPose{}, QueryFilter{ .bStatic = false, .Ignore = inside1 });
		Require(moving.size() == 1 && moving[0] == inside2, "The filter should leave out static and ignored bodies");
	}

	void TestSensorLetsABoxThroughAndTellsOfIt()
	{
		PhysicsWorld world;
		AddFloor(world);
		BodyDesc sensorDesc;
		sensorDesc.Shape.HalfExtents = glm::vec3(2.0f, 0.5f, 2.0f);
		sensorDesc.Position = glm::vec3(0.0f, 3.0f, 0.0f);
		sensorDesc.Motion = EBodyMotion::Static;
		sensorDesc.bSensor = true;
		const BodyHandle sensor = world.CreateBody(sensorDesc);
		const BodyHandle box = AddBox(world, glm::vec3(0.0f, 6.0f, 0.0f));

		int began = 0;
		int ended = 0;
		for (int step = 0; step < 300; ++step)
		{
			world.Step(Step);
			for (const ContactEvent& event : world.TakeContactEvents())
			{
				const bool bSensorPair = (event.BodyA == sensor && event.BodyB == box) || (event.BodyA == box && event.BodyB == sensor);
				if (bSensorPair)
				{
					began += event.Kind == EContactEvent::Began ? 1 : 0;
					ended += event.Kind == EContactEvent::Ended ? 1 : 0;
				}
			}
		}

		Require(world.GetPose(box).Position.y < 0.6f, "The box should fall through the sensor onto the floor");
		Require(began == 1 && ended == 1, "The sensor should tell once of the box entering and once of it leaving (" + std::to_string(began) + ", " + std::to_string(ended) + ")");

		Require(!world.RayCast(glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 7.5f), "Queries should pass sensors by default");
		const std::optional<QueryHit> hit = world.RayCast(glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 7.5f, QueryFilter{ .bSensors = true });
		Require(hit.has_value() && hit->Body == sensor, "Queries should find sensors when asked to");
	}

	// Five boxes dropped side by side onto a floor, and every contact event until they rest
	std::vector<ContactEvent> DropBoxes(std::vector<BodyHandle>& boxes, BodyHandle& floor, int& firstEventStep)
	{
		PhysicsWorld world;
		floor = AddFloor(world);
		boxes.clear();
		for (int i = 0; i < 5; ++i)
		{
			boxes.push_back(AddBox(world, glm::vec3(-6.0f + 3.0f * float(i), 1.5f, 0.0f)));
		}

		Require(world.TakeContactEvents().empty(), "There should be no contact events before a step");
		std::vector<ContactEvent> events;
		firstEventStep = -1;
		for (int step = 0; step < 400; ++step)
		{
			world.Step(Step);
			std::vector<ContactEvent> stepEvents = world.TakeContactEvents();
			if (firstEventStep < 0 && !stepEvents.empty())
			{
				firstEventStep = step;
			}
			events.insert(events.end(), stepEvents.begin(), stepEvents.end());
		}
		for (const BodyHandle box : boxes)
		{
			Require(!world.IsAwake(box), "The dropped boxes should have fallen asleep");
		}
		return events;
	}

	void TestABoxLandingBeginsOneContact()
	{
		std::vector<BodyHandle> boxes;
		BodyHandle floor;
		int firstEventStep = 0;
		const std::vector<ContactEvent> events = DropBoxes(boxes, floor, firstEventStep);

		Require(events.size() == boxes.size(), "Each box landing should begin one contact, and falling asleep end none (" + std::to_string(events.size()) + ")");
		for (size_t i = 0; i < events.size(); ++i)
		{
			const ContactEvent& event = events[i];
			Require(event.Kind == EContactEvent::Began, "A landing should begin a contact");
			// The floor was made first, so it has the lower handle; the boxes land in one step, in handle order
			Require(event.BodyA == floor && event.BodyB == boxes[i], "The events should be sorted by their bodies");
			Require(NearVec(event.Normal, glm::vec3(0.0f, 1.0f, 0.0f), 1e-3f), "The normal should point from the floor up to the box");
			Require(Near(event.Point.y, 0.0f, 0.05f) && Near(event.Point.x, -6.0f + 3.0f * float(i), 0.05f), "The contact should be under the box");
			// Dropped from a metre up, about 4.4 m/s
			Require(event.ApproachSpeed > 3.5f && event.ApproachSpeed < 5.0f, "The box should approach at its falling speed (" + std::to_string(event.ApproachSpeed) + ")");
		}

		std::vector<BodyHandle> boxesAgain;
		BodyHandle floorAgain;
		int firstEventStepAgain = 0;
		const std::vector<ContactEvent> again = DropBoxes(boxesAgain, floorAgain, firstEventStepAgain);
		Require(again.size() == events.size() && firstEventStepAgain == firstEventStep, "The same drop should give as many events, in the same step");
		for (size_t i = 0; i < events.size(); ++i)
		{
			const ContactEvent& a = events[i];
			const ContactEvent& b = again[i];
			const bool bSame = a.Kind == b.Kind && a.BodyA == b.BodyA && a.BodyB == b.BodyB && a.Point == b.Point && a.Normal == b.Normal;
			Require(bSame && a.ApproachSpeed == b.ApproachSpeed, "The same drop should give exactly the same events");
		}
	}

	// Jolt drops the contacts of sleeping bodies; PhysicsWorld keeps them open until the bodies part
	void TestSleepingContactsStayOpenUntilTheBodiesPart()
	{
		PhysicsWorld world;
		const BodyHandle floor = AddFloor(world);
		const BodyHandle box = AddBox(world, glm::vec3(0.0f, 0.5f, 0.0f));
		const BodyHandle other = AddBox(world, glm::vec3(5.0f, 0.5f, 0.0f));
		RunSteps(world, 200);
		Require(!world.IsAwake(box) && !world.IsAwake(other), "The boxes should fall asleep on the floor");
		Require(world.TakeContactEvents().size() == 2, "Each box should begin one contact with the floor");

		// Woken by a slide along the floor, the box still touches it
		world.AddImpulse(box, glm::vec3(300.0f, 0.0f, 0.0f));
		RunSteps(world, 30);
		Require(world.TakeContactEvents().empty(), "A sleeping box woken without leaving the floor should begin and end nothing");

		// Thrown up, it leaves the floor and lands again
		world.AddImpulse(box, glm::vec3(0.0f, 5000.0f, 0.0f));
		RunSteps(world, 200);
		const std::vector<ContactEvent> events = world.TakeContactEvents();
		Require(events.size() == 2 && events[0].Kind == EContactEvent::Ended && events[1].Kind == EContactEvent::Began,
			"A box thrown off the floor should end its contact, and begin one when it lands");
		Require(events[0].BodyA == floor && events[0].BodyB == box, "The ended contact should be the box's with the floor");

		world.DestroyBody(other);
		world.Step(Step);
		const std::vector<ContactEvent> gone = world.TakeContactEvents();
		Require(gone.size() == 1 && gone[0].Kind == EContactEvent::Ended && gone[0].BodyB == other,
			"Destroying a sleeping box should end its contact after the next step");
	}

	void TestDistanceConstraintKeepsItsRange()
	{
		PhysicsWorld world;
		const glm::vec3 anchor(0.0f, 10.0f, 0.0f);
		const BodyHandle box = AddBox(world, glm::vec3(0.0f, 8.0f, 0.0f), 1.0f);
		const ConstraintHandle rope = world.CreateConstraint(ConstraintDesc{ .Kind = EConstraintKind::Distance,
			.Body = box,
			.Point = glm::vec3(0.0f, 8.0f, 0.0f),
			.OtherPoint = anchor,
			.MinDistance = 1.5f,
			.MaxDistance = 2.5f });
		Require(rope.IsValid() && world.GetConstraintCount() == 1, "A distance constraint to the world should be made");

		// Swung sideways and up
		world.SetLinearVelocity(box, glm::vec3(4.0f, 3.0f, 0.0f));
		for (int step = 0; step < 300; ++step)
		{
			world.Step(Step);
			const float distance = glm::length(world.GetPose(box).Position - anchor);
			Require(distance > 1.45f && distance < 2.55f, "The box should stay between 1.5 m and 2.5 m from the anchor (" + std::to_string(distance) + ")");
		}

		world.DestroyConstraint(rope);
		world.DestroyConstraint(rope);
		Require(world.GetConstraintCount() == 0, "A destroyed constraint should be gone; destroying it again does nothing");
		RunSteps(world, 120);
		Require(glm::length(world.GetPose(box).Position - anchor) > 5.0f, "Without the rope the box should fall");
	}

	void TestHingeKeepsItsAxis()
	{
		PhysicsWorld world;
		const glm::vec3 pivot(0.0f, 5.0f, 0.0f);
		// Doors sticking out along +X, turning about Z, dropping under gravity
		const BodyHandle door = AddBox(world, glm::vec3(1.0f, 5.0f, 0.0f), 1.0f);
		const BodyHandle limited = AddBox(world, glm::vec3(1.0f, 5.0f, 10.0f), 1.0f);
		ConstraintDesc hinge{ .Kind = EConstraintKind::Hinge, .Body = door, .Point = pivot, .Axis = glm::vec3(0.0f, 0.0f, 1.0f) };
		Require(world.CreateConstraint(hinge).IsValid(), "A hinge should be made");
		hinge.Body = limited;
		hinge.Point = pivot + glm::vec3(0.0f, 0.0f, 10.0f);
		hinge.bLimits = true;
		hinge.LimitMin = -0.5f;
		hinge.LimitMax = 0.5f;
		Require(world.CreateConstraint(hinge).IsValid(), "A hinge with limits should be made");

		// A push across the hinge, which it should take
		world.AddImpulse(door, glm::vec3(0.0f, 0.0f, 3.0f));
		for (int step = 0; step < 240; ++step)
		{
			world.Step(Step);
			const BodyPose pose = world.GetPose(door);
			Require(Near(pose.Position.z, 0.0f, 0.02f), "The door should turn in its plane only");
			Require(Near(glm::length(pose.Position - pivot), 1.0f, 0.02f), "The door should stay a metre from its pivot");
			Require(NearVec(pose.Rotation * glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 0.0f, 1.0f), 0.02f), "The door's axis should stay the hinge's");

			const glm::vec3 arm = world.GetPose(limited).Rotation * glm::vec3(1.0f, 0.0f, 0.0f);
			Require(std::atan2(arm.y, arm.x) > -0.55f, "The limited door should stop at its limit (" + std::to_string(std::atan2(arm.y, arm.x)) + ")");
		}
		Require(world.GetPose(door).Position.y < 4.5f, "The free door should swing down");
	}

	void TestSliderKeepsItsBodyOnTheAxis()
	{
		PhysicsWorld world;
		const BodyHandle box = AddBox(world, glm::vec3(0.0f, 5.0f, 0.0f), 1.0f);
		const ConstraintHandle slider = world.CreateConstraint(ConstraintDesc{ .Kind = EConstraintKind::Slider,
			.Body = box,
			.Axis = glm::vec3(1.0f, 0.0f, 0.0f),
			.bLimits = true,
			.LimitMin = -1.0f,
			.LimitMax = 2.0f });
		Require(slider.IsValid(), "A slider should be made");

		// A push along, across and up, and a twist
		world.AddImpulse(box, glm::vec3(3.0f, 3.0f, 3.0f));
		world.AddForceAtPoint(box, glm::vec3(0.0f, 0.0f, 100.0f), glm::vec3(0.5f, 5.0f, 0.0f));
		for (int step = 0; step < 120; ++step)
		{
			world.Step(Step);
			const BodyPose pose = world.GetPose(box);
			Require(Near(pose.Position.y, 5.0f, 0.02f) && Near(pose.Position.z, 0.0f, 0.02f), "The box should stay on the slider's axis");
			Require(pose.Position.x < 2.05f && pose.Position.x > -1.05f, "The box should stay within the slider's limits");
			Require(std::abs(glm::dot(pose.Rotation, glm::quat(1.0f, 0.0f, 0.0f, 0.0f))) > 0.999f, "A slider shouldn't let the box turn");
		}
		Require(world.GetPose(box).Position.x > 1.9f, "The push along should carry the box to its limit");
	}

	void TestFixedConstraintHoldsTwoBodies()
	{
		PhysicsWorld world;
		const BodyHandle first = AddBox(world, glm::vec3(0.0f, 5.0f, 0.0f), 1.0f);
		const BodyHandle second = AddBox(world, glm::vec3(1.0f, 5.0f, 0.0f), 1.0f);
		const ConstraintHandle weld = world.CreateConstraint(ConstraintDesc{ .Kind = EConstraintKind::Fixed, .Body = second, .Other = first });
		Require(weld.IsValid(), "A fixed constraint should be made");
		Require(!world.CreateConstraint(ConstraintDesc{ .Body = first, .Other = first }).IsValid(), "A body can't be joined to itself");

		// Spun and thrown while falling
		world.AddForceAtPoint(first, glm::vec3(0.0f, 0.0f, 600.0f), glm::vec3(0.0f, 5.5f, 0.0f));
		world.AddImpulse(second, glm::vec3(0.0f, 4.0f, 2.0f));
		for (int step = 0; step < 120; ++step)
		{
			world.Step(Step);
			const BodyPose a = world.GetPose(first);
			const BodyPose b = world.GetPose(second);
			Require(NearVec(b.Position, a.Position + a.Rotation * glm::vec3(1.0f, 0.0f, 0.0f), 0.02f), "The second box should stay where it was to the first");
			Require(std::abs(glm::dot(a.Rotation, b.Rotation)) > 0.999f, "The boxes should turn together");
		}
		Require(glm::length(world.GetAngularVelocity(first)) > 0.1f, "The pair should be spinning");

		world.DestroyBody(first);
		Require(world.GetConstraintCount() == 0, "A destroyed body's constraints should go with it");
		RunSteps(world, 10);
	}

	// The lines of the drawing in a colour
	std::vector<DebugLine> LinesOf(const DebugDraw& drawing, const glm::vec3& color)
	{
		std::vector<DebugLine> lines;
		std::copy_if(drawing.GetLines().begin(), drawing.GetLines().end(), std::back_inserter(lines), [&](const DebugLine& line)
			{
				return line.Color == color;
			});
		return lines;
	}

	const PhysicsDebugOptions OnlyShapes{ .bShapes = true, .bContacts = false, .bConstraints = false };

	// Every line end of the drawing within the box from -extent to extent, and some on its faces
	bool Spans(const DebugDraw& drawing, const glm::vec3& extent)
	{
		glm::vec3 reach(0.0f);
		for (const DebugLine& line : drawing.GetLines())
		{
			reach = glm::max(reach, glm::max(glm::abs(line.From), glm::abs(line.To)));
		}
		return glm::all(glm::lessThan(glm::abs(reach - extent), glm::vec3(1e-4f)));
	}

	void TestDebugViewDrawsEachShape()
	{
		const auto draw = [](const BodyShape& shape, DebugDraw& drawing)
		{
			PhysicsWorld world;
			BodyDesc desc;
			desc.Shape = shape;
			desc.Motion = EBodyMotion::Static;
			world.CreateBody(desc);
			world.DrawDebug(drawing, OnlyShapes);
		};

		DebugDraw box;
		draw(BodyShape{ .Kind = EShapeKind::Box, .HalfExtents = glm::vec3(1.0f, 2.0f, 3.0f) }, box);
		Require(box.GetLines().size() == 12, "A box should be drawn as its 12 edges (" + std::to_string(box.GetLines().size()) + ")");
		Require(Spans(box, glm::vec3(1.0f, 2.0f, 3.0f)), "A box's edges should be its size");

		DebugDraw sphere;
		draw(BodyShape{ .Kind = EShapeKind::Sphere, .Radius = 0.5f }, sphere);
		Require(sphere.GetLines().size() == 72, "A sphere should be drawn as 3 circles of 24 lines (" + std::to_string(sphere.GetLines().size()) + ")");
		Require(Spans(sphere, glm::vec3(0.5f)), "A sphere's circles should be its size");

		DebugDraw capsule;
		draw(BodyShape{ .Kind = EShapeKind::Capsule, .Radius = 0.3f, .HalfHeight = 0.7f }, capsule);
		Require(capsule.GetLines().size() == 100,
			"A capsule should be drawn as 2 rings of 24 lines, 4 sides and 4 half circles of 12 (" + std::to_string(capsule.GetLines().size()) + ")");
		Require(Spans(capsule, glm::vec3(0.3f, 1.0f, 0.3f)), "A capsule's lines should reach its round ends, along its Y axis");
	}

	void TestDebugViewColoursByMotion()
	{
		const PhysicsDebugOptions options;
		PhysicsWorld world;
		AddFloor(world);
		const BodyHandle sleeper = AddBox(world, glm::vec3(0.0f, 0.5f, 0.0f));
		RunSteps(world, 300);
		Require(!world.IsAwake(sleeper), "The box on the floor should be asleep");

		BodyDesc kinematic;
		kinematic.Position = glm::vec3(5.0f, 1.0f, 0.0f);
		kinematic.Motion = EBodyMotion::Kinematic;
		world.CreateBody(kinematic);
		BodyDesc sensor;
		sensor.Position = glm::vec3(-5.0f, 1.0f, 0.0f);
		sensor.Motion = EBodyMotion::Static;
		sensor.bSensor = true;
		world.CreateBody(sensor);
		AddBox(world, glm::vec3(0.0f, 5.0f, 0.0f));

		DebugDraw drawing;
		world.DrawDebug(drawing, OnlyShapes);
		Require(drawing.GetLines().size() == 5 * 12, "Five boxes should be drawn");
		for (const glm::vec3& color : { options.StaticColor, options.KinematicColor, options.AwakeColor, options.AsleepColor, options.SensorColor })
		{
			Require(LinesOf(drawing, color).size() == 12, "Each motion should have its own colour, one box each");
		}
	}

	void TestDebugViewDrawsContactsAfterALanding()
	{
		const PhysicsDebugOptions onlyContacts{ .bShapes = false, .bContacts = true, .bConstraints = false };
		for (const bool bKeep : { true, false })
		{
			PhysicsWorld world;
			AddFloor(world);
			AddBox(world, glm::vec3(0.0f, 2.0f, 0.0f));
			world.SetKeepContactPoints(bKeep);

			RunSteps(world, 1);
			DebugDraw inTheAir;
			world.DrawDebug(inTheAir, onlyContacts);
			Require(inTheAir.GetLines().empty(), "A box in the air should have no contacts to draw");

			int steps = 0;
			while (world.TakeContactEvents().empty() && steps++ < 120)
			{
				world.Step(Step);
			}
			Require(steps < 120, "The box should land");

			DebugDraw landed;
			world.DrawDebug(landed, onlyContacts);
			if (!bKeep)
			{
				Require(landed.GetLines().empty(), "Contact points not kept shouldn't be drawn");
				continue;
			}

			// A cross and an arrow each: 3 lines, then the arrow's 5
			const std::vector<DebugLine>& lines = landed.GetLines();
			Require(!lines.empty() && lines.size() % 8 == 0, "Each contact point should be a cross and an arrow (" + std::to_string(lines.size()) + " lines)");
			Require(LinesOf(landed, PhysicsDebugOptions{}.ContactColor).size() == lines.size(), "Contacts should be in their colour");
			for (size_t point = 0; point < lines.size(); point += 8)
			{
				const DebugLine& normal = lines[point + 3];
				Require(std::abs(normal.From.y) < 0.1f, "A contact point should be on the floor");
				Require(Near(std::abs(normal.To.y - normal.From.y), 0.3f, 1e-3f), "A contact's arrow should point along the floor's normal");
			}
		}
	}

	void TestDebugViewDrawsConstraints()
	{
		PhysicsWorld world;
		const BodyHandle a = AddBox(world, glm::vec3(0.0f, 5.0f, 0.0f));
		const BodyHandle b = AddBox(world, glm::vec3(2.0f, 5.0f, 0.0f));
		world.CreateConstraint(ConstraintDesc{ .Kind = EConstraintKind::Point, .Body = a, .Other = b, .Point = glm::vec3(1.0f, 5.0f, 0.0f) });
		world.CreateConstraint(ConstraintDesc{ .Kind = EConstraintKind::Distance,
			.Body = a,
			.Point = glm::vec3(0.0f, 5.5f, 0.0f),
			.OtherPoint = glm::vec3(0.0f, 8.0f, 0.0f),
			.MinDistance = 0.0f,
			.MaxDistance = 2.5f });

		DebugDraw drawing;
		world.DrawDebug(drawing, PhysicsDebugOptions{ .bShapes = false, .bContacts = false, .bConstraints = true });
		const std::vector<DebugLine> lines = LinesOf(drawing, PhysicsDebugOptions{}.ConstraintColor);
		Require(drawing.GetLines().size() == 14 && lines.size() == 14, "Each constraint should be a line and a cross at each end");

		// In the order made: the point joint's anchors meet at its pivot; the rope runs from the world to the box
		Require(glm::length(lines[0].From - glm::vec3(1.0f, 5.0f, 0.0f)) < 1e-4f && glm::length(lines[0].To - lines[0].From) < 1e-4f,
			"A point joint's anchors should both be at its pivot");
		const bool bRope = glm::length(lines[7].From - glm::vec3(0.0f, 8.0f, 0.0f)) < 1e-4f && glm::length(lines[7].To - glm::vec3(0.0f, 5.5f, 0.0f)) < 1e-4f;
		Require(bRope, "A distance constraint should be drawn between its two ends");
	}

	void TestDebugViewDrawsNothingWhenOff()
	{
		PhysicsWorld world;
		AddFloor(world);
		const BodyHandle a = AddBox(world, glm::vec3(0.0f, 0.6f, 0.0f));
		const BodyHandle b = AddBox(world, glm::vec3(0.0f, 1.8f, 0.0f));
		world.CreateConstraint(ConstraintDesc{ .Kind = EConstraintKind::Fixed, .Body = a, .Other = b });
		world.SetKeepContactPoints(true);
		RunSteps(world, 10);

		DebugDraw drawing;
		world.DrawDebug(drawing, PhysicsDebugOptions{ .bShapes = false, .bContacts = false, .bConstraints = false });
		Require(drawing.GetLines().empty(), "Nothing should be drawn with every part switched off");
		world.DrawDebug(drawing, PhysicsDebugOptions{});
		Require(!LinesOf(drawing, PhysicsDebugOptions{}.ContactColor).empty(), "The resting boxes should have contacts to draw");
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
		TestRayCasts();
		TestShapeCastStopsAtAWall();
		TestOverlapFindsTheBodiesInside();
		TestSensorLetsABoxThroughAndTellsOfIt();
		TestABoxLandingBeginsOneContact();
		TestSleepingContactsStayOpenUntilTheBodiesPart();
		TestDistanceConstraintKeepsItsRange();
		TestHingeKeepsItsAxis();
		TestSliderKeepsItsBodyOnTheAxis();
		TestFixedConstraintHoldsTwoBodies();
		if constexpr (DebugDraw::Enabled)
		{
			TestDebugViewDrawsEachShape();
			TestDebugViewColoursByMotion();
			TestDebugViewDrawsContactsAfterALanding();
			TestDebugViewDrawsConstraints();
			TestDebugViewDrawsNothingWhenOff();
		}
	}
	catch (const std::exception& error)
	{
		std::cerr << "Physics test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All physics tests passed.\n";
	return 0;
}
