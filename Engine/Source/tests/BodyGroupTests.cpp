// Bodies made of several entities (BodyGroupComponent, PhysicsBodies, BodyDesc::Parts): a group is
// one body with each entity's collider as a part, each part's mass from its own density or mass and
// the whole's centre of mass from them; each entity's transform follows the body; each part touches
// with its own surface; a hit on a part names its entity; a constraint on a part holds the body by
// that part's frame; a group made keeps its entities' momentum, and one broken up gives each entity
// the velocity of its point; one entity put somewhere from outside makes the group again there; a
// character standing on a moving, turning group is carried with it.
#include "CharacterComponent.h"
#include "ConstraintComponent.h"
#include "GameWorld.h"
#include "GuidComponent.h"
#include "Log.h"
#include "PhysicsComponents.h"
#include "PhysicsPick.h"
#include "TestSupport.h"
#include "TransformComponent.h"

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using namespace Nyx;
	using Nyx::Tests::Require;

	constexpr float StepSeconds = 1.0f / 60.0f;

	struct Steps
	{
		GameWorld& World;
		uint64_t Next = 0;

		void Run(int count)
		{
			for (int i = 0; i < count; ++i, ++Next)
			{
				World.Step(StepTime{ StepSeconds, Next, static_cast<double>(Next) * StepSeconds });
			}
		}
	};

	std::string Describe(const glm::vec3& v)
	{
		return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
	}

	// The ground, its top at y = 0
	Entity AddFloor(Registry& world, float friction = 0.5f)
	{
		const Entity floor = world.CreateEntity();
		world.Add<TransformComponent>(floor, TransformComponent{ .Position = glm::vec3(0.0f, -0.5f, 0.0f) });
		world.Add<BoxColliderComponent>(floor, BoxColliderComponent{ .HalfExtents = glm::vec3(40.0f, 0.5f, 40.0f) });
		world.Add<PhysicsMaterialComponent>(floor, PhysicsMaterialComponent{ .Friction = friction });
		return floor;
	}

	// A 1 m cube, undamped, of `mass` kg (0: water's density), falling as `gravity` says
	Entity AddBox(Registry& world, const glm::vec3& position, float mass = 0.0f, float gravity = 1.0f, const glm::vec3& halfExtents = glm::vec3(0.5f))
	{
		const Entity box = world.CreateEntity();
		world.Add<TransformComponent>(box, TransformComponent{ .Position = position });
		world.Add<BoxColliderComponent>(box, BoxColliderComponent{ .HalfExtents = halfExtents });
		world.Add<RigidBodyComponent>(box,
			RigidBodyComponent{ .Mass = mass, .LinearDamping = 0.0f, .AngularDamping = 0.0f, .GravityFactor = gravity });
		return box;
	}

	void Join(Registry& world, Entity entity, uint64_t group)
	{
		world.Add<BodyGroupComponent>(entity, BodyGroupComponent{ group });
	}

	glm::vec3 PositionOf(GameWorld& world, Entity entity)
	{
		return world.GetRegistry().Get<TransformComponent>(entity).Position;
	}

	// One body of two parts, each its own mass, the whole's centre of mass between them; it falls as
	// one, each entity's transform following it, and lands on the floor
	void TestAGroupIsOneBody()
	{
		GameWorld world;
		Steps steps{ world };
		Registry& registry = world.GetRegistry();
		const Entity floor = AddFloor(registry);
		const Entity light = AddBox(registry, glm::vec3(-1.0f, 3.0f, 0.0f), 50.0f);
		const Entity heavy = AddBox(registry, glm::vec3(1.0f, 3.0f, 0.0f), 150.0f);
		Join(registry, light, 7);
		Join(registry, heavy, 7);
		steps.Run(1);

		const PhysicsBodies& bodies = world.GetBodies();
		const BodyHandle body = bodies.FindBody(light);
		Require(body.IsValid() && bodies.FindBody(heavy) == body, "Both entities should be one body");
		Require(bodies.GetBodyCount() == 2 && bodies.GetGroupCount() == 1 && world.GetPhysics().GetBodyCount() == 2,
			"The floor and the group should be the only bodies");
		Require(world.GetPhysics().GetPartCount(body) == 2, "The group's body should have two parts");
		const std::optional<BodyPartOf> lightPart = bodies.FindPart(light);
		const std::optional<BodyPartOf> heavyPart = bodies.FindPart(heavy);
		Require(lightPart && heavyPart && lightPart->Part == 0 && heavyPart->Part == 1, "Each entity should be a part, in the order of the entities");
		Require(std::abs(world.GetPhysics().GetPartMass(body, lightPart->Part) - 50.0f) < 0.01f &&
				std::abs(world.GetPhysics().GetPartMass(body, heavyPart->Part) - 150.0f) < 0.01f,
			"Each part should keep its own mass");
		Require(std::abs(world.GetPhysics().GetMass(body) - 200.0f) < 0.01f, "The body's mass should be its parts' together");
		const glm::vec3 centre = world.GetPhysics().GetCenterOfMass(body);
		Require(std::abs(centre.x - 0.5f) < 0.01f, "The centre of mass should be the parts' together, nearer the heavy one: " + Describe(centre));
		Require(bodies.FindEntity(world.GetPhysics(), body) == light, "A group's body should name its first entity");
		Require(bodies.GetGroupEntities(world.GetPhysics(), body).size() == 2, "The group's entities should be listed");

		steps.Run(180);
		const glm::vec3 a = PositionOf(world, light);
		const glm::vec3 b = PositionOf(world, heavy);
		std::cout << "Group landed: " << Describe(a) << " and " << Describe(b) << "\n";
		Require(std::abs(glm::distance(a, b) - 2.0f) < 1e-3f, "The entities should keep their places in the group");
		Require(a.y > 0.47f && a.y < 0.51f && b.y > 0.47f && b.y < 0.51f, "The group should land on the floor");
		const QueryFilter staticOnly{ .bStatic = true, .bMoving = false };
		Require(world.GetPhysics().Overlap(body, staticOnly, 0.05f) == std::vector<BodyHandle>{ bodies.FindBody(floor) },
			"Landed, the group's body should touch the floor");
		Require(world.GetPhysics().Overlap(body, staticOnly, 0.05f).size() == 1 && world.GetPhysics().Overlap(body).empty() == false,
			"Its own body should be left out");

		// Out of the group, each its own body again
		registry.Remove<BodyGroupComponent>(heavy);
		steps.Run(1);
		Require(bodies.GetGroupCount() == 0 && bodies.FindBody(light) != bodies.FindBody(heavy) && bodies.GetBodyCount() == 3,
			"A group of one should be the entity's own body");
		Require(std::abs(world.GetPhysics().GetMass(bodies.FindBody(heavy)) - 150.0f) < 0.01f, "Its own body should have its own mass");
	}

	// Each part touches with its own surface: a box of ice under a box of rubber, the rubber the
	// group's first, slides on the ice
	void TestEachPartKeepsItsSurface()
	{
		GameWorld world;
		Steps steps{ world };
		Registry& registry = world.GetRegistry();
		AddFloor(registry, 1.0f);
		const Entity rubber = AddBox(registry, glm::vec3(0.0f, 1.5f, 0.0f));
		registry.Add<PhysicsMaterialComponent>(rubber, PhysicsMaterialComponent{ .Friction = 1.0f });
		const Entity ice = AddBox(registry, glm::vec3(0.0f, 0.5f, 0.0f));
		registry.Add<PhysicsMaterialComponent>(ice, PhysicsMaterialComponent{ .Friction = 0.0f });
		Join(registry, rubber, 1);
		Join(registry, ice, 1);
		steps.Run(30);

		world.GetPhysics().SetLinearVelocity(world.GetBodies().FindBody(ice), glm::vec3(2.0f, 0.0f, 0.0f));
		steps.Run(60);
		const float slid = PositionOf(world, ice).x;
		std::cout << "Rubber on ice: slid " << slid << " m in a second\n";
		Require(slid > 1.8f, "Touching the floor with the ice, the group should slide on");
	}

	// A group made keeps its entities' momentum: linear, and angular about its centre of mass
	void TestAGroupMadeKeepsMomentum()
	{
		GameWorld world;
		Steps steps{ world };
		Registry& registry = world.GetRegistry();
		const Entity moving = AddBox(registry, glm::vec3(0.0f, 5.0f, -1.0f), 10.0f, 0.0f);
		const Entity still = AddBox(registry, glm::vec3(0.0f, 5.0f, 1.0f), 10.0f, 0.0f);
		steps.Run(1);
		world.GetPhysics().SetLinearVelocity(world.GetBodies().FindBody(moving), glm::vec3(2.0f, 0.0f, 0.0f));
		steps.Run(1);

		Join(registry, moving, 3);
		Join(registry, still, 3);
		steps.Run(1);
		const BodyHandle body = world.GetBodies().FindBody(moving);
		const glm::vec3 velocity = world.GetPhysics().GetLinearVelocity(body);
		const glm::vec3 turning = world.GetPhysics().GetAngularVelocity(body);
		// Two 10 kg cubes 2 m apart: 20 kg m/s along X; 20 kg m^2/s about -Y over an inertia of
		// 2 x (10/6 + 10 x 1^2)
		const float expected = -20.0f / (2.0f * (10.0f / 6.0f + 10.0f));
		std::cout << "Group made: velocity " << Describe(velocity) << ", turning " << Describe(turning) << " (expected " << expected << ")\n";
		Require(std::abs(velocity.x - 1.0f) < 0.01f && std::abs(velocity.y) < 0.01f && std::abs(velocity.z) < 0.01f,
			"Made of a 2 m/s cube and a still one of the same mass, the group should move at 1 m/s");
		Require(std::abs(turning.y - expected) < 0.01f && std::abs(turning.x) < 0.01f && std::abs(turning.z) < 0.01f,
			"The group should turn with the cubes' angular momentum about its centre");
	}

	// A spinning group broken up: each entity flies off with its point's velocity
	void TestABrokenGroupFliesApart()
	{
		GameWorld world;
		Steps steps{ world };
		Registry& registry = world.GetRegistry();
		const Entity left = AddBox(registry, glm::vec3(-1.0f, 5.0f, 0.0f), 10.0f, 0.0f);
		const Entity right = AddBox(registry, glm::vec3(1.0f, 5.0f, 0.0f), 10.0f, 0.0f);
		Join(registry, left, 9);
		Join(registry, right, 9);
		steps.Run(1);
		const BodyHandle body = world.GetBodies().FindBody(left);
		world.GetPhysics().SetAngularVelocity(body, glm::vec3(0.0f, 2.0f, 0.0f));
		steps.Run(1);

		const glm::vec3 centre = world.GetPhysics().GetCenterOfMass(body);
		const glm::vec3 rightPoint = PositionOf(world, right);
		const glm::vec3 expected = glm::cross(glm::vec3(0.0f, 2.0f, 0.0f), rightPoint - centre);
		registry.Remove<BodyGroupComponent>(left);
		registry.Remove<BodyGroupComponent>(right);
		steps.Run(1);

		const BodyHandle rightBody = world.GetBodies().FindBody(right);
		Require(rightBody.IsValid() && rightBody != world.GetBodies().FindBody(left), "Broken up, each should be a body of its own");
		const glm::vec3 velocity = world.GetPhysics().GetLinearVelocity(rightBody);
		const glm::vec3 turning = world.GetPhysics().GetAngularVelocity(rightBody);
		std::cout << "Broken up while spinning: the right cube flies at " << Describe(velocity) << ", its point moved at " << Describe(expected)
				  << "\n";
		Require(glm::distance(velocity, expected) < 0.05f, "Each part should keep the velocity of its point");
		Require(std::abs(turning.y - 2.0f) < 0.01f, "Each part should keep turning as the group turned");
		Require(glm::distance(world.GetPhysics().GetLinearVelocity(world.GetBodies().FindBody(left)), -expected) < 0.05f,
			"The other part should fly the other way");
	}

	// A ray hitting a part names its entity
	void TestAHitNamesThePart()
	{
		GameWorld world;
		Steps steps{ world };
		Registry& registry = world.GetRegistry();
		const Entity left = AddBox(registry, glm::vec3(-1.0f, 2.0f, 0.0f), 0.0f, 0.0f);
		const Entity right = AddBox(registry, glm::vec3(1.0f, 2.0f, 0.0f), 0.0f, 0.0f);
		Join(registry, left, 4);
		Join(registry, right, 4);
		steps.Run(1);

		const std::optional<PhysicsPick> leftHit = PickPhysicsBody(world, Ray{ glm::vec3(-1.0f, 10.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f) }, 20.0f);
		const std::optional<PhysicsPick> rightHit = PickPhysicsBody(world, Ray{ glm::vec3(1.0f, 10.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f) }, 20.0f);
		Require(leftHit && leftHit->HitEntity == left && rightHit && rightHit->HitEntity == right, "A hit on a part should name its entity");
		Require(leftHit && std::abs(leftHit->Point.y - 2.5f) < 1e-3f, "The hit should be on the part's top");
	}

	// A constraint on a part, not the group's first, holds the body by that part's frame
	void TestAConstraintHoldsByItsPart()
	{
		GameWorld world;
		Steps steps{ world };
		Registry& registry = world.GetRegistry();
		const Entity first = AddBox(registry, glm::vec3(-1.0f, 4.0f, 0.0f), 10.0f);
		const Entity held = AddBox(registry, glm::vec3(1.0f, 4.0f, 0.0f), 10.0f);
		registry.Add<GuidComponent>(held, GuidComponent{ EntityGuid::Generate() });
		Join(registry, first, 2);
		Join(registry, held, 2);
		// Its middle held to the world where it is
		registry.Add<ConstraintComponent>(held,
			ConstraintComponent{ .Kind = EJointKind::Fixed, .OtherPoint = glm::vec3(1.0f, 4.0f, 0.0f) });
		steps.Run(120);

		const glm::vec3 position = PositionOf(world, held);
		std::cout << "Group held by its second part: it is at " << Describe(position) << ", the first at " << Describe(PositionOf(world, first)) << "\n";
		Require(glm::distance(position, glm::vec3(1.0f, 4.0f, 0.0f)) < 0.02f, "The part held should stay where it is held");
		Require(glm::distance(PositionOf(world, first), glm::vec3(-1.0f, 4.0f, 0.0f)) < 0.05f, "Held fixed, the whole group should stay");
	}

	// One entity put somewhere from outside makes the group again, where they all are now, stopped
	void TestAnEntityPutSomewhereRemakesTheGroup()
	{
		GameWorld world;
		Steps steps{ world };
		Registry& registry = world.GetRegistry();
		const Entity a = AddBox(registry, glm::vec3(0.0f, 10.0f, 0.0f));
		const Entity b = AddBox(registry, glm::vec3(2.0f, 10.0f, 0.0f));
		Join(registry, a, 5);
		Join(registry, b, 5);
		steps.Run(30);
		const BodyHandle before = world.GetBodies().FindBody(a);

		registry.Get<TransformComponent>(b).Position = glm::vec3(0.0f, 20.0f, 3.0f);
		steps.Run(1);
		const BodyHandle after = world.GetBodies().FindBody(a);
		Require(after.IsValid() && after != before && world.GetBodies().FindBody(b) == after, "Moving one entity should make the group again");
		Require(world.GetPhysics().GetLinearVelocity(after).y > -0.5f, "Made again where it was put, the group should start from rest");
		const float apart = glm::distance(PositionOf(world, a), PositionOf(world, b));
		Require(std::abs(apart - glm::distance(glm::vec3(0.0f, 9.0f, 0.0f), glm::vec3(0.0f, 20.0f, 3.0f))) < 0.5f,
			"The entities should keep the places they were put in");
	}

	// A character standing on a group that moves and turns is carried with it
	void TestACharacterRidesAMovingTurningGroup()
	{
		GameWorld world;
		Steps steps{ world };
		Registry& registry = world.GetRegistry();
		AddFloor(registry);
		// A floating platform 8 m by 4, of two slabs
		const Entity west = AddBox(registry, glm::vec3(-2.0f, 2.0f, 0.0f), 20000.0f, 0.0f, glm::vec3(2.0f, 0.25f, 2.0f));
		const Entity east = AddBox(registry, glm::vec3(2.0f, 2.0f, 0.0f), 20000.0f, 0.0f, glm::vec3(2.0f, 0.25f, 2.0f));
		Join(registry, west, 11);
		Join(registry, east, 11);
		const Entity rider = registry.CreateEntity();
		registry.Add<TransformComponent>(rider, TransformComponent{ .Position = glm::vec3(2.5f, 2.3f, 0.0f) });
		registry.Add<CharacterComponent>(rider, CharacterComponent{});
		steps.Run(30);
		Require(registry.Get<CharacterComponent>(rider).bOnGround, "The character should stand on the platform");

		// The rider's feet in the platform's frame
		const BodyHandle platform = world.GetBodies().FindBody(west);
		PhysicsWorld& physics = world.GetPhysics();
		BodyPose pose = physics.GetPose(platform);
		const glm::vec3 local = glm::inverse(pose.Rotation) * (PositionOf(world, rider) - pose.Position);

		// Moving 1 m/s east and turning a quarter turn in about 3 s, for 2 s
		for (int step = 0; step < 120; ++step)
		{
			physics.SetLinearVelocity(platform, glm::vec3(1.0f, 0.0f, 0.0f));
			physics.SetAngularVelocity(platform, glm::vec3(0.0f, 0.5f, 0.0f));
			steps.Run(1);
		}
		pose = physics.GetPose(platform);
		const glm::vec3 carried = pose.Position + pose.Rotation * local;
		const glm::vec3 position = PositionOf(world, rider);
		const float turned = glm::degrees(glm::angle(pose.Rotation));
		std::cout << "Riding the platform: it turned " << turned << " degrees; the rider is at " << Describe(position) << ", its spot on it at "
				  << Describe(carried) << "\n";
		Require(turned > 50.0f, "The platform should have turned");
		Require(glm::distance(glm::vec2(position.x, position.z), glm::vec2(carried.x, carried.z)) < 0.3f,
			"The character should be carried with the platform, moving and turning");
		Require(registry.Get<CharacterComponent>(rider).bOnGround, "The character should still stand on it");
	}
}

int main()
{
	Nyx::Logger::Get().Init();

	try
	{
		TestAGroupIsOneBody();
		TestEachPartKeepsItsSurface();
		TestAGroupMadeKeepsMomentum();
		TestABrokenGroupFliesApart();
		TestAHitNamesThePart();
		TestAConstraintHoldsByItsPart();
		TestAnEntityPutSomewhereRemakesTheGroup();
		TestACharacterRidesAMovingTurningGroup();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Body group test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All body group tests passed.\n";
	return 0;
}
