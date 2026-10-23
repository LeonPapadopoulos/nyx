// Characters in the game world (CharacterComponent, PhysicsBodies::MoveCharacters(), Jolt's
// CharacterVirtual behind PhysicsWorld): falling and landing, walking at its speed, climbing steps
// up to its step height, sliding down ground too steep and standing on ground it can walk, jumping
// as high as its jump speed says, pushing a light crate, rays that see it or leave it out, live
// edits and shape changes, its previous pose for drawing, the debug view's capsule, scene files, and
// the same inputs giving the same path. The sample scene loads, and its character stands.
//
// NyxCharacterTests --write-scene writes the sample scene, Assets/Scenes/CharacterTest.nyxscene,
// and its materials into the assets folder: this file is their source.
#include "CameraComponent.h"
#include "CharacterComponent.h"
#include "ComponentRegistration.h"
#include "DirectionalLightComponent.h"
#include "GameWorld.h"
#include "GuidComponent.h"
#include "Log.h"
#include "MeshRendererComponent.h"
#include "NameComponent.h"
#include "Paths.h"
#include "PhysicsComponents.h"
#include "PhysicsPick.h"
#include "PreviousTransformComponent.h"
#include "SceneSerializer.h"
#include "TestSupport.h"
#include "TransformComponent.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
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

	// A static box, its top `height` above the ground's, from `minX` to `maxX` and 4 m wide
	Entity AddBlock(Registry& world, float minX, float maxX, float height)
	{
		const Entity block = world.CreateEntity();
		world.Add<TransformComponent>(block, TransformComponent{ .Position = glm::vec3(0.5f * (minX + maxX), 0.5f * height, 0.0f) });
		world.Add<BoxColliderComponent>(block, BoxColliderComponent{ .HalfExtents = glm::vec3(0.5f * (maxX - minX), 0.5f * height, 2.0f) });
		return block;
	}

	// The ground, its top at y = 0
	Entity AddFloor(Registry& world)
	{
		const Entity floor = world.CreateEntity();
		world.Add<TransformComponent>(floor, TransformComponent{ .Position = glm::vec3(0.0f, -0.5f, 0.0f) });
		world.Add<BoxColliderComponent>(floor, BoxColliderComponent{ .HalfExtents = glm::vec3(30.0f, 0.5f, 30.0f) });
		return floor;
	}

	// A plank tilted `degrees` about Z, rising towards +X, its top surface through `center`
	Entity AddRamp(Registry& world, const glm::vec3& center, float degrees)
	{
		const glm::quat rotation = glm::angleAxis(glm::radians(degrees), glm::vec3(0.0f, 0.0f, 1.0f));
		const Entity ramp = world.CreateEntity();
		world.Add<TransformComponent>(ramp, TransformComponent{ .Position = center - rotation * glm::vec3(0.0f, 0.1f, 0.0f), .Rotation = rotation });
		world.Add<BoxColliderComponent>(ramp, BoxColliderComponent{ .HalfExtents = glm::vec3(4.0f, 0.1f, 2.0f) });
		return ramp;
	}

	Entity AddCharacter(Registry& world, const glm::vec3& feet, const CharacterComponent& character = {})
	{
		const Entity entity = world.CreateEntity();
		world.Add<TransformComponent>(entity, TransformComponent{ .Position = feet });
		world.Add<CharacterComponent>(entity, character);
		return entity;
	}

	glm::vec3 PositionOf(GameWorld& world, Entity entity)
	{
		return world.GetRegistry().Get<TransformComponent>(entity).Position;
	}

	CharacterComponent& CharacterOf(GameWorld& world, Entity entity)
	{
		return world.GetRegistry().Get<CharacterComponent>(entity);
	}

	std::string Describe(const glm::vec3& v)
	{
		return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
	}

	void TestItFallsAndLandsOnTheFloor()
	{
		GameWorld world;
		Steps steps{ world };
		AddFloor(world.GetRegistry());
		const Entity character = AddCharacter(world.GetRegistry(), glm::vec3(0.0f, 3.0f, 0.0f));

		steps.Run(1);
		Require(world.GetBodies().GetCharacterCount() == 1 && world.GetPhysics().GetCharacterCount() == 1, "The character should be made");
		Require(!CharacterOf(world, character).bOnGround && PositionOf(world, character).y < 3.0f, "In the air, it should fall");

		steps.Run(120);
		const glm::vec3 position = PositionOf(world, character);
		Require(std::abs(position.y) < 0.03f, "Its feet should rest on the floor " + Describe(position));
		Require(std::abs(position.x) < 1e-3f && std::abs(position.z) < 1e-3f, "Falling straight down shouldn't move it sideways");
		Require(CharacterOf(world, character).bOnGround, "On the floor, it should be on the ground");
		Require(glm::length(CharacterOf(world, character).GroundNormal - glm::vec3(0.0f, 1.0f, 0.0f)) < 1e-3f, "The floor faces up");
		Require(glm::length(CharacterOf(world, character).Velocity) < 1e-3f, "At rest, it shouldn't move");
	}

	void TestItWalksAtItsSpeed()
	{
		GameWorld world;
		Steps steps{ world };
		AddFloor(world.GetRegistry());
		// At its speed from the first step, so the distance is speed times time
		const Entity character = AddCharacter(world.GetRegistry(), glm::vec3(0.0f), CharacterComponent{ .WalkSpeed = 3.0f, .Acceleration = 10000.0f });
		steps.Run(30);

		const glm::vec3 start = PositionOf(world, character);
		CharacterOf(world, character).DesiredMove = glm::vec3(0.0f, 0.0f, -2.0f);
		steps.Run(120);
		const glm::vec3 walked = PositionOf(world, character) - start;
		Require(std::abs(walked.z + 6.0f) < 0.05f, "Two seconds at 3 m/s should walk 6 m " + Describe(walked));
		Require(std::abs(walked.x) < 1e-3f && std::abs(walked.y) < 0.01f, "It should walk straight along the floor " + Describe(walked));
		Require(std::abs(glm::length(CharacterOf(world, character).Velocity) - 3.0f) < 0.01f,
			"A desired move longer than 1 shouldn't walk faster than its speed");

		// The default acceleration takes a moment to stop it
		CharacterOf(world, character).Acceleration = 30.0f;
		CharacterOf(world, character).DesiredMove = glm::vec3(0.0f);
		steps.Run(1);
		Require(std::abs(glm::length(CharacterOf(world, character).Velocity) - 2.5f) < 0.01f, "It should slow down at its acceleration");
		steps.Run(10);
		Require(glm::length(CharacterOf(world, character).Velocity) < 1e-3f, "Without a move, it should stop");
	}

	// Walks a character along +X towards a block of `height` from x = 2, for 1.2 s
	glm::vec3 WalkAtAStep(float height)
	{
		GameWorld world;
		Steps steps{ world };
		AddFloor(world.GetRegistry());
		AddBlock(world.GetRegistry(), 2.0f, 8.0f, height);
		const Entity character = AddCharacter(world.GetRegistry(), glm::vec3(0.0f));
		steps.Run(10);
		CharacterOf(world, character).DesiredMove = glm::vec3(1.0f, 0.0f, 0.0f);
		steps.Run(72);
		return PositionOf(world, character);
	}

	void TestItClimbsStepsUpToItsStepHeight()
	{
		// The default step height is 0.3 m
		const glm::vec3 low = WalkAtAStep(0.25f);
		Require(std::abs(low.y - 0.25f) < 0.03f && low.x > 3.5f, "It should climb a step lower than its step height and walk on " + Describe(low));

		const glm::vec3 high = WalkAtAStep(0.45f);
		Require(std::abs(high.y) < 0.03f && high.x < 2.0f - 0.25f, "A step higher than its step height should stop it " + Describe(high));
	}

	// Drops a character onto a ramp of `degrees`, lets it land, and returns where it is then and a
	// second later, without a move
	std::pair<glm::vec3, glm::vec3> StandOnARamp(float degrees, bool& bOnGround)
	{
		GameWorld world;
		Steps steps{ world };
		AddFloor(world.GetRegistry());
		AddRamp(world.GetRegistry(), glm::vec3(0.0f, 3.0f, 0.0f), degrees);
		const Entity character = AddCharacter(world.GetRegistry(), glm::vec3(0.0f, 3.5f, 0.0f));
		steps.Run(30);
		const glm::vec3 landed = PositionOf(world, character);
		bOnGround = CharacterOf(world, character).bOnGround;
		steps.Run(60);
		return { landed, PositionOf(world, character) };
	}

	void TestItSlidesDownGroundTooSteep()
	{
		bool bOnGround = false;
		const auto [walkableLanded, walkableLater] = StandOnARamp(30.0f, bOnGround);
		Require(bOnGround, "Ground less steep than its max slope should be ground it stands on");
		Require(glm::length(walkableLater - walkableLanded) < 0.01f,
			"On ground it can walk, it should stay put " + Describe(walkableLanded) + " " + Describe(walkableLater));

		const auto [steepLanded, steepLater] = StandOnARamp(60.0f, bOnGround);
		Require(!bOnGround, "Ground steeper than its max slope shouldn't be ground it stands on");
		Require(steepLater.x < steepLanded.x - 1.0f && steepLater.y < steepLanded.y - 1.0f,
			"On ground too steep, it should slide down " + Describe(steepLanded) + " " + Describe(steepLater));
	}

	void TestItJumpsAsHighAsItsSpeedSays()
	{
		GameWorld world;
		Steps steps{ world };
		AddFloor(world.GetRegistry());
		const Entity character = AddCharacter(world.GetRegistry(), glm::vec3(0.0f), CharacterComponent{ .JumpSpeed = 5.0f });
		steps.Run(10);

		CharacterOf(world, character).bJumpRequested = true;
		float highest = 0.0f;
		for (int step = 0; step < 120; ++step)
		{
			steps.Run(1);
			Require(!CharacterOf(world, character).bJumpRequested, "A jump should be taken by the next step");
			highest = std::max(highest, PositionOf(world, character).y);
		}
		// v * v / 2g; a step's worth less, as each step moves at the speed it has at its end
		const float expected = 25.0f / (2.0f * 9.81f);
		Require(std::abs(highest - expected) < 0.06f, "It should rise " + std::to_string(expected) + " m, not " + std::to_string(highest));
		Require(CharacterOf(world, character).bOnGround && std::abs(PositionOf(world, character).y) < 0.03f, "It should land again");

		// Asked in the air: dropped, not kept for the landing
		CharacterOf(world, character).bJumpRequested = true;
		steps.Run(1);
		CharacterOf(world, character).bJumpRequested = true;
		steps.Run(5);
		const float rising = CharacterOf(world, character).Velocity.y;
		Require(std::abs(rising - (5.0f - 6.0f * 9.81f * StepSeconds)) < 0.01f, "A jump asked for in the air shouldn't jump again");
	}

	void TestItPushesALightCrate()
	{
		GameWorld world;
		Steps steps{ world };
		Registry& registry = world.GetRegistry();
		AddFloor(registry);
		const Entity crate = registry.CreateEntity();
		registry.Add<TransformComponent>(crate, TransformComponent{ .Position = glm::vec3(1.5f, 0.25f, 0.0f) });
		registry.Add<BoxColliderComponent>(crate, BoxColliderComponent{ .HalfExtents = glm::vec3(0.25f) });
		registry.Add<RigidBodyComponent>(crate, RigidBodyComponent{ .Mass = 5.0f });
		const Entity character = AddCharacter(registry, glm::vec3(0.0f));
		steps.Run(30);

		CharacterOf(world, character).DesiredMove = glm::vec3(1.0f, 0.0f, 0.0f);
		steps.Run(90);
		const float pushed = PositionOf(world, crate).x - 1.5f;
		Require(pushed > 0.5f, "Walking into a light crate should push it along (" + std::to_string(pushed) + " m)");
		Require(PositionOf(world, character).x > 1.0f, "It should walk on behind the crate");
	}

	void TestRaysSeeItOrLeaveItOut()
	{
		GameWorld world;
		Steps steps{ world };
		const Entity floor = AddFloor(world.GetRegistry());
		const Entity character = AddCharacter(world.GetRegistry(), glm::vec3(0.0f));
		// A collider on a character makes no body of its own
		world.GetRegistry().Add<BoxColliderComponent>(character, BoxColliderComponent{});
		steps.Run(5);
		Require(world.GetBodies().GetBodyCount() == 1, "The character's collider shouldn't make a body");

		const Ray down{ glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f) };
		const std::optional<PhysicsPick> hit = PickPhysicsBody(world, down, 10.0f);
		Require(hit && hit->HitEntity == character, "A ray down onto the character should hit it, and know its entity");
		Require(hit->Point.y > 1.6f && hit->Point.y < 1.8f, "It should hit the top of its body, a little inside its capsule");
		Require(world.GetBodies().FindEntity(world.GetPhysics(), world.GetBodies().FindBody(character)) == character,
			"The character's body should be found by its entity, and the entity by the body");

		const QueryFilter ignoring{ .Ignore = world.GetBodies().FindBody(character) };
		const std::optional<PhysicsPick> through = PickPhysicsBody(world, down, 10.0f, ignoring);
		Require(through && through->HitEntity == floor && std::abs(through->Point.y) < 1e-3f, "A ray that leaves the character out should hit the floor");
	}

	void TestLiveEditsAndShapeChanges()
	{
		GameWorld world;
		Steps steps{ world };
		Registry& registry = world.GetRegistry();
		AddFloor(registry);
		const Entity character = AddCharacter(registry, glm::vec3(0.0f));
		steps.Run(10);
		const CharacterHandle first = world.GetBodies().FindCharacter(character);

		// Put somewhere else, as an editor's live edit does: there at once, drawn there
		registry.Get<TransformComponent>(character).Position = glm::vec3(5.0f, 0.0f, 0.0f);
		steps.Run(1);
		Require(glm::length(PositionOf(world, character) - glm::vec3(5.0f, 0.0f, 0.0f)) < 0.01f, "A set position should put the character there");
		Require(world.GetBodies().FindCharacter(character) == first, "Moving it shouldn't make it again");
		Require(!registry.Has<PreviousTransformComponent>(character), "Put there and standing, it shouldn't be drawn moving");

		registry.Get<CharacterComponent>(character).Height = 1.0f;
		steps.Run(1);
		const CharacterHandle remade = world.GetBodies().FindCharacter(character);
		Require(remade.IsValid() && !(remade == first) && world.GetPhysics().GetCharacterCount() == 1, "A changed shape should make it again");
		Require(glm::length(PositionOf(world, character) - glm::vec3(5.0f, 0.0f, 0.0f)) < 0.01f, "It should be made again where it stands");
		const Ray down{ glm::vec3(5.0f, 5.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f) };
		const std::optional<PhysicsPick> hit = PickPhysicsBody(world, down, 10.0f);
		Require(hit && hit->HitEntity == character && hit->Point.y < 1.0f, "Its body should have the new height");

		registry.Remove<CharacterComponent>(character);
		steps.Run(1);
		Require(world.GetPhysics().GetCharacterCount() == 0 && world.GetPhysics().GetBodyCount() == 1,
			"Without its component, the character and its body should go");

		const Entity other = AddCharacter(registry, glm::vec3(0.0f));
		steps.Run(1);
		registry.DestroyEntity(other);
		steps.Run(1);
		Require(world.GetPhysics().GetCharacterCount() == 0, "A destroyed entity's character should go");
	}

	void TestItIsDrawnBetweenSteps()
	{
		GameWorld world;
		Steps steps{ world };
		Registry& registry = world.GetRegistry();
		AddFloor(registry);
		const Entity character = AddCharacter(registry, glm::vec3(0.0f), CharacterComponent{ .Acceleration = 10000.0f });
		steps.Run(10);
		Require(!registry.Has<PreviousTransformComponent>(character), "Standing, it shouldn't keep a previous pose");

		CharacterOf(world, character).DesiredMove = glm::vec3(1.0f, 0.0f, 0.0f);
		steps.Run(5);
		const glm::vec3 before = PositionOf(world, character);
		steps.Run(1);
		Require(registry.Has<PreviousTransformComponent>(character), "Walking, it should keep where it was before the step");
		const PreviousTransformComponent& previous = registry.Get<PreviousTransformComponent>(character);
		const TransformComponent& transform = registry.Get<TransformComponent>(character);
		Require(previous.Position == before && previous.StepPosition == transform.Position, "Its previous pose should be the step's start and end");
		const glm::vec3 halfway = InterpolateTransform(previous, transform, 0.5f).Position;
		Require(glm::length(halfway - 0.5f * (before + transform.Position)) < 1e-5f, "Halfway into the next step, it should be drawn halfway");
	}

	void TestTheDebugViewShowsItsCapsule()
	{
		GameWorld world;
		Steps steps{ world };
		AddFloor(world.GetRegistry());
		AddCharacter(world.GetRegistry(), glm::vec3(0.0f));
		const PhysicsDebugOptions options;
		world.SetPhysicsDebugView(options);
		steps.Run(1);

		std::vector<DebugLine> capsule;
		std::copy_if(world.GetDebugDraw().GetLines().begin(), world.GetDebugDraw().GetLines().end(), std::back_inserter(capsule), [&](const DebugLine& line)
			{
				return line.Color == options.CharacterColor;
			});
		Require(capsule.size() == 100, "The character should be drawn as a capsule, in its own colour (" + std::to_string(capsule.size()) + " lines)");
		float lowest = 10.0f;
		float highest = -10.0f;
		for (const DebugLine& line : capsule)
		{
			lowest = std::min({ lowest, line.From.y, line.To.y });
			highest = std::max({ highest, line.From.y, line.To.y });
		}
		Require(lowest > -0.05f && lowest < 0.05f && std::abs(highest - lowest - 1.8f) < 0.01f, "The capsule should stand on its feet, 1.8 m tall");
	}

	// A walk around crates and a jump, step by step, from a new world: the character's and the crates'
	// positions after every step
	std::vector<glm::vec3> WalkAmongCrates()
	{
		GameWorld world;
		Steps steps{ world };
		Registry& registry = world.GetRegistry();
		AddFloor(registry);
		AddBlock(registry, 3.0f, 5.0f, 0.2f);
		std::vector<Entity> crates;
		for (int i = 0; i < 4; ++i)
		{
			const Entity crate = registry.CreateEntity();
			registry.Add<TransformComponent>(crate, TransformComponent{ .Position = glm::vec3(1.0f + 0.6f * i, 0.25f + 0.1f * i, 0.4f * (i % 2)) });
			registry.Add<BoxColliderComponent>(crate, BoxColliderComponent{ .HalfExtents = glm::vec3(0.25f) });
			registry.Add<RigidBodyComponent>(crate, RigidBodyComponent{ .Mass = 5.0f });
			crates.push_back(crate);
		}
		const Entity character = AddCharacter(registry, glm::vec3(0.0f, 0.5f, 0.0f));

		std::vector<glm::vec3> path;
		for (int step = 0; step < 240; ++step)
		{
			CharacterComponent& walker = registry.Get<CharacterComponent>(character);
			const float angle = 0.01f * static_cast<float>(step);
			walker.DesiredMove = glm::vec3(std::cos(angle), 0.0f, std::sin(angle));
			walker.bJumpRequested = step == 100;
			steps.Run(1);
			path.push_back(PositionOf(world, character));
			for (const Entity crate : crates)
			{
				path.push_back(PositionOf(world, crate));
			}
		}
		return path;
	}

	void TestTheSameInputsGiveTheSamePath()
	{
		const std::vector<glm::vec3> first = WalkAmongCrates();
		const std::vector<glm::vec3> second = WalkAmongCrates();
		Require(first.size() == second.size(), "Both walks should be as long");
		for (size_t i = 0; i < first.size(); ++i)
		{
			Require(first[i] == second[i], "The same inputs should give exactly the same path, step " + std::to_string(i / 5));
		}
		Require(glm::length(first[first.size() - 5] - first[0]) > 3.0f, "The walk should have gone somewhere");
	}

	const std::filesystem::path SampleScenePath = "Scenes/CharacterTest.nyxscene";

	// A part of the sample scene: a cube or the capsule, `size` its scale, seen through a material
	Entity AddSamplePart(Registry& world, const std::string& name, const glm::vec3& position, const glm::quat& rotation,
		const glm::vec3& size, const std::string& mesh, const std::string& material)
	{
		const Entity entity = world.CreateEntity();
		world.Add<NameComponent>(entity, NameComponent{ name });
		world.Add<GuidComponent>(entity, GuidComponent{ EntityGuid::Generate() });
		world.Add<TransformComponent>(entity, TransformComponent{ .Position = position, .Rotation = rotation, .Scale = size });
		world.Add<MeshRendererComponent>(entity, MeshRendererComponent{ .Mesh = AssetReference{ .Type = "Mesh", .Path = mesh }, .Material = AssetReference{ .Type = "Material", .Path = material }, .bVisible = true });
		return entity;
	}

	// A static box of `size`, the cube mesh scaled to it
	Entity AddSampleBlock(Registry& world, const std::string& name, const glm::vec3& position, const glm::quat& rotation,
		const glm::vec3& size, const std::string& material)
	{
		const Entity block = AddSamplePart(world, name, position, rotation, size, "Meshes/Cube.nyxmesh", material);
		world.Add<BoxColliderComponent>(block, BoxColliderComponent{ .HalfExtents = glm::vec3(0.5f) });
		return block;
	}

	// A plank 3 m wide and 6 m long tilted `degrees`, rising away from the camera (-Z), its low
	// end's top on the floor at z = 1
	void AddSampleRamp(Registry& world, const std::string& name, float x, float degrees, const std::string& material)
	{
		const float angle = glm::radians(degrees);
		const glm::quat rotation = glm::angleAxis(angle, glm::vec3(1.0f, 0.0f, 0.0f));
		const glm::vec3 topCenter(x, 3.0f * std::sin(angle), 1.0f - 3.0f * std::cos(angle));
		AddSampleBlock(world, name, topCenter - rotation * glm::vec3(0.0f, 0.1f, 0.0f), rotation, glm::vec3(3.0f, 0.2f, 6.0f), material);
	}

	// The scene to try the character in by hand: a floor, a ramp it walks up and one it slides down,
	// a step it climbs and one it doesn't, crates to push, the character and a camera behind it
	void WriteSampleScene()
	{
		const std::filesystem::path assets = Nyx::Paths::GetAssetsDir();
		std::filesystem::create_directories(assets / "Materials/CharacterTest");
		struct SampleMaterial
		{
			const char* Path;
			const char* What;
			const char* Values;
		};
		const SampleMaterial materials[] = {
			{ "Materials/CharacterTest/Character.nyxmat", "character, warm orange plastic", "BaseColor = 1 0.45 0.1\nRoughness = 0.4\n" },
			{ "Materials/CharacterTest/Crate.nyxmat", "crates, matte brown", "BaseColor = 0.55 0.36 0.2\nRoughness = 0.8\n" },
			{ "Materials/CharacterTest/TooSteep.nyxmat", "ground the character can't walk up, matte red", "BaseColor = 0.85 0.2 0.15\nRoughness = 0.7\n" },
		};
		for (const SampleMaterial& material : materials)
		{
			std::ofstream file(assets / material.Path, std::ios::binary | std::ios::trunc);
			file << "# The character test scene's " << material.What << " (NyxCharacterTests --write-scene)\nShader = Shaders/Standard.slang\n"
				 << material.Values;
		}

		Registry world;
		const glm::quat upright(1.0f, 0.0f, 0.0f, 0.0f);
		AddSampleBlock(world, "Floor", glm::vec3(0.0f, -0.5f, 0.0f), upright, glm::vec3(30.0f, 1.0f, 30.0f), "Materials/Textured.nyxmat");
		AddSampleRamp(world, "Ramp 25 Degrees", -6.0f, 25.0f, "Materials/Untextured.nyxmat");
		AddSampleRamp(world, "Ramp 50 Degrees", 6.0f, 50.0f, "Materials/CharacterTest/TooSteep.nyxmat");
		AddSampleBlock(world, "Step 0.2 m", glm::vec3(-2.5f, 0.1f, -5.0f), upright, glm::vec3(2.0f, 0.2f, 2.0f), "Materials/Untextured.nyxmat");
		AddSampleBlock(world, "Step 0.5 m", glm::vec3(2.5f, 0.25f, -5.0f), upright, glm::vec3(2.0f, 0.5f, 2.0f), "Materials/CharacterTest/TooSteep.nyxmat");

		const glm::vec3 crates[] = { { -0.8f, 0.3f, -1.5f }, { 0.0f, 0.3f, -1.5f }, { 0.8f, 0.3f, -1.5f }, { 0.0f, 0.9f, -1.5f } };
		for (size_t i = 0; i < std::size(crates); ++i)
		{
			const Entity crate = AddSampleBlock(world, "Crate " + std::to_string(i + 1), crates[i], upright, glm::vec3(0.6f), "Materials/CharacterTest/Crate.nyxmat");
			world.Add<RigidBodyComponent>(crate, RigidBodyComponent{ .Mass = 10.0f });
		}

		const Entity character = AddSamplePart(world, "Character", glm::vec3(0.0f, 0.0f, 3.0f), upright, glm::vec3(1.0f), "Meshes/Capsule.nyxmesh",
			"Materials/CharacterTest/Character.nyxmat");
		world.Add<CharacterComponent>(character, CharacterComponent{});

		const Entity camera = world.CreateEntity();
		world.Add<NameComponent>(camera, NameComponent{ "Camera" });
		world.Add<GuidComponent>(camera, GuidComponent{ EntityGuid::Generate() });
		world.Add<TransformComponent>(camera,
			TransformComponent{ .Position = glm::vec3(0.0f, 2.4f, 8.0f), .Rotation = glm::angleAxis(glm::radians(-12.0f), glm::vec3(1.0f, 0.0f, 0.0f)) });
		world.Add<CameraComponent>(camera, CameraComponent{});

		const Entity sun = world.CreateEntity();
		world.Add<NameComponent>(sun, NameComponent{ "Sun" });
		world.Add<GuidComponent>(sun, GuidComponent{ EntityGuid::Generate() });
		world.Add<TransformComponent>(sun, TransformComponent{ .Position = glm::vec3(0.0f, 5.0f, 0.0f), .Rotation = glm::quat(glm::radians(glm::vec3(-50.0f, 30.0f, 0.0f))) });
		world.Add<DirectionalLightComponent>(sun, DirectionalLightComponent{});

		Require(SceneSerializer::SaveToFile(world, assets / SampleScenePath), "The sample scene can't be saved");
		std::cout << "Wrote " << (assets / SampleScenePath).string() << " and its materials\n";
	}

	void TestTheSampleSceneStands()
	{
		GameWorld world;
		Steps steps{ world };
		Registry& registry = world.GetRegistry();
		Require(SceneSerializer::LoadFromFile(Nyx::Paths::GetAssetsDir() / SampleScenePath, registry),
			"The sample scene can't be loaded; NyxCharacterTests --write-scene writes it");
		steps.Run(120);

		int characters = 0;
		registry.Each<CharacterComponent>([&](Entity entity, CharacterComponent& character)
			{
				++characters;
				const glm::vec3 position = registry.Get<TransformComponent>(entity).Position;
				Require(character.bOnGround && std::abs(position.y) < 0.03f, "The sample scene's character should stand on its floor " + Describe(position));
			});
		Require(characters == 1, "The sample scene should have a character");
		int crates = 0;
		registry.Each<RigidBodyComponent>([&](Entity entity, RigidBodyComponent&)
			{
				++crates;
				Require(!registry.Has<PreviousTransformComponent>(entity), "The sample scene's crates should be at rest");
			});
		Require(crates == 4, "The sample scene should have its crates");
	}

	void TestScenesKeepCharacters(const std::filesystem::path& directory)
	{
		Nyx::RegisterComponentTypes();
		Registry world;
		const Entity entity = world.CreateEntity();
		world.Add<TransformComponent>(entity, TransformComponent{});
		world.Add<CharacterComponent>(entity, CharacterComponent{ .Radius = 0.4f, .StepHeight = 0.2f, .AirControl = 0.5f, .JumpSpeed = 6.0f });
		world.Get<CharacterComponent>(entity).DesiredMove = glm::vec3(1.0f, 0.0f, 0.0f);

		const std::filesystem::path path = directory / "Character.nyxscene";
		Require(SceneSerializer::SaveToFile(world, path), "Saving a scene with a character failed");
		Registry loaded;
		Require(SceneSerializer::LoadFromFile(path, loaded), "Loading a scene with a character failed");

		int characters = 0;
		loaded.Each<CharacterComponent>([&](Entity, CharacterComponent& character)
			{
				++characters;
				Require(character.Radius == 0.4f && character.StepHeight == 0.2f && character.AirControl == 0.5f && character.JumpSpeed == 6.0f,
					"The character's values should be kept");
				Require(character.DesiredMove == glm::vec3(0.0f), "What it wants while the game runs shouldn't be saved");
			});
		Require(characters == 1, "The character should be saved and loaded");
	}
}

int main(int argc, char** argv)
{
	Nyx::Logger::Get().Init();

	try
	{
		if (argc > 1 && std::strcmp(argv[1], "--write-scene") == 0)
		{
			Nyx::RegisterComponentTypes();
			WriteSampleScene();
			return 0;
		}
		Nyx::Tests::TestDirectory directory("NyxCharacterTests");
		TestItFallsAndLandsOnTheFloor();
		TestItWalksAtItsSpeed();
		TestItClimbsStepsUpToItsStepHeight();
		TestItSlidesDownGroundTooSteep();
		TestItJumpsAsHighAsItsSpeedSays();
		TestItPushesALightCrate();
		TestRaysSeeItOrLeaveItOut();
		TestLiveEditsAndShapeChanges();
		TestItIsDrawnBetweenSteps();
		if constexpr (DebugDraw::Enabled)
		{
			TestTheDebugViewShowsItsCapsule();
		}
		TestTheSameInputsGiveTheSamePath();
		TestScenesKeepCharacters(directory.Path);
		TestTheSampleSceneStands();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Character test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All character tests passed.\n";
	return 0;
}
