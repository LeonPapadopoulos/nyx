// An SDF terrain's collision in the game world (TerrainCollision): the way out of the terrain from
// a point inside it (FindTerrainFreePoint()); a ball dropped on the hills comes to rest on their
// surface; a character walks across them on the ground; one falling past a floating landmass's cliff
// at 60 m/s, stepped as a game steps, lands on the hills below without ever missing collision; the collision is the chunks'
// triangles as drawn, those drawn shared; bodies follow the movers, are made again for new values
// and go with the terrain or its collision; edits by command take the ground from a crate at rest,
// which falls, and push a character out of a heap made on it, and are drawn and in collision in the
// same frame; the sample scene's balls come to rest on its hills.
#include "CharacterComponent.h"
#include "ComponentRegistration.h"
#include "GameWorld.h"
#include "GuidComponent.h"
#include "JobSystem.h"
#include "Log.h"
#include "Paths.h"
#include "PhysicsComponents.h"
#include "SceneSerializer.h"
#include "SdfTerrain.h"
#include "SdfTerrainComponent.h"
#include "SdfTerrainEditCommand.h"
#include "TerrainChunks.h"
#include "TestSupport.h"
#include "TransformComponent.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
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

		// As a game steps: no sooner than the clock, so the workers have the time they would; yielding
		// rather than sleeping, which the system may round up to twice a step
		void RunInTime(int count)
		{
			for (int i = 0; i < count; ++i)
			{
				const auto due = std::chrono::steady_clock::now() + std::chrono::microseconds(int64_t(StepSeconds * 1e6f));
				Run(1);
				while (std::chrono::steady_clock::now() < due)
				{
					std::this_thread::yield();
				}
			}
		}
	};

	std::string Describe(const glm::vec3& v)
	{
		return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
	}

	// Terraced hills with tunnels under them
	SdfTerrainComponent MakeHills()
	{
		SdfTerrainComponent terrain;
		terrain.Seed = 4;
		terrain.CollisionDistance = 24.0f;
		terrain.Layers[0].Amplitude = 6.0f;
		terrain.Layers[0].Frequency = 0.015f;
		SdfTerrainLayer tunnels;
		tunnels.Kind = ESdfTerrainLayerKind::Tunnels;
		tunnels.Combine = ESdfTerrainCombine::Carve;
		tunnels.Height = -8.0f;
		tunnels.Depth = 30.0f;
		tunnels.Radius = 5.0f;
		tunnels.Frequency = 0.03f;
		tunnels.Octaves = 2;
		terrain.Layers.push_back(tunnels);
		return terrain;
	}

	// The hills with a floating landmass of sharp stone above them
	SdfTerrainComponent MakeHillsAndLandmass()
	{
		SdfTerrainComponent terrain = MakeHills();
		terrain.Layers.resize(1);
		SdfTerrainLayer island;
		island.Kind = ESdfTerrainLayerKind::Landmass;
		island.Height = 120.0f;
		island.Depth = 25.0f;
		island.Amplitude = 3.0f;
		island.Center = glm::vec2(0.0f, 0.0f);
		island.Radius = 40.0f;
		island.UndersideDepth = 30.0f;
		island.EdgeRoughness = 6.0f;
		island.Frequency = 0.02f;
		island.Material = 2;
		terrain.Layers.push_back(island);
		terrain.SharpMaterials = { 2 };
		return terrain;
	}

	Entity AddTerrain(Registry& world, const SdfTerrainComponent& terrain)
	{
		const Entity entity = world.CreateEntity();
		world.Add<TransformComponent>(entity, TransformComponent{});
		world.Add<SdfTerrainComponent>(entity, terrain);
		return entity;
	}

	// The surface's height under a point, by a ray down the field
	float SurfaceBelow(const SdfTerrainComponent& terrain, const glm::vec3& point)
	{
		const std::optional<TerrainRayHit> hit = RayCastTerrain(terrain, point, glm::vec3(0.0f, -1.0f, 0.0f), 500.0f);
		Require(hit.has_value(), "There should be ground below " + Describe(point));
		return hit->Point.y;
	}

	const TerrainCollisionStats& StatsOf(GameWorld& world, Entity terrain)
	{
		return world.GetRegistry().Get<TerrainCollisionComponent>(terrain).Stats;
	}

	// Steps until every wanted chunk has its body and nothing is being made, at most 30 s
	void StepUntilCovered(GameWorld& world, Steps& steps, Entity terrain)
	{
		const auto start = std::chrono::steady_clock::now();
		for (;;)
		{
			steps.Run(1);
			const TerrainCollisionStats& stats = StatsOf(world, terrain);
			if (stats.Pending == 0 && stats.Resident >= stats.Wanted)
			{
				return;
			}
			Require(std::chrono::steady_clock::now() - start < std::chrono::seconds(30), "The collision should be made within 30 s");
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}

	// The way out: a point in the air is free already; one 3 m inside a hill comes out above the
	// surface, a ball of the clearance round it clear, about as far as it was in
	void TestTheWayOut()
	{
		const SdfTerrainComponent terrain = MakeHills();
		const float surface = SurfaceBelow(terrain, glm::vec3(10.0f, 50.0f, 5.0f));
		const glm::vec3 air(10.0f, surface + 3.0f, 5.0f);
		Require(FindTerrainFreePoint(terrain, air, 0.5f, 10.0f) == air, "A point in the air should be free where it is");

		const glm::vec3 inside(10.0f, surface - 3.0f, 5.0f);
		const std::optional<glm::vec3> out = FindTerrainFreePoint(terrain, inside, 0.5f, 10.0f);
		Require(out.has_value(), "A point 3 m inside a hill should find its way out");
		Require(SampleTerrainValue(terrain, *out) >= 0.0f && SampleTerrainValue(terrain, *out - glm::vec3(0.0f, 0.5f, 0.0f)) >= 0.0f,
			"The way out should be outside, the clearance under it too");
		Require(glm::length(*out - inside) < 3.0f + 0.5f + 1.0f, "The way out should be near, not " + std::to_string(glm::length(*out - inside)) + " m");
		Require(!FindTerrainFreePoint(terrain, glm::vec3(10.0f, surface - 30.0f, 5.0f), 0.5f, 2.0f).has_value(),
			"Deep inside, a short search should find nothing");
		std::cout << "Out of a hill: " << glm::length(*out - inside) << " m from 3 m inside\n";
	}

	// A ball dropped on the hills rests on their surface
	void TestABallRestsOnTheHills()
	{
		GameWorld world;
		Steps steps{ world };
		Registry& registry = world.GetRegistry();
		const Entity terrain = AddTerrain(registry, MakeHills());
		const SdfTerrainComponent& values = registry.Get<SdfTerrainComponent>(terrain);
		const glm::vec3 above(-6.0f, 0.0f, 9.0f);
		const float surface = SurfaceBelow(values, above + glm::vec3(0.0f, 50.0f, 0.0f));

		const Entity ball = registry.CreateEntity();
		registry.Add<TransformComponent>(ball, TransformComponent{ .Position = glm::vec3(above.x, surface + 6.0f, above.z) });
		registry.Add<SphereColliderComponent>(ball, SphereColliderComponent{ .Radius = 0.5f });
		registry.Add<RigidBodyComponent>(ball, RigidBodyComponent{ .GravityFactor = 0.0f });
		steps.Run(1);
		StepUntilCovered(world, steps, terrain);
		Require(world.GetTerrainCollision().GetBodyCount() > 0, "The chunks round the ball should have bodies");

		registry.Get<RigidBodyComponent>(ball).GravityFactor = 1.0f;
		steps.Run(240);
		const glm::vec3 position = registry.Get<TransformComponent>(ball).Position;
		const float below = SurfaceBelow(values, position + glm::vec3(0.0f, 5.0f, 0.0f));
		Require(std::abs(position.y - 0.5f - below) < 0.15f, "The ball should rest on the surface: centre " + Describe(position) + ", surface " + std::to_string(below));
		Require(StatsOf(world, terrain).Uncovered == 0, "The ball should never be without collision");
		std::cout << "A ball at rest " << position.y - 0.5f - below << " m over the surface, " << StatsOf(world, terrain).Resident << " chunks with bodies\n";
	}

	// A character walks 60 m across the hills on the ground, its feet never in them
	void TestACharacterWalksOnTheHills()
	{
		GameWorld world;
		Steps steps{ world };
		Registry& registry = world.GetRegistry();
		const Entity terrain = AddTerrain(registry, MakeHills());
		const SdfTerrainComponent& values = registry.Get<SdfTerrainComponent>(terrain);
		const glm::vec3 start(0.0f, SurfaceBelow(values, glm::vec3(0.0f, 50.0f, 0.0f)) + 0.2f, 0.0f);
		const Entity walker = registry.CreateEntity();
		registry.Add<TransformComponent>(walker, TransformComponent{ .Position = start });
		registry.Add<CharacterComponent>(walker, CharacterComponent{ .GravityFactor = 0.0f });
		steps.Run(1);
		StepUntilCovered(world, steps, terrain);

		registry.Get<CharacterComponent>(walker).GravityFactor = 1.0f;
		steps.Run(30);
		registry.Get<CharacterComponent>(walker).DesiredMove = glm::vec3(1.0f, 0.0f, 0.0f);
		int onGround = 0;
		int frames = 0;
		float deepest = 0.0f;
		while (registry.Get<TransformComponent>(walker).Position.x < 60.0f && frames < 60 * 30)
		{
			steps.RunInTime(1);
			++frames;
			const glm::vec3 feet = registry.Get<TransformComponent>(walker).Position;
			onGround += registry.Get<CharacterComponent>(walker).bOnGround ? 1 : 0;
			deepest = std::min(deepest, feet.y - SurfaceBelow(values, feet + glm::vec3(0.0f, 3.0f, 0.0f)));
			Require(StatsOf(world, terrain).Uncovered == 0, "The walker should never be without collision");
		}
		Require(registry.Get<TransformComponent>(walker).Position.x >= 60.0f, "The walker should cover 60 m");
		Require(onGround > frames * 9 / 10, "The walker should be on the ground nearly all the way, not " + std::to_string(onGround) + " of " + std::to_string(frames));
		Require(deepest > -0.2f, "The walker's feet should never be in the hills, not " + std::to_string(deepest) + " m");
		std::cout << "Walked 60 m in " << frames << " steps, on the ground for " << onGround << ", feet at worst " << deepest << " m in\n";
	}

	// A character falling from far above past the landmass's cliff, 60 m/s at the bottom, lands on the
	// hills below; collision is always ahead of it
	void TestAFastFallPastTheCliff()
	{
		GameWorld world;
		Steps steps{ world };
		Registry& registry = world.GetRegistry();
		const Entity terrain = AddTerrain(registry, MakeHillsAndLandmass());
		const SdfTerrainComponent& values = registry.Get<SdfTerrainComponent>(terrain);
		// Just past the landmass's edge: the first point out from its middle with nothing above the hills
		glm::vec3 column(30.0f, 0.0f, 0.0f);
		while (SurfaceBelow(values, glm::vec3(column.x, 400.0f, column.z)) > 40.0f)
		{
			column.x += 1.0f;
		}
		column.x += 1.5f;
		const float ground = SurfaceBelow(values, glm::vec3(column.x, 400.0f, column.z));
		const Entity faller = registry.CreateEntity();
		registry.Add<TransformComponent>(faller, TransformComponent{ .Position = glm::vec3(column.x, ground + 200.0f, column.z) });
		registry.Add<CharacterComponent>(faller, CharacterComponent{ .GravityFactor = 0.0f });
		steps.Run(1);
		StepUntilCovered(world, steps, terrain);
		registry.Get<CharacterComponent>(faller).GravityFactor = 1.0f;

		float fastest = 0.0f;
		int frames = 0;
		uint32_t worstOutrunning = 0;
		bool bPassedTheCliff = false;
		while (!registry.Get<CharacterComponent>(faller).bOnGround && frames < 60 * 20)
		{
			steps.RunInTime(1);
			++frames;
			const glm::vec3 feet = registry.Get<TransformComponent>(faller).Position;
			fastest = std::max(fastest, -registry.Get<CharacterComponent>(faller).Velocity.y);
			bPassedTheCliff = bPassedTheCliff || (feet.y < 100.0f && feet.y > 80.0f);
			const TerrainCollisionStats& stats = StatsOf(world, terrain);
			Require(stats.Uncovered == 0, "Falling, the character should never be without collision, at " + Describe(feet) + " after " + std::to_string(frames) +
				" steps; " + std::to_string(stats.Resident) + " of " + std::to_string(stats.Wanted) + " chunks with bodies, " + std::to_string(stats.Pending) +
				" being made, the worst " + std::to_string(stats.WorstSeconds) + " s");
			worstOutrunning = std::max(worstOutrunning, stats.Outrunning);
			Require(SampleTerrainValue(values, feet + glm::vec3(0.0f, 0.3f, 0.0f)) > 0.0f, "Falling, the character should never be in the terrain, at " + Describe(feet));
		}
		const glm::vec3 feet = registry.Get<TransformComponent>(faller).Position;
		Require(registry.Get<CharacterComponent>(faller).bOnGround, "The character should land");
		Require(bPassedTheCliff, "The character should fall past the landmass");
		Require(std::abs(feet.y - ground) < 0.3f, "It should land on the hills: feet " + Describe(feet) + ", ground " + std::to_string(ground));
		Require(fastest > 55.0f, "It should be falling at about 60 m/s, not " + std::to_string(fastest));
		Require(worstOutrunning == 0, "The collision should keep ahead of it");
		std::cout << "Fell 200 m past the cliff at up to " << fastest << " m/s, landed " << feet.y - ground << " m over the hills; the worst chunk took "
				  << StatsOf(world, terrain).WorstSeconds * 1000.0 << " ms\n";
	}

	// Where a ray first meets a mesh's triangles, from either side
	std::optional<float> RayMesh(const MeshData& mesh, const glm::vec3& corner, const glm::vec3& origin, const glm::vec3& direction)
	{
		std::optional<float> nearest;
		for (size_t t = 0; t + 2 < mesh.Indices.size(); t += 3)
		{
			const glm::vec3 a = mesh.Vertices[mesh.Indices[t]].Position + corner;
			const glm::vec3 b = mesh.Vertices[mesh.Indices[t + 1]].Position + corner;
			const glm::vec3 c = mesh.Vertices[mesh.Indices[t + 2]].Position + corner;
			const glm::vec3 ab = b - a;
			const glm::vec3 ac = c - a;
			const glm::vec3 p = glm::cross(direction, ac);
			const float determinant = glm::dot(ab, p);
			if (std::abs(determinant) < 1e-12f)
			{
				continue;
			}
			const glm::vec3 s = origin - a;
			const float u = glm::dot(s, p) / determinant;
			const glm::vec3 q = glm::cross(s, ab);
			const float v = glm::dot(direction, q) / determinant;
			const float distance = glm::dot(ac, q) / determinant;
			if (u >= 0.0f && v >= 0.0f && u + v <= 1.0f && distance >= 0.0f && (!nearest || distance < *nearest))
			{
				nearest = distance;
			}
		}
		return nearest;
	}

	// The collision is the drawn chunks' triangles: rays down meet the bodies where they meet the
	// meshes drawn, and the drawn meshes at the finest level are shared, not made again
	void TestTheSameTrianglesAsDrawn()
	{
		GameWorld world;
		Steps steps{ world };
		Registry& registry = world.GetRegistry();
		SdfTerrainComponent values = MakeHills();
		values.ViewDistance = 128.0f;
		values.CollisionDistance = 40.0f;
		const Entity terrain = AddTerrain(registry, values);
		const glm::vec3 viewpoint(0.0f, 10.0f, 0.0f);
		const auto start = std::chrono::steady_clock::now();
		do
		{
			UpdateTerrainChunks(registry, { &viewpoint, 1 }, JobSystem::Get());
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			Require(std::chrono::steady_clock::now() - start < std::chrono::seconds(30), "The chunks should be drawn within 30 s");
		} while (!registry.Get<TerrainChunksComponent>(terrain).Stats.bSettled || registry.Get<TerrainChunksComponent>(terrain).Stats.Pending > 0);

		const Entity ball = registry.CreateEntity();
		registry.Add<TransformComponent>(ball, TransformComponent{ .Position = glm::vec3(3.0f, 8.0f, -4.0f) });
		registry.Add<SphereColliderComponent>(ball, SphereColliderComponent{});
		registry.Add<RigidBodyComponent>(ball, RigidBodyComponent{ .GravityFactor = 0.0f });
		steps.Run(1);
		StepUntilCovered(world, steps, terrain);
		const TerrainCollisionStats& stats = StatsOf(world, terrain);
		Require(stats.Shared > 0, "The chunks drawn at the finest level should be shared with the collision");

		const TerrainChunksComponent& chunks = registry.Get<TerrainChunksComponent>(terrain);
		QueryFilter filter;
		filter.bMoving = false;
		int compared = 0;
		float worst = 0.0f;
		for (int n = 0; n < 200; ++n)
		{
			// Off the half cells, where vertices of flat ground are: a ray through a vertex may slip
			// between its triangles
			const glm::vec3 origin(-20.0f + float(n % 20) * 2.1f + 0.137f, 40.0f, -20.0f + float(n / 20) * 4.3f + 0.291f);
			const std::optional<QueryHit> hit = world.GetPhysics().RayCast(origin, glm::vec3(0.0f, -1.0f, 0.0f), 100.0f, filter);
			std::optional<float> drawn;
			for (const TerrainChunk& chunk : chunks.Chunks)
			{
				if (chunk.Level == 0 && chunk.Mesh.Mesh)
				{
					const std::optional<float> distance = RayMesh(*chunk.Mesh.Mesh, chunk.Origin, origin, glm::vec3(0.0f, -1.0f, 0.0f));
					if (distance && (!drawn || *distance < *drawn))
					{
						drawn = distance;
					}
				}
			}
			if (!hit || !drawn)
			{
				continue;
			}
			Require(world.GetTerrainCollision().FindEntity(hit->Body) == terrain, "A ray should hit the terrain's body");
			if (std::abs(hit->Distance - *drawn) > 1e-3f && worst <= 1e-3f)
			{
				std::cout << "  ray at " << Describe(origin) << ": the collision at " << hit->Distance << " m, the drawn triangles at " << *drawn << " m\n";
			}
			worst = std::max(worst, std::abs(hit->Distance - *drawn));
			++compared;
		}
		Require(compared > 100, "Most rays should meet the terrain, not " + std::to_string(compared));
		Require(worst < 1e-3f, "Rays should meet the collision where they meet the drawn triangles, not " + std::to_string(worst) + " m off");
		std::cout << "The collision as drawn: " << compared << " rays, at worst " << worst << " m apart; " << stats.Shared << " of " << stats.Made << " chunks shared\n";
	}

	// Bodies follow the movers: moved far, the old chunks' bodies go and new ones come; new values
	// make them again; without collision, or without the terrain, none
	void TestBodiesComeAndGo()
	{
		GameWorld world;
		Steps steps{ world };
		Registry& registry = world.GetRegistry();
		const Entity terrain = AddTerrain(registry, MakeHills());
		const Entity ball = registry.CreateEntity();
		registry.Add<TransformComponent>(ball, TransformComponent{ .Position = glm::vec3(0.0f, 20.0f, 0.0f) });
		registry.Add<SphereColliderComponent>(ball, SphereColliderComponent{});
		registry.Add<RigidBodyComponent>(ball, RigidBodyComponent{ .GravityFactor = 0.0f });
		steps.Run(1);
		StepUntilCovered(world, steps, terrain);
		const size_t here = world.GetTerrainCollision().GetBodyCount();
		Require(here > 0, "The ball should have chunks round it");

		registry.Get<TransformComponent>(ball).Position = glm::vec3(500.0f, 20.0f, 0.0f);
		steps.Run(1);
		StepUntilCovered(world, steps, terrain);
		const float reach = registry.Get<SdfTerrainComponent>(terrain).CollisionDistance + TerrainChunkSize;
		QueryFilter filter;
		filter.bMoving = false;
		Require(!world.GetPhysics().RayCast(glm::vec3(0.0f, 40.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 100.0f, filter).has_value(),
			"The chunks left behind should have no bodies");
		Require(world.GetPhysics().RayCast(glm::vec3(500.0f, 40.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 100.0f, filter).has_value(),
			"The chunks where the ball is should have bodies");
		Require(world.GetTerrainCollision().GetBodyCount() <= size_t(std::pow(2.0f * reach / TerrainChunkSize + 1.0f, 3.0f)), "Only chunks near the ball should have bodies");

		const uint64_t made = StatsOf(world, terrain).Made;
		registry.Get<SdfTerrainComponent>(terrain).Seed = 99;
		steps.Run(1);
		Require(world.GetTerrainCollision().GetBodyCount() > 0, "The old bodies should stay while new ones are made");
		StepUntilCovered(world, steps, terrain);
		Require(StatsOf(world, terrain).Made > made, "New values should make the bodies again");

		registry.Get<SdfTerrainComponent>(terrain).CollisionDistance = 0.0f;
		steps.Run(1);
		Require(world.GetTerrainCollision().GetBodyCount() == 0 && !registry.Has<TerrainCollisionComponent>(terrain), "Without collision, no bodies");
		registry.Get<SdfTerrainComponent>(terrain).CollisionDistance = 24.0f;
		steps.Run(1);
		StepUntilCovered(world, steps, terrain);
		Require(world.GetTerrainCollision().GetBodyCount() > 0, "Collision back, bodies back");
		registry.DestroyEntity(terrain);
		steps.Run(1);
		Require(world.GetTerrainCollision().GetBodyCount() == 0, "With the terrain gone, no bodies");
	}

	// The ground under a point as the collision has it, by a ray down at static bodies
	std::optional<float> CollisionBelow(GameWorld& world, const glm::vec3& point)
	{
		QueryFilter filter;
		filter.bMoving = false;
		const std::optional<QueryHit> hit = world.GetPhysics().RayCast(point, glm::vec3(0.0f, -1.0f, 0.0f), 100.0f, filter);
		return hit ? std::optional<float>(hit->Point.y) : std::nullopt;
	}

	void SendEdit(GameWorld& world, EntityGuid terrain, const SdfTerrainEdit& edit)
	{
		SdfTerrainEditCommand command;
		command.Terrain = terrain;
		command.Edit = edit;
		world.GetCommands().Add(command);
	}

	// Edits by command: a dig under a crate at rest takes its support, which wakes it, and it falls
	// in; a heap made where a character stands puts it inside, and it is pushed out on top; a command
	// for no terrain does nothing
	void TestEditsUnderBodies()
	{
		GameWorld world;
		Steps steps{ world };
		Registry& registry = world.GetRegistry();
		const Entity terrain = AddTerrain(registry, MakeHills());
		const EntityGuid guid{ 0x7E44A1 };
		registry.Add<GuidComponent>(terrain, GuidComponent{ guid });
		const SdfTerrainComponent& values = registry.Get<SdfTerrainComponent>(terrain);

		const glm::vec3 above(-6.0f, 0.0f, 9.0f);
		const Entity crate = registry.CreateEntity();
		registry.Add<TransformComponent>(crate, TransformComponent{ .Position = glm::vec3(above.x, SurfaceBelow(values, above + glm::vec3(0.0f, 50.0f, 0.0f)) + 0.5f, above.z) });
		registry.Add<BoxColliderComponent>(crate, BoxColliderComponent{ .HalfExtents = glm::vec3(0.4f) });
		registry.Add<RigidBodyComponent>(crate, RigidBodyComponent{ .GravityFactor = 0.0f });
		const glm::vec3 stand(12.0f, 0.0f, -4.0f);
		const Entity walker = registry.CreateEntity();
		registry.Add<TransformComponent>(walker, TransformComponent{ .Position = glm::vec3(stand.x, SurfaceBelow(values, stand + glm::vec3(0.0f, 50.0f, 0.0f)) + 0.1f, stand.z) });
		registry.Add<CharacterComponent>(walker, CharacterComponent{ .GravityFactor = 0.0f });
		steps.Run(1);
		StepUntilCovered(world, steps, terrain);
		registry.Get<RigidBodyComponent>(crate).GravityFactor = 1.0f;
		registry.Get<CharacterComponent>(walker).GravityFactor = 1.0f;
		// Its body made again for the gravity: asked for by its entity each time
		auto isAwake = [&] { return world.GetPhysics().IsAwake(world.GetBodies().FindBody(crate)); };
		steps.Run(1);
		for (int step = 0; step < 60 * 10 && isAwake(); ++step)
		{
			steps.Run(1);
		}
		Require(!isAwake(), "The crate should come to rest and sleep");
		const glm::vec3 resting = registry.Get<TransformComponent>(crate).Position;
		const glm::vec3 standing = registry.Get<TransformComponent>(walker).Position;

		// A command for no terrain: nothing
		SdfTerrainEdit dig;
		dig.Position = resting - glm::vec3(0.0f, 0.4f, 0.0f);
		dig.Radius = 2.5f;
		SendEdit(world, EntityGuid{ 0x1234 }, dig);
		steps.Run(1);
		Require(values.Edits.empty(), "A command for no terrain should do nothing");

		// The dig and the heap at once
		SendEdit(world, guid, dig);
		SdfTerrainEdit heap;
		heap.Mode = ESdfTerrainEditMode::Add;
		heap.Position = standing;
		heap.Radius = 1.5f;
		heap.Material = 3;
		SendEdit(world, guid, heap);
		const auto start = std::chrono::steady_clock::now();
		std::vector<TerrainEditEvent> events;
		double liveSeconds = -1.0;
		for (int step = 0; step < 60 * 5; ++step)
		{
			steps.RunInTime(1);
			const TerrainCollisionComponent& collision = registry.Get<TerrainCollisionComponent>(terrain);
			events.insert(events.end(), collision.Events.begin(), collision.Events.end());
			if (liveSeconds < 0.0 && !collision.Events.empty())
			{
				liveSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
			}
		}
		Require(values.Edits.size() == 2 && values.Edits[0].Revision == 1 && values.Edits[1].Revision == 2, "The commands should become the terrain's edits 1 and 2");
		const TerrainEditActivationComponent& activation = registry.Get<TerrainEditActivationComponent>(terrain);
		Require(activation.Live == HashTerrainEdits(values) && activation.Late == 0, "The edits should go live without the deadline, with no drawing to wait for");

		auto eventFor = [&](Entity body, ETerrainEditEvent kind)
		{
			return std::find_if(events.begin(), events.end(), [&](const TerrainEditEvent& event) { return event.Body == body && event.Kind == kind; });
		};
		Require(eventFor(crate, ETerrainEditEvent::LostSupport) != events.end(), "The crate should lose its support to the dig under it");
		const glm::vec3 fallen = registry.Get<TransformComponent>(crate).Position;
		Require(fallen.y < resting.y - 1.0f, "The crate should fall into the dig, not stay at " + Describe(fallen) + " from " + Describe(resting));
		Require(SampleTerrainValue(values, fallen) > 0.0f, "The crate should be in the dig, not in the terrain");

		const auto pushed = eventFor(walker, ETerrainEditEvent::InsideNewTerrain);
		Require(pushed != events.end(), "The character should be pushed out of the heap made on it");
		// On the heap as its collision has it, the chunk's triangles, a little under the field's
		// rounded top
		const glm::vec3 feet = registry.Get<TransformComponent>(walker).Position;
		const std::optional<float> heapTop = CollisionBelow(world, glm::vec3(feet.x, standing.y + 10.0f, feet.z));
		Require(heapTop && *heapTop > standing.y + 1.0f, "The heap should rise over where the character stood");
		Require(feet.y > *heapTop - 0.1f, "The character should be on the heap, not in it: feet at " + Describe(feet) + ", heap top " + std::to_string(*heapTop));
		std::cout << "Edits under bodies: live after " << std::fixed << std::setprecision(1) << liveSeconds * 1000.0 << " ms; the crate fell " << resting.y - fallen.y
				  << " m, the character pushed " << glm::length(pushed->To - pushed->From) << " m onto the heap\n";
	}

	// Drawn and in collision together: with the chunks drawn round the character too, a dig beside it
	// changes the collision and the chunks drawn in the same frame; its time from the command to both
	void TestEditsDrawnWithCollision()
	{
		GameWorld world;
		Steps steps{ world };
		Registry& registry = world.GetRegistry();
		SdfTerrainComponent hills = MakeHills();
		hills.ViewDistance = 300.0f;
		// Together however long the chunks take to make, in Debug too
		hills.ActivationDeadline = 60.0f;
		const Entity terrain = AddTerrain(registry, hills);
		const EntityGuid guid{ 0x7E44A2 };
		registry.Add<GuidComponent>(terrain, GuidComponent{ guid });
		const SdfTerrainComponent& values = registry.Get<SdfTerrainComponent>(terrain);
		const glm::vec3 stand(4.0f, 0.0f, 3.0f);
		const float ground = SurfaceBelow(values, stand + glm::vec3(0.0f, 50.0f, 0.0f));
		const Entity walker = registry.CreateEntity();
		registry.Add<TransformComponent>(walker, TransformComponent{ .Position = glm::vec3(stand.x, ground + 0.1f, stand.z) });
		registry.Add<CharacterComponent>(walker, CharacterComponent{ .GravityFactor = 0.0f });
		const glm::vec3 viewpoint(stand.x, ground + 2.0f, stand.z);
		JobSystem& jobs = JobSystem::Get();
		auto frame = [&]
		{
			UpdateTerrainChunks(registry, { &viewpoint, 1 }, jobs);
			steps.RunInTime(1);
		};
		frame();
		StepUntilCovered(world, steps, terrain);
		for (int n = 0; n < 60 * 30 && !registry.Get<TerrainChunksComponent>(terrain).Stats.bSettled; ++n)
		{
			frame();
		}
		Require(registry.Get<TerrainChunksComponent>(terrain).Stats.bSettled, "The chunks round the character should be drawn");
		const uint64_t before = registry.Get<TerrainChunksComponent>(terrain).Chunks.front().Edits;

		// A dig 3 m to the side of the character, where nothing stands
		const glm::vec3 probe(stand.x + 3.0f, ground + 10.0f, stand.z);
		SdfTerrainEdit dig;
		dig.Position = glm::vec3(probe.x, SurfaceBelow(values, probe), probe.z);
		dig.Radius = 2.0f;
		const float oldGround = *CollisionBelow(world, probe);
		SendEdit(world, guid, dig);
		const auto start = std::chrono::steady_clock::now();
		int drawnFrame = -1;
		int collisionFrame = -1;
		double drawnSeconds = 0.0;
		for (int n = 0; n < 60 * 60 && (drawnFrame < 0 || collisionFrame < 0); ++n)
		{
			frame();
			const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
			bool bDrawn = false;
			for (const TerrainChunk& chunk : registry.Get<TerrainChunksComponent>(terrain).Chunks)
			{
				bDrawn = bDrawn || (chunk.bDrawn && chunk.Edits != before && chunk.Level == registry.Get<SdfTerrainComponent>(terrain).FinestLevel);
			}
			if (bDrawn && drawnFrame < 0)
			{
				drawnFrame = n;
				drawnSeconds = elapsed;
			}
			const std::optional<float> now = CollisionBelow(world, probe);
			if (now && *now < oldGround - 1.0f && collisionFrame < 0)
			{
				collisionFrame = n;
			}
		}
		Require(drawnFrame >= 0 && collisionFrame >= 0, "The dig should be drawn and in collision");
		Require(std::abs(drawnFrame - collisionFrame) <= 1, "The dig should be drawn and in collision together, not frames " + std::to_string(drawnFrame) + " and " + std::to_string(collisionFrame));
		std::cout << std::fixed << std::setprecision(1) << "A dig drawn at frame " << drawnFrame << " and in collision at frame " << collisionFrame << ", " << drawnSeconds * 1000.0
				  << " ms after its command\n";
	}

	// The sample scene run for 10 s: its balls fall on the hills, never into them, and land and roll
	// on what is under them (the hills, or a tunnel's floor where one rolls in)
	void TestTheSampleScene()
	{
		GameWorld world;
		Steps steps{ world };
		Registry& registry = world.GetRegistry();
		Require(SceneSerializer::LoadFromFile(Nyx::Paths::GetAssetsDir() / "Scenes/SdfTerrainTest.nyxscene", registry),
			"The sample scene can't be loaded; NyxTerrainChunksTests --write-scene writes it");
		std::vector<Entity> balls;
		const SdfTerrainComponent* terrain = nullptr;
		registry.Each<RigidBodyComponent>([&](Entity entity, RigidBodyComponent&) { balls.push_back(entity); });
		registry.Each<SdfTerrainComponent>([&](Entity, SdfTerrainComponent& component) { terrain = &component; });
		Require(terrain != nullptr && balls.size() == 6, "The sample scene should have a terrain and six balls");
		// How many steps each spent on what is under it, its centre a radius over it
		std::vector<int> touching(balls.size(), 0);
		for (int step = 0; step < 60 * 10; ++step)
		{
			steps.RunInTime(1);
			for (size_t n = 0; n < balls.size(); ++n)
			{
				const glm::vec3 position = registry.Get<TransformComponent>(balls[n]).Position;
				Require(SampleTerrainValue(*terrain, position) > 0.0f, "A ball should never be in the terrain, at " + Describe(position));
				const float gap = position.y - SurfaceBelow(*terrain, position);
				touching[n] += gap > 0.7f && gap < 1.3f ? 1 : 0;
			}
		}
		for (size_t n = 0; n < balls.size(); ++n)
		{
			Require(touching[n] > 30, "A ball should land and roll on what is under it, not " + std::to_string(touching[n]) + " steps on it");
		}
		std::cout << "The sample scene's six balls on the terrain for " << *std::min_element(touching.begin(), touching.end()) << " of 600 steps at least\n";
	}
}

int main()
{
	try
	{
		// Each line out at once, so a crash leaves what came before it
		std::cout << std::unitbuf;
		Nyx::Logger::Get().Init();
		Nyx::RegisterComponentTypes();
		TestTheWayOut();
		TestABallRestsOnTheHills();
		TestACharacterWalksOnTheHills();
		TestAFastFallPastTheCliff();
		TestTheSameTrianglesAsDrawn();
		TestBodiesComeAndGo();
		TestEditsUnderBodies();
		TestEditsDrawnWithCollision();
		TestTheSampleScene();
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All terrain collision tests passed\n";
	return 0;
}
