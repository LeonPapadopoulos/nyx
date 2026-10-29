// An SDF terrain's chunks (TerrainChunks.h) without a GPU: made on the workers round a viewpoint,
// each the same bits as the chunk made directly; only chunks the surface crosses, all within the
// view distance; moving away lets them go and makes new ones; new values make them again while the
// old ones stay drawn; another view distance makes nothing again; removing the terrain takes them
// and its jobs away; a terrain with no layers has none; a terrain entity moved has its chunks round
// it; the sample scene's terrain is made. Prints how long a view takes to make.
//
// NyxTerrainChunksTests --write-scene writes the sample scene, Assets/Scenes/SdfTerrainTest.nyxscene.
#include "CameraComponent.h"
#include "ComponentRegistration.h"
#include "DirectionalLightComponent.h"
#include "Entity.h"
#include "FlyCameraComponent.h"
#include "GuidComponent.h"
#include "JobSystem.h"
#include "Log.h"
#include "MeshRendererComponent.h"
#include "NameComponent.h"
#include "Paths.h"
#include "SceneSerializer.h"
#include "SdfTerrain.h"
#include "TerrainChunks.h"
#include "TestSupport.h"
#include "TransformComponent.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
	using namespace Nyx;
	using Nyx::Tests::Require;

	// Hills with terraces and tunnels under them
	SdfTerrainComponent MakeTerrain()
	{
		SdfTerrainComponent terrain;
		terrain.Seed = 4;
		terrain.ViewDistance = 80.0f;
		terrain.Layers[0].Amplitude = 10.0f;
		terrain.Layers[0].Frequency = 0.015f;
		terrain.Layers[0].TerraceHeight = 4.0f;
		terrain.Layers[0].Material = 0;
		SdfTerrainLayer tunnels;
		tunnels.Kind = ESdfTerrainLayerKind::Tunnels;
		tunnels.Combine = ESdfTerrainCombine::Carve;
		tunnels.Height = 0.0f;
		tunnels.Depth = 30.0f;
		tunnels.Radius = 5.0f;
		tunnels.Frequency = 0.03f;
		tunnels.Octaves = 2;
		tunnels.Material = 2;
		terrain.Layers.push_back(tunnels);
		terrain.SharpMaterials = { 2 };
		return terrain;
	}

	Entity AddTerrain(Registry& world, const SdfTerrainComponent& terrain, const glm::vec3& position = glm::vec3(0.0f))
	{
		const Entity entity = world.CreateEntity();
		world.Add<TransformComponent>(entity, TransformComponent{ .Position = position });
		world.Add<SdfTerrainComponent>(entity, terrain);
		return entity;
	}

	// Frames until every wanted chunk is in, made from the values as they are, and nothing is being
	// made, at most a minute. Chunks up to a chunk past the view distance may stay as well.
	void UpdateUntilSettled(Registry& world, std::span<const glm::vec3> viewpoints, JobSystem& jobs, Entity terrain)
	{
		const auto start = std::chrono::steady_clock::now();
		for (;;)
		{
			UpdateTerrainChunks(world, viewpoints, jobs);
			const TerrainChunksStats& stats = world.Get<TerrainChunksComponent>(terrain).Stats;
			bool bAllCurrent = true;
			for (const TerrainChunk& chunk : world.Get<TerrainChunksComponent>(terrain).Chunks)
			{
				bAllCurrent = bAllCurrent && chunk.Shape == HashSdfTerrainShape(world.Get<SdfTerrainComponent>(terrain));
			}
			if (stats.Pending == 0 && stats.Resident >= stats.Wanted && bAllCurrent)
			{
				return;
			}
			Require(std::chrono::steady_clock::now() - start < std::chrono::seconds(60), "The chunks should settle within a minute");
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}

	bool SameMesh(const MeshData& a, const MeshData& b)
	{
		if (a.Vertices.size() != b.Vertices.size() || a.Indices != b.Indices)
		{
			return false;
		}
		for (size_t v = 0; v < a.Vertices.size(); ++v)
		{
			if (std::memcmp(&a.Vertices[v].Position, &b.Vertices[v].Position, sizeof(glm::vec3)) != 0 ||
				std::memcmp(&a.Vertices[v].Normal, &b.Vertices[v].Normal, sizeof(glm::vec3)) != 0 ||
				std::memcmp(&a.Vertices[v].Custom, &b.Vertices[v].Custom, sizeof(glm::vec4)) != 0 || a.Vertices[v].Color != b.Vertices[v].Color)
			{
				return false;
			}
		}
		return true;
	}

	// Round a viewpoint: every chunk wanted in, each the same bits as made directly, all within the
	// view distance, crossed by the surface, its mesh around its corner
	void TestChunksRoundAViewpoint()
	{
		JobSystem jobs(4);
		Registry world;
		const SdfTerrainComponent values = MakeTerrain();
		const Entity terrain = AddTerrain(world, values);
		const glm::vec3 viewpoint(5.0f, 12.0f, -3.0f);
		UpdateUntilSettled(world, { &viewpoint, 1 }, jobs, terrain);

		const TerrainChunksComponent& chunks = world.Get<TerrainChunksComponent>(terrain);
		Require(chunks.Stats.Resident > 20, "Many chunks should be made round the viewpoint, not " + std::to_string(chunks.Stats.Resident));
		Require(chunks.Stats.Skipped > 0, "Chunks all air or all solid should be skipped");
		Require(chunks.Stats.Triangles > 10000, "The chunks should have many triangles");
		for (const TerrainChunk& chunk : chunks.Chunks)
		{
			Require(chunk.Origin == glm::vec3(chunk.Key) * TerrainChunkSize, "A chunk's corner should be its key times its size");
			const glm::vec3 nearest = glm::clamp(viewpoint, chunk.Origin, chunk.Origin + TerrainChunkSize);
			Require(glm::length(viewpoint - nearest) <= values.ViewDistance, "Every chunk should be within the view distance");
			const MeshData direct = MakeTerrainChunkMesh(values, chunk.Key);
			if (!chunk.Mesh.Mesh)
			{
				Require(direct.Indices.empty(), "A chunk without a mesh should have no surface");
				continue;
			}
			Require(SameMesh(*chunk.Mesh.Mesh, direct), "A chunk made on a worker should be the same bits as made directly");
			for (const Vertex& vertex : chunk.Mesh.Mesh->Vertices)
			{
				Require(glm::all(glm::greaterThanEqual(vertex.Position, glm::vec3(-TerrainCellSize - 0.01f))) && glm::all(glm::lessThanEqual(vertex.Position, glm::vec3(TerrainChunkSize + 0.01f))),
					"A chunk's vertices should be within a cell of its own cube");
			}
		}
		std::cout << "Round a viewpoint: " << chunks.Stats.Resident << " chunks, " << chunks.Stats.Skipped << " skipped, " << chunks.Stats.Triangles << " triangles\n";
	}

	// Moving away lets the chunks behind go and makes those ahead; another view distance alone makes
	// nothing again
	void TestMovingAndViewDistance()
	{
		JobSystem jobs(4);
		Registry world;
		const Entity terrain = AddTerrain(world, MakeTerrain());
		glm::vec3 viewpoint(0.0f, 10.0f, 0.0f);
		UpdateUntilSettled(world, { &viewpoint, 1 }, jobs, terrain);
		const uint64_t madeHere = world.Get<TerrainChunksComponent>(terrain).Stats.Made;

		viewpoint.x = 400.0f;
		UpdateUntilSettled(world, { &viewpoint, 1 }, jobs, terrain);
		const TerrainChunksComponent& moved = world.Get<TerrainChunksComponent>(terrain);
		for (const TerrainChunk& chunk : moved.Chunks)
		{
			Require(chunk.Origin.x > 200.0f, "Chunks left far behind should go");
		}
		Require(moved.Stats.Made > madeHere, "Chunks ahead should be made");

		const uint64_t made = moved.Stats.Made;
		world.Get<SdfTerrainComponent>(terrain).ViewDistance = 60.0f;
		UpdateUntilSettled(world, { &viewpoint, 1 }, jobs, terrain);
		Require(world.Get<TerrainChunksComponent>(terrain).Stats.Made == made, "A shorter view distance alone should make nothing again");
	}

	// New values: the chunks stay drawn while their new meshes are made, then are the new values'
	void TestNewValues()
	{
		JobSystem jobs(2);
		Registry world;
		const Entity terrain = AddTerrain(world, MakeTerrain());
		const glm::vec3 viewpoint(0.0f, 10.0f, 0.0f);
		UpdateUntilSettled(world, { &viewpoint, 1 }, jobs, terrain);
		const uint32_t before = world.Get<TerrainChunksComponent>(terrain).Stats.Resident;

		world.Get<SdfTerrainComponent>(terrain).Seed = 99;
		UpdateTerrainChunks(world, { &viewpoint, 1 }, jobs);
		Require(world.Get<TerrainChunksComponent>(terrain).Stats.Resident >= before / 2, "Chunks should stay drawn while they are made again");
		UpdateUntilSettled(world, { &viewpoint, 1 }, jobs, terrain);
		const SdfTerrainComponent& values = world.Get<SdfTerrainComponent>(terrain);
		for (const TerrainChunk& chunk : world.Get<TerrainChunksComponent>(terrain).Chunks)
		{
			if (chunk.Mesh.Mesh)
			{
				Require(SameMesh(*chunk.Mesh.Mesh, MakeTerrainChunkMesh(values, chunk.Key)), "A chunk made again should be the new values'");
			}
		}
	}

	// Removed, its chunks and jobs go; a terrain with no layers has no chunks; one moved has them
	// round where it is
	void TestRemovedEmptyAndMoved()
	{
		JobSystem jobs(2);
		Registry world;
		const Entity terrain = AddTerrain(world, MakeTerrain());
		const glm::vec3 viewpoint(0.0f, 10.0f, 0.0f);
		UpdateTerrainChunks(world, { &viewpoint, 1 }, jobs);
		Require(world.Get<TerrainChunksComponent>(terrain).Stats.Pending > 0, "Chunks should be being made");
		RemoveTerrainChunks(world, terrain);
		Require(!world.Has<TerrainChunksComponent>(terrain), "Removing a terrain's chunks should take them away");

		SdfTerrainComponent empty = MakeTerrain();
		empty.Layers.clear();
		const Entity nothing = AddTerrain(world, empty);
		world.Remove<SdfTerrainComponent>(terrain);
		UpdateUntilSettled(world, { &viewpoint, 1 }, jobs, nothing);
		Require(world.Get<TerrainChunksComponent>(nothing).Chunks.empty() && world.Get<TerrainChunksComponent>(nothing).Stats.Skipped > 0,
			"A terrain with no layers should have no chunks");
		world.Remove<SdfTerrainComponent>(nothing);

		Registry far;
		const Entity moved = AddTerrain(far, MakeTerrain(), glm::vec3(1000.0f, 0.0f, 0.0f));
		const glm::vec3 there(1000.0f, 10.0f, 0.0f);
		UpdateUntilSettled(far, { &there, 1 }, jobs, moved);
		const TerrainChunksComponent& chunks = far.Get<TerrainChunksComponent>(moved);
		Require(!chunks.Chunks.empty(), "A moved terrain should have chunks round the viewpoint");
		for (const TerrainChunk& chunk : chunks.Chunks)
		{
			Require(std::abs(chunk.Origin.x) < 120.0f, "A moved terrain's chunks should be round it, in its own space");
		}
	}

	// Not a test: how long a view takes to make on all the workers
	void TimeAView()
	{
		Registry world;
		SdfTerrainComponent values = MakeTerrain();
		values.ViewDistance = 128.0f;
		const Entity terrain = AddTerrain(world, values);
		const glm::vec3 viewpoint(0.0f, 10.0f, 0.0f);
		const auto start = std::chrono::steady_clock::now();
		UpdateUntilSettled(world, { &viewpoint, 1 }, JobSystem::Get(), terrain);
		const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		// Each frame once all is made: what an update costs while nothing changes
		const auto settled = std::chrono::steady_clock::now();
		constexpr int Frames = 100;
		for (int frame = 0; frame < Frames; ++frame)
		{
			UpdateTerrainChunks(world, { &viewpoint, 1 }, JobSystem::Get());
		}
		const double frameMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - settled).count() / Frames;
		const TerrainChunksStats& stats = world.Get<TerrainChunksComponent>(terrain).Stats;
		std::cout << "An update with nothing to make: " << frameMilliseconds << " ms\n";
		std::cout << "A view of 128 m: " << stats.Resident << " chunks, " << stats.Triangles << " triangles, made in " << seconds << " s on "
				  << JobSystem::Get().GetWorkerCount() << " workers; the last chunk sampled in " << stats.LastSampleMilliseconds << " ms, meshed in "
				  << stats.LastMeshMilliseconds << " ms\n";
	}

	const std::filesystem::path SampleScenePath = "Scenes/SdfTerrainTest.nyxscene";

	Entity AddNamed(Registry& world, const std::string& name, const TransformComponent& transform)
	{
		const Entity entity = world.CreateEntity();
		world.Add<NameComponent>(entity, NameComponent{ name });
		world.Add<GuidComponent>(entity, GuidComponent{ EntityGuid::Generate() });
		world.Add<TransformComponent>(entity, transform);
		return entity;
	}

	// The scene to look at the terrain in by hand: terraced hills with tunnels and chambers under them,
	// cliffs of stone kept sharp, and a floating landmass with a hanging underside above them, in
	// their vertex colours, a sun, and a camera that flies
	SdfTerrainComponent MakeSampleTerrain()
	{
		SdfTerrainComponent terrain;
		terrain.Seed = 7;
		terrain.ViewDistance = 256.0f;
		terrain.Layers.clear();

		SdfTerrainLayer hills;
		hills.Amplitude = 18.0f;
		hills.Frequency = 0.008f;
		hills.TerraceHeight = 5.0f;
		hills.TerraceSharpness = 0.7f;
		hills.Material = 0;
		terrain.Layers.push_back(hills);

		SdfTerrainLayer tunnels;
		tunnels.Kind = ESdfTerrainLayerKind::Tunnels;
		tunnels.Combine = ESdfTerrainCombine::Carve;
		tunnels.Height = 4.0f;
		tunnels.Depth = 40.0f;
		tunnels.Radius = 7.0f;
		tunnels.Frequency = 0.02f;
		tunnels.Octaves = 3;
		tunnels.BlendRadius = 1.5f;
		terrain.Layers.push_back(tunnels);

		SdfTerrainLayer island;
		island.Kind = ESdfTerrainLayerKind::Landmass;
		island.Height = 90.0f;
		island.Depth = 25.0f;
		island.Amplitude = 4.0f;
		island.Center = glm::vec2(40.0f, -60.0f);
		island.Radius = 70.0f;
		island.UndersideDepth = 35.0f;
		island.EdgeRoughness = 18.0f;
		island.Rounding = 3.0f;
		island.Frequency = 0.02f;
		island.TerraceHeight = 3.0f;
		island.Material = 2;
		terrain.Layers.push_back(island);

		SdfTerrainLayer chambers;
		chambers.Kind = ESdfTerrainLayerKind::Chambers;
		chambers.Combine = ESdfTerrainCombine::Carve;
		chambers.Height = 85.0f;
		chambers.Depth = 20.0f;
		chambers.Openness = 0.55f;
		chambers.Frequency = 0.035f;
		chambers.Octaves = 2;
		terrain.Layers.push_back(chambers);

		SdfTerrainLayer detail;
		detail.Kind = ESdfTerrainLayerKind::Detail;
		detail.Amplitude = 0.4f;
		detail.Frequency = 0.25f;
		detail.Octaves = 2;
		terrain.Layers.push_back(detail);

		terrain.SharpMaterials = { 2 };
		return terrain;
	}

	void WriteSampleScene()
	{
		Registry world;
		const Entity terrain = AddNamed(world, "Terrain", TransformComponent{});
		world.Add<SdfTerrainComponent>(terrain, MakeSampleTerrain());
		world.Add<MeshRendererComponent>(terrain, MeshRendererComponent{ .Material = AssetReference{ .Type = "Material", .Path = "Materials/VertexColor.nyxmat" }, .bVisible = true });

		const Entity camera = AddNamed(world, "Camera",
			TransformComponent{ .Position = glm::vec3(-60.0f, 40.0f, 80.0f), .Rotation = glm::quat(glm::radians(glm::vec3(-15.0f, -30.0f, 0.0f))) });
		world.Add<CameraComponent>(camera, CameraComponent{});
		world.Add<FlyCameraComponent>(camera, FlyCameraComponent{});

		const Entity sun = AddNamed(world, "Sun", TransformComponent{ .Position = glm::vec3(0.0f, 50.0f, 0.0f), .Rotation = glm::quat(glm::radians(glm::vec3(-45.0f, 35.0f, 0.0f))) });
		world.Add<DirectionalLightComponent>(sun, DirectionalLightComponent{});

		const std::filesystem::path path = Nyx::Paths::GetAssetsDir() / SampleScenePath;
		Require(SceneSerializer::SaveToFile(world, path), "The sample scene can't be saved");
		std::cout << "Wrote " << path.string() << "\n";
	}

	// The sample scene: its terrain's chunks made round its camera
	void TestTheSampleScene()
	{
		Registry world;
		Require(SceneSerializer::LoadFromFile(Nyx::Paths::GetAssetsDir() / SampleScenePath, world),
			"The sample scene can't be loaded; NyxTerrainChunksTests --write-scene writes it");
		Entity terrain;
		bool bFound = false;
		world.Each<SdfTerrainComponent>([&](Entity entity, SdfTerrainComponent& component)
			{
				terrain = entity;
				bFound = true;
				component.ViewDistance = 96.0f;
			});
		Require(bFound, "The sample scene should have a terrain");
		glm::vec3 viewpoint(0.0f);
		world.Each<CameraComponent>([&](Entity entity, CameraComponent&) { viewpoint = world.Get<TransformComponent>(entity).Position; });
		JobSystem jobs(4);
		UpdateUntilSettled(world, { &viewpoint, 1 }, jobs, terrain);
		Require(world.Get<TerrainChunksComponent>(terrain).Stats.Triangles > 1000, "The sample scene's terrain should be made round its camera");
	}
}

int main(int argc, char** argv)
{
	try
	{
		Nyx::Logger::Get().Init();
		Nyx::RegisterComponentTypes();
		if (argc > 1 && std::strcmp(argv[1], "--write-scene") == 0)
		{
			WriteSampleScene();
			return 0;
		}

		TestChunksRoundAViewpoint();
		TestMovingAndViewDistance();
		TestNewValues();
		TestRemovedEmptyAndMoved();
		TestTheSampleScene();
		TimeAView();
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All terrain chunk tests passed\n";
	return 0;
}
