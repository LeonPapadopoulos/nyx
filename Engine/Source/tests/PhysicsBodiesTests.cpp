// Physics in the game world (PhysicsBodies, GameWorld): bodies made, moved, remade and destroyed
// with their entities' components, transforms written back, a transform set from outside
// teleporting its body, kinematic bodies following their transforms, systems before and after
// the physics step, and physics components in scene files. Moving bodies keep where they were
// before the last step, to be drawn between the two, and the maths of drawing them so.
#include "ComponentRegistration.h"
#include "GameWorld.h"
#include "Log.h"
#include "PhysicsComponents.h"
#include "PreviousTransformComponent.h"
#include "SceneSerializer.h"
#include "TestSupport.h"
#include "TransformComponent.h"

#include <glm/gtc/constants.hpp>

#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace
{
	using namespace Nyx;
	using Nyx::Tests::Require;

	struct Steps
	{
		GameWorld& World;
		uint64_t Next = 0;

		void Run(int count)
		{
			for (int i = 0; i < count; ++i, ++Next)
			{
				World.Step(StepTime{ 1.0 / 60.0, Next, static_cast<double>(Next) / 60.0 });
			}
		}
	};

	Entity AddFloor(Registry& world)
	{
		const Entity floor = world.CreateEntity();
		world.Add<TransformComponent>(floor, TransformComponent{ .Position = glm::vec3(0.0f, -0.5f, 0.0f) });
		world.Add<BoxColliderComponent>(floor, BoxColliderComponent{ .HalfExtents = glm::vec3(20.0f, 0.5f, 20.0f) });
		return floor;
	}

	Entity AddCrate(Registry& world, const glm::vec3& position)
	{
		const Entity crate = world.CreateEntity();
		world.Add<TransformComponent>(crate, TransformComponent{ .Position = position });
		world.Add<BoxColliderComponent>(crate, BoxColliderComponent{});
		world.Add<RigidBodyComponent>(crate, RigidBodyComponent{});
		return crate;
	}

	float HeightOf(GameWorld& world, Entity entity)
	{
		return world.GetRegistry().Get<TransformComponent>(entity).Position.y;
	}

	void TestACrateFallsOntoTheFloor()
	{
		GameWorld world;
		Steps steps{ world };
		AddFloor(world.GetRegistry());
		const Entity crate = AddCrate(world.GetRegistry(), glm::vec3(0.0f, 4.0f, 0.0f));

		steps.Run(1);
		Require(world.GetBodies().GetBodyCount() == 2, "The floor and the crate should have bodies");
		Require(world.GetBodies().FindBody(crate).IsValid(), "The crate's body should be found by its entity");
		Require(world.GetBodies().FindEntity(world.GetPhysics(), world.GetBodies().FindBody(crate)) == crate,
			"The crate should be found by its body");

		steps.Run(300);
		// Jolt lets resting bodies sink up to 2 cm before pushing them out
		const float height = HeightOf(world, crate);
		Require(height > 0.475f && height < 0.505f, "The crate's transform should rest on the floor (" + std::to_string(height) + ")");
	}

	void TestASetTransformTeleports()
	{
		GameWorld world;
		Steps steps{ world };
		AddFloor(world.GetRegistry());
		const Entity crate = AddCrate(world.GetRegistry(), glm::vec3(0.0f, 4.0f, 0.0f));
		steps.Run(30);
		Require(world.GetPhysics().GetLinearVelocity(world.GetBodies().FindBody(crate)).y < -1.0f, "The crate should be falling");

		// As an editor's live edit does
		world.GetRegistry().Get<TransformComponent>(crate).Position = glm::vec3(5.0f, 10.0f, 0.0f);
		steps.Run(1);
		const glm::vec3 position = world.GetRegistry().Get<TransformComponent>(crate).Position;
		Require(std::abs(position.x - 5.0f) < 1e-3f && position.y > 9.9f, "A transform set from outside should move the body there");
		Require(world.GetPhysics().GetLinearVelocity(world.GetBodies().FindBody(crate)).y > -0.5f,
			"Moving the body there should stop it, then it falls anew");
	}

	void TestChangesRemakeAndRemoveBodies()
	{
		GameWorld world;
		Steps steps{ world };
		Registry& registry = world.GetRegistry();
		const Entity crate = AddCrate(registry, glm::vec3(0.0f, 4.0f, 0.0f));
		steps.Run(1);
		const BodyHandle first = world.GetBodies().FindBody(crate);

		registry.Get<BoxColliderComponent>(crate).HalfExtents = glm::vec3(1.0f);
		steps.Run(1);
		const BodyHandle remade = world.GetBodies().FindBody(crate);
		Require(remade.IsValid() && remade != first, "A changed collider should make the body again");
		Require(std::abs(world.GetPhysics().GetMass(remade) - 8000.0f) < 1.0f, "The new body should have the new size's mass");

		registry.Get<TransformComponent>(crate).Scale = glm::vec3(0.5f);
		steps.Run(1);
		Require(std::abs(world.GetPhysics().GetMass(world.GetBodies().FindBody(crate)) - 1000.0f) < 1.0f,
			"The entity's scale should scale the shape");

		registry.Remove<RigidBodyComponent>(crate);
		steps.Run(1);
		Require(world.GetPhysics().GetMass(world.GetBodies().FindBody(crate)) == 0.0f, "Without a rigid body, the body should be static");

		registry.Remove<BoxColliderComponent>(crate);
		steps.Run(1);
		Require(!world.GetBodies().FindBody(crate).IsValid() && world.GetPhysics().GetBodyCount() == 0,
			"Without a collider, the body should go");

		const Entity other = AddCrate(registry, glm::vec3(0.0f));
		steps.Run(1);
		registry.DestroyEntity(other);
		steps.Run(1);
		Require(world.GetPhysics().GetBodyCount() == 0, "A destroyed entity's body should go");
	}

	void TestKinematicBodiesFollowTheirTransforms()
	{
		GameWorld world;
		Steps steps{ world };
		Registry& registry = world.GetRegistry();
		const Entity platform = AddCrate(registry, glm::vec3(0.0f));
		registry.Get<RigidBodyComponent>(platform).bKinematic = true;
		steps.Run(30);
		Require(HeightOf(world, platform) == 0.0f, "A kinematic body shouldn't fall");

		registry.Get<TransformComponent>(platform).Position.y = 2.0f;
		steps.Run(1);
		const BodyPose pose = world.GetPhysics().GetPose(world.GetBodies().FindBody(platform));
		Require(std::abs(pose.Position.y - 2.0f) < 1e-4f, "A kinematic body should follow its transform over a step");
	}

	// Holds every body with a rigid body up against gravity, before the physics step
	class AntiGravitySystem final : public ISimSystem
	{
	public:
		const char* GetName() const override
		{
			return "Anti-Gravity";
		}

		void Step(const SimContext& context) override
		{
			context.World.Each<RigidBodyComponent>(
				[&](Entity entity, RigidBodyComponent&)
				{
					const BodyHandle body = context.Bodies.FindBody(entity);
					context.Physics.AddForce(body, -context.Physics.GetGravity() * context.Physics.GetMass(body));
				});
		}
	};

	// Writes down the height of every body after the physics step
	class HeightAfterPhysicsSystem final : public ISimSystem
	{
	public:
		const char* GetName() const override
		{
			return "Height After Physics";
		}

		void Step(const SimContext& context) override
		{
			context.World.Each<RigidBodyComponent>([&](Entity entity, RigidBodyComponent&)
				{
					Height = context.World.Get<TransformComponent>(entity).Position.y;
				});
		}

		float Height = 0.0f;
	};

	void TestSystemsBeforeAndAfterPhysics()
	{
		GameWorld world;
		Steps steps{ world };
		const Entity crate = AddCrate(world.GetRegistry(), glm::vec3(0.0f, 3.0f, 0.0f));
		world.AddSystem(std::make_unique<AntiGravitySystem>());
		auto& after = static_cast<HeightAfterPhysicsSystem&>(
			world.AddSystem(std::make_unique<HeightAfterPhysicsSystem>(), 1, ESimStage::AfterPhysics));

		steps.Run(120);
		Require(std::abs(HeightOf(world, crate) - 3.0f) < 1e-3f, "A force before the physics step should act in it: the crate hovers");
		Require(after.Height == HeightOf(world, crate), "A system after the physics step should see the transforms written back");
	}

	void TestARigidBodyWithoutColliderWarnsOnce()
	{
		GameWorld world;
		Steps steps{ world };
		Registry& registry = world.GetRegistry();
		const Entity cube = registry.CreateEntity();
		registry.Add<TransformComponent>(cube, TransformComponent{});
		registry.Add<RigidBodyComponent>(cube, RigidBodyComponent{});

		const uint64_t before = Logger::Get().GetCounts().Warnings;
		steps.Run(30);
		Require(Logger::Get().GetCounts().Warnings == before + 1, "A rigid body without a collider should be warned of, once");
		Require(world.GetPhysics().GetBodyCount() == 0, "A rigid body without a collider has no body");

		registry.Add<BoxColliderComponent>(cube, BoxColliderComponent{});
		steps.Run(30);
		Require(Logger::Get().GetCounts().Warnings == before + 1 && world.GetPhysics().GetBodyCount() == 1,
			"With a collider, it should get a body and no more warnings");

		registry.Remove<BoxColliderComponent>(cube);
		steps.Run(5);
		Require(Logger::Get().GetCounts().Warnings == before + 2, "Losing the collider again should warn again");
	}

	bool Near(const glm::vec3& a, const glm::vec3& b)
	{
		return glm::length(a - b) < 1e-5f;
	}

	// The angle between two rotations, in radians
	float AngleBetween(const glm::quat& a, const glm::quat& b)
	{
		return 2.0f * std::acos(std::min(std::abs(glm::dot(glm::normalize(a), glm::normalize(b))), 1.0f));
	}

	void TestInterpolatingTransforms()
	{
		const glm::vec3 up(0.0f, 1.0f, 0.0f);
		TransformComponent current{ .Position = glm::vec3(2.0f, 4.0f, 0.0f),
			.Rotation = glm::angleAxis(glm::half_pi<float>(), up),
			.Scale = glm::vec3(3.0f) };
		PreviousTransformComponent previous;
		SetPreviousTransform(previous, glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), current);

		const TransformComponent start = InterpolateTransform(previous, current, 0.0f);
		Require(Near(start.Position, glm::vec3(0.0f)) && AngleBetween(start.Rotation, glm::quat(1.0f, 0.0f, 0.0f, 0.0f)) < 1e-3f,
			"At 0, the transform should be the previous one");
		const TransformComponent end = InterpolateTransform(previous, current, 1.0f);
		Require(Near(end.Position, current.Position) && AngleBetween(end.Rotation, current.Rotation) < 1e-3f,
			"At 1, the transform should be the current one");

		const TransformComponent half = InterpolateTransform(previous, current, 0.5f);
		Require(Near(half.Position, glm::vec3(1.0f, 2.0f, 0.0f)), "Halfway, the position should be halfway");
		Require(AngleBetween(half.Rotation, glm::angleAxis(glm::quarter_pi<float>(), up)) < 1e-3f,
			"Halfway, the rotation should have turned half of the quarter turn");
		Require(std::abs(glm::length(half.Rotation) - 1.0f) < 1e-5f, "The rotation drawn should be a unit quaternion");
		Require(half.Scale == current.Scale, "The scale should be the current one");

		// The same rotation with the other sign: still a turn the short way, not most of the way round
		current.Rotation = -current.Rotation;
		SetPreviousTransform(previous, glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), current);
		Require(AngleBetween(InterpolateTransform(previous, current, 0.5f).Rotation, glm::angleAxis(glm::quarter_pi<float>(), up)) < 1e-3f,
			"Rotations should turn the short way round, whatever their sign");

		Require(Near(InterpolateTransform(previous, current, 2.0f).Position, current.Position),
			"A factor beyond 1 shouldn't draw the transform beyond the current one");

		// Set from outside since the step, a teleport: drawn where it was put
		TransformComponent teleported = current;
		teleported.Position = glm::vec3(10.0f, 0.0f, 0.0f);
		Require(InterpolateTransform(previous, teleported, 0.5f).Position == teleported.Position,
			"A transform set since the step shouldn't be smeared from the previous one");
		teleported = current;
		teleported.Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		Require(InterpolateTransform(previous, teleported, 0.5f).Rotation == teleported.Rotation,
			"A rotation set since the step shouldn't be smeared from the previous one");
	}

	void TestMovingBodiesKeepTheirPreviousPose()
	{
		GameWorld world;
		Steps steps{ world };
		Registry& registry = world.GetRegistry();
		const Entity floor = AddFloor(registry);
		const Entity crate = AddCrate(registry, glm::vec3(0.0f, 4.0f, 0.0f));
		const Entity platform = AddCrate(registry, glm::vec3(5.0f, 0.0f, 0.0f));
		registry.Get<RigidBodyComponent>(platform).bKinematic = true;

		steps.Run(10);
		const glm::vec3 before = registry.Get<TransformComponent>(crate).Position;
		steps.Run(1);
		const TransformComponent& transform = registry.Get<TransformComponent>(crate);
		Require(registry.Has<PreviousTransformComponent>(crate), "A falling crate should keep where it was before the step");
		const PreviousTransformComponent& previous = registry.Get<PreviousTransformComponent>(crate);
		Require(previous.Position == before && transform.Position.y < before.y, "The previous pose should be the one before the step");
		Require(previous.StepPosition == transform.Position && previous.StepRotation == transform.Rotation,
			"The pose the step left should be the transform written");
		Require(!registry.Has<PreviousTransformComponent>(floor) && !registry.Has<PreviousTransformComponent>(platform),
			"Static and kinematic bodies shouldn't get a previous pose");

		// Set from outside, as an editor's live edit does: drawn there at once, and the next step
		// starts from there
		const glm::vec3 target(3.0f, 10.0f, 0.0f);
		registry.Get<TransformComponent>(crate).Position = target;
		Require(InterpolateTransform(registry.Get<PreviousTransformComponent>(crate), registry.Get<TransformComponent>(crate), 0.5f).Position == target,
			"A crate put somewhere should be drawn there before the next step");
		steps.Run(1);
		Require(registry.Get<PreviousTransformComponent>(crate).Position == target,
			"After a teleport, the previous pose should be where the crate was put, not where it fell from");

		// At rest, asleep, it no longer moves and keeps nothing
		steps.Run(400);
		Require(!world.GetPhysics().IsAwake(world.GetBodies().FindBody(crate)), "The crate should be asleep by now");
		Require(!registry.Has<PreviousTransformComponent>(crate), "A crate that didn't move in the step shouldn't keep a previous pose");

		// Made static while it moves: drawn where it is from then on
		registry.Get<TransformComponent>(crate).Position.y = 6.0f;
		steps.Run(5);
		Require(registry.Has<PreviousTransformComponent>(crate), "The crate should be falling again");
		registry.Remove<RigidBodyComponent>(crate);
		steps.Run(1);
		Require(!registry.Has<PreviousTransformComponent>(crate), "A body made static shouldn't keep a previous pose");

		registry.Add<RigidBodyComponent>(crate, RigidBodyComponent{});
		steps.Run(5);
		registry.Remove<BoxColliderComponent>(crate);
		steps.Run(1);
		Require(!registry.Has<PreviousTransformComponent>(crate), "An entity whose body went shouldn't keep a previous pose");
	}

	void TestScenesKeepPhysicsComponents(const std::filesystem::path& directory)
	{
		Nyx::RegisterComponentTypes();
		Registry world;
		const Entity crate = AddCrate(world, glm::vec3(1.0f, 2.0f, 3.0f));
		world.Get<RigidBodyComponent>(crate) = RigidBodyComponent{ .bKinematic = true, .Mass = 12.0f, .GravityFactor = 0.5f };
		world.Add<PhysicsMaterialComponent>(crate, PhysicsMaterialComponent{ .Friction = 0.9f, .Restitution = 0.3f });
		const Entity ball = world.CreateEntity();
		world.Add<SphereColliderComponent>(ball, SphereColliderComponent{ .Radius = 0.25f });
		const Entity pill = world.CreateEntity();
		world.Add<CapsuleColliderComponent>(pill, CapsuleColliderComponent{ .Radius = 0.3f, .HalfHeight = 0.7f });

		const std::filesystem::path path = directory / "Physics.nyxscene";
		Require(SceneSerializer::SaveToFile(world, path), "Saving a scene with physics components failed");
		Registry loaded;
		Require(SceneSerializer::LoadFromFile(path, loaded), "Loading a scene with physics components failed");

		int rigidBodies = 0;
		loaded.Each<RigidBodyComponent>([&](Entity entity, RigidBodyComponent& body)
			{
				++rigidBodies;
				Require(body.bKinematic && body.Mass == 12.0f && body.GravityFactor == 0.5f, "The rigid body's values should be kept");
				Require(loaded.Get<PhysicsMaterialComponent>(entity).Restitution == 0.3f, "The physics material should be kept");
			});
		int spheres = 0;
		loaded.Each<SphereColliderComponent>([&](Entity, SphereColliderComponent& sphere)
			{
				spheres += sphere.Radius == 0.25f ? 1 : 0;
			});
		int capsules = 0;
		loaded.Each<CapsuleColliderComponent>([&](Entity, CapsuleColliderComponent& capsule)
			{
				capsules += capsule.HalfHeight == 0.7f ? 1 : 0;
			});
		Require(rigidBodies == 1 && spheres == 1 && capsules == 1, "Every physics component should be saved and loaded");
	}
}

int main()
{
	Nyx::Logger::Get().Init();

	try
	{
		Nyx::Tests::TestDirectory directory("NyxPhysicsBodiesTests");
		TestACrateFallsOntoTheFloor();
		TestASetTransformTeleports();
		TestChangesRemakeAndRemoveBodies();
		TestKinematicBodiesFollowTheirTransforms();
		TestSystemsBeforeAndAfterPhysics();
		TestARigidBodyWithoutColliderWarnsOnce();
		TestInterpolatingTransforms();
		TestMovingBodiesKeepTheirPreviousPose();
		TestScenesKeepPhysicsComponents(directory.Path);
	}
	catch (const std::exception& error)
	{
		std::cerr << "Physics bodies test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All physics bodies tests passed.\n";
	return 0;
}
