// An SDF terrain's chunks (TerrainChunks.h) without a GPU: the tree of levels round viewpoints,
// covering the view distance without overlap, balanced, each level where its distance says, held
// split by the hysteresis; the chunks of a tree, meshed against their neighbourhoods, meet without a
// gap at every seam between levels, with both meshers and with half-metre cells; made on the
// workers round a viewpoint, each the same bits as the chunk made directly, all within the view
// distance; drawn all at once, a tree at a time; moving away lets them go and makes new ones; new
// values make them again while the old ones stay drawn; another view distance alone makes nothing
// again; edits make only the chunks they reach again, drawn with collision's or at the deadline;
// the terrain brush's strokes, rays and ring;
// removing the terrain takes them and its jobs away; a terrain with no layers has none; a
// terrain entity moved has its chunks round it; the sample scene's terrain is made. Prints how long
// a view takes to make.
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
#include "PhysicsComponents.h"
#include "SceneSerializer.h"
#include "SdfTerrain.h"
#include "TerrainBrush.h"
#include "TerrainChunks.h"
#include "TestSupport.h"
#include "TransformComponent.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

namespace
{
	using namespace Nyx;
	using Nyx::Tests::Require;

	// Hills with terraces and tunnels under them, sharp where the tunnels cut
	SdfTerrainComponent MakeTerrain()
	{
		SdfTerrainComponent terrain;
		terrain.Seed = 4;
		terrain.ViewDistance = 160.0f;
		terrain.LevelDistance = 2.0f;
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

	// Frames until the chunks drawn are those chosen for where the viewpoints are, made from the
	// values as they are, at most a minute
	void UpdateUntilSettled(Registry& world, std::span<const glm::vec3> viewpoints, JobSystem& jobs, Entity terrain)
	{
		const auto start = std::chrono::steady_clock::now();
		for (;;)
		{
			UpdateTerrainChunks(world, viewpoints, jobs);
			const TerrainChunksStats& stats = world.Get<TerrainChunksComponent>(terrain).Stats;
			if (stats.bSettled && stats.Pending == 0)
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

	glm::vec3 CornerOf(const TerrainChunkId& id)
	{
		return glm::vec3(id.Key) * GetTerrainChunkSize(id.Level);
	}

	// Whether two chunks' boxes share a point, and whether they share some volume
	bool Touch(const TerrainChunkId& a, const TerrainChunkId& b)
	{
		const glm::vec3 lowA = CornerOf(a);
		const glm::vec3 lowB = CornerOf(b);
		return glm::all(glm::lessThanEqual(lowA, lowB + GetTerrainChunkSize(b.Level))) && glm::all(glm::lessThanEqual(lowB, lowA + GetTerrainChunkSize(a.Level)));
	}

	bool Overlap(const TerrainChunkId& a, const TerrainChunkId& b)
	{
		const glm::vec3 lowA = CornerOf(a);
		const glm::vec3 lowB = CornerOf(b);
		return glm::all(glm::lessThan(lowA, lowB + GetTerrainChunkSize(b.Level))) && glm::all(glm::lessThan(lowB, lowA + GetTerrainChunkSize(a.Level)));
	}

	float DistanceTo(const glm::vec3& point, const TerrainChunkId& id)
	{
		const glm::vec3 low = CornerOf(id);
		return glm::length(point - glm::clamp(point, low, low + GetTerrainChunkSize(id.Level)));
	}

	// The tree round a viewpoint: within the view distance and all of it covered once, balanced,
	// each level no nearer than its distance; held split by the hysteresis
	void TestLevelTree()
	{
		TerrainLevelSettings settings;
		settings.ViewDistance = 700.0f;
		settings.LevelDistance = 3.0f;
		const glm::vec3 viewpoint(10.0f, 5.0f, -20.0f);
		const TerrainChunkTree tree = SelectTerrainChunks({ &viewpoint, 1 }, settings);
		Require(tree.Leaves.size() > 100, "A tree of 700 m should have many chunks, not " + std::to_string(tree.Leaves.size()));
		int coarsest = 0;
		for (size_t a = 0; a < tree.Leaves.size(); ++a)
		{
			const TerrainChunkId& leaf = tree.Leaves[a];
			coarsest = std::max(coarsest, leaf.Level);
			const float distance = DistanceTo(viewpoint, leaf);
			Require(distance <= settings.ViewDistance, "Every chunk should be within the view distance");
			if (leaf.Level > 0)
			{
				Require(distance >= settings.LevelDistance * GetTerrainChunkSize(leaf.Level - 1), "A chunk should be no coarser than its distance says");
			}
			for (size_t b = a + 1; b < tree.Leaves.size(); ++b)
			{
				const TerrainChunkId& other = tree.Leaves[b];
				Require(!Overlap(leaf, other), "Chunks should not overlap");
				if (Touch(leaf, other))
				{
					Require(std::abs(leaf.Level - other.Level) <= 1, "Chunks that touch should be at most a level apart");
				}
			}
		}
		Require(coarsest >= 3, "A tree of 700 m should reach level 3 at least");

		// Every point within the view distance in one chunk
		for (int n = 0; n < 4000; ++n)
		{
			const float t = float(n) * 0.61803398f;
			const glm::vec3 direction = glm::normalize(glm::vec3(std::sin(t * 7.1f), std::cos(t * 3.3f), std::sin(t * 5.7f + 1.0f)));
			const glm::vec3 point = viewpoint + direction * (settings.ViewDistance * float(n % 97) / 97.0f);
			int containing = 0;
			for (const TerrainChunkId& leaf : tree.Leaves)
			{
				const glm::vec3 low = CornerOf(leaf);
				containing += glm::all(glm::greaterThanEqual(point, low)) && glm::all(glm::lessThan(point, low + GetTerrainChunkSize(leaf.Level))) ? 1 : 0;
			}
			Require(containing == 1, "Every point within the view distance should be in one chunk, not " + std::to_string(containing));
		}

		// Moved just past where a chunk of level 1 joins back: still split with the tree before, not
		// without
		const glm::vec3 near(-60.0f, 32.0f, 32.0f);
		const TerrainChunkTree before = SelectTerrainChunks({ &near, 1 }, settings);
		const TerrainChunkId watched{ 1, glm::ivec3(0) };
		Require(std::find(before.Split.begin(), before.Split.end(), watched) != before.Split.end(), "The chunk of level 1 by the viewpoint should be split");
		const glm::vec3 away(-settings.LevelDistance * TerrainChunkSize * 1.1f, 32.0f, 32.0f);
		const TerrainChunkTree held = SelectTerrainChunks({ &away, 1 }, settings, &before);
		const TerrainChunkTree fresh = SelectTerrainChunks({ &away, 1 }, settings);
		Require(std::find(held.Split.begin(), held.Split.end(), watched) != held.Split.end(), "A chunk split should stay split within the hysteresis");
		Require(std::find(fresh.Split.begin(), fresh.Split.end(), watched) == fresh.Split.end(), "Without the tree before, the chunk should not be split there");
		std::cout << "A tree of 700 m: " << tree.Leaves.size() << " chunks, levels 0 to " << coarsest << "\n";
	}

	// The chunks of a tree, all meshed against their neighbourhoods and put together: every edge of
	// a triangle well within the view distance has two triangles, one each way round, wherever two
	// levels meet. With a level distance past the view distance, all one level, to compare.
	void TestSeamsAreClosed(ESdfTerrainMesher mesher, int finestLevel, float levelDistance = 2.0f)
	{
		SdfTerrainComponent terrain = MakeTerrain();
		terrain.Mesher = mesher;
		terrain.FinestLevel = finestLevel;
		TerrainLevelSettings settings;
		settings.ViewDistance = 200.0f;
		settings.LevelDistance = levelDistance;
		settings.FinestLevel = finestLevel;
		const glm::vec3 viewpoint(5.0f, 4.0f, -3.0f);
		const TerrainChunkTree tree = SelectTerrainChunks({ &viewpoint, 1 }, settings);
		const std::vector<TerrainNeighbourhood> neighbourhoods = GetTerrainNeighbourhoods(tree);

		std::vector<MeshData> meshes(tree.Leaves.size());
		std::vector<TerrainChunkTimes> times(tree.Leaves.size());
		JobGroup group;
		for (size_t index = 0; index < tree.Leaves.size(); ++index)
		{
			JobSystem::Get().Schedule([&, index] { meshes[index] = MakeTerrainChunkMesh(terrain, tree.Leaves[index], neighbourhoods[index], &times[index]); },
				EJobPriority::Normal, &group);
		}
		JobSystem::Get().Wait(group);

		// Each edge, by its ends' bits in the world, counted each way round
		using Point = std::array<uint32_t, 3>;
		auto bitsOf = [](const glm::vec3& position)
		{
			// -0 is 0: the same place
			const glm::vec3 same = position + glm::vec3(0.0f);
			Point point;
			std::memcpy(point.data(), &same, sizeof(glm::vec3));
			return point;
		};
		std::map<std::pair<Point, Point>, std::array<int, 2>> edges;
		uint32_t seamChunks = 0;
		uint32_t seamTriangles = 0;
		uint64_t triangles = 0;
		const float checked = settings.ViewDistance - 16.0f;
		for (size_t index = 0; index < tree.Leaves.size(); ++index)
		{
			const MeshData& mesh = meshes[index];
			const glm::vec3 corner = CornerOf(tree.Leaves[index]);
			seamChunks += neighbourhoods[index].HasSeams() && !mesh.Indices.empty() ? 1u : 0u;
			seamTriangles += times[index].SeamTriangles;
			triangles += mesh.Indices.size() / 3;
			for (size_t t = 0; t + 2 < mesh.Indices.size(); t += 3)
			{
				glm::vec3 positions[3];
				for (int n = 0; n < 3; ++n)
				{
					positions[n] = mesh.Vertices[mesh.Indices[t + n]].Position + corner;
				}
				for (int n = 0; n < 3; ++n)
				{
					const Point a = bitsOf(positions[n]);
					const Point b = bitsOf(positions[(n + 1) % 3]);
					if (a == b)
					{
						continue;
					}
					const bool bForward = a < b;
					edges[bForward ? std::make_pair(a, b) : std::make_pair(b, a)][bForward ? 0 : 1] += 1;
				}
			}
		}
		uint32_t open = 0;
		uint32_t shared = 0;
		uint32_t wound = 0;
		uint32_t checkedEdges = 0;
		glm::vec3 firstOpen(0.0f);
		for (const auto& [ends, count] : edges)
		{
			glm::vec3 a;
			glm::vec3 b;
			std::memcpy(&a, ends.first.data(), sizeof(glm::vec3));
			std::memcpy(&b, ends.second.data(), sizeof(glm::vec3));
			if (glm::length(a - viewpoint) > checked || glm::length(b - viewpoint) > checked)
			{
				continue;
			}
			++checkedEdges;
			const int total = count[0] + count[1];
			if (total % 2 != 0)
			{
				if (open < 6)
				{
					std::cout << std::setprecision(9) << "  open: " << a.x << " " << a.y << " " << a.z << " to " << b.x << " " << b.y << " " << b.z << " (" << count[0] << ", " << count[1] << ")\n";
				}
				firstOpen = open == 0 ? a : firstOpen;
				++open;
			}
			else if (total > 2)
			{
				++shared;
			}
			else if (count[0] != 1)
			{
				++wound;
			}
		}
		if (open > 0)
		{
			// Which chunks have triangles by the first open edge, for finding why
			for (size_t index = 0; index < tree.Leaves.size(); ++index)
			{
				const glm::vec3 corner = CornerOf(tree.Leaves[index]);
				bool bNear = false;
				for (const Vertex& vertex : meshes[index].Vertices)
				{
					bNear = bNear || glm::length(vertex.Position + corner - firstOpen) < 1.5f;
				}
				if (bNear)
				{
					std::string levels;
					for (const int8_t level : neighbourhoods[index].Levels)
					{
						levels += std::to_string(int(level)) + " ";
					}
					std::cout << "  near it: level " << tree.Leaves[index].Level << " key " << tree.Leaves[index].Key.x << " " << tree.Leaves[index].Key.y << " "
							  << tree.Leaves[index].Key.z << " round it " << levels << "\n";
				}
			}
		}
		const char* name = mesher == ESdfTerrainMesher::Dual ? "Dual" : "Marching cubes";
		std::cout << name << ", finest level " << finestLevel << ": " << tree.Leaves.size() << " chunks, " << seamChunks << " with seams, " << triangles
				  << " triangles, " << seamTriangles << " joining levels; of " << checkedEdges << " edges " << open << " open, " << shared
				  << " shared by four or more, " << wound << " with both triangles the same way round\n";
		Require(levelDistance > 8.0f || (seamChunks > 4 && seamTriangles > 100), "The tree should have seams to check");
		Require(open == 0, std::string(name) + ": every edge should have two triangles; " + std::to_string(open) + " open, the first at " +
			std::to_string(firstOpen.x) + ", " + std::to_string(firstOpen.y) + ", " + std::to_string(firstOpen.z));
		Require(wound == 0, std::string(name) + ": the triangles either side of an edge should be wound against each other");
		if (mesher == ESdfTerrainMesher::MarchingCubes)
		{
			Require(shared == 0, "Marching cubes: no edge should be shared by four triangles");
		}
	}

	// Round a viewpoint: every chunk of the tree the surface crosses drawn, each the same bits as made
	// directly against its neighbourhood, within the view distance, at several levels
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
		Require(chunks.Stats.ByLevel[size_t(0 - TerrainFinestLevel)] > 0 && chunks.Stats.ByLevel[size_t(2 - TerrainFinestLevel)] > 0,
			"Chunks should be drawn at levels 0 to 2 at least");
		Require(chunks.Stats.WithSeams > 0, "Some chunks should be next to another level");
		for (const TerrainChunk& chunk : chunks.Chunks)
		{
			Require(chunk.bDrawn, "Settled, every chunk should be drawn");
			const TerrainChunkId id{ chunk.Level, chunk.Key };
			Require(chunk.Origin == CornerOf(id), "A chunk's corner should be its key times its level's size");
			Require(DistanceTo(viewpoint, id) <= values.ViewDistance, "Every chunk should be within the view distance");
			const MeshData direct = MakeTerrainChunkMesh(values, id, chunk.Neighbourhood);
			if (!chunk.Mesh.Mesh)
			{
				Require(direct.Indices.empty(), "A chunk without a mesh should have no surface");
				continue;
			}
			Require(SameMesh(*chunk.Mesh.Mesh, direct), "A chunk made on a worker should be the same bits as made directly");
			const float size = GetTerrainChunkSize(chunk.Level);
			const float cell = GetTerrainCellSize(chunk.Level);
			for (const Vertex& vertex : chunk.Mesh.Mesh->Vertices)
			{
				Require(glm::all(glm::greaterThanEqual(vertex.Position, glm::vec3(-2.0f * cell - 0.01f))) && glm::all(glm::lessThanEqual(vertex.Position, glm::vec3(size + 2.0f * cell + 0.01f))),
					"A chunk's vertices should be within two cells of its own cube");
			}
		}
		std::cout << "Round a viewpoint: " << chunks.Stats.Resident << " chunks, " << chunks.Stats.WithSeams << " with seams, " << chunks.Stats.Skipped
				  << " skipped, " << chunks.Stats.Triangles << " triangles\n";
	}

	// The set of chunks drawn, as made
	std::vector<std::pair<TerrainChunkId, uint64_t>> DrawnChunks(const TerrainChunksComponent& chunks)
	{
		std::vector<std::pair<TerrainChunkId, uint64_t>> drawn;
		for (const TerrainChunk& chunk : chunks.Chunks)
		{
			if (chunk.bDrawn)
			{
				drawn.push_back({ { chunk.Level, chunk.Key }, chunk.Neighbourhood.Hash() ^ chunk.Shape });
			}
		}
		std::sort(drawn.begin(), drawn.end(), [](const auto& a, const auto& b)
			{
				return std::tie(a.first.Level, a.first.Key.x, a.first.Key.y, a.first.Key.z, a.second) < std::tie(b.first.Level, b.first.Key.x, b.first.Key.y, b.first.Key.z, b.second);
			});
		return drawn;
	}

	// The chunks drawn: each one's edits and mesh
	std::map<std::tuple<int, int, int, int>, std::pair<uint64_t, const MeshData*>> DrawnEdits(const TerrainChunksComponent& chunks)
	{
		std::map<std::tuple<int, int, int, int>, std::pair<uint64_t, const MeshData*>> drawn;
		for (const TerrainChunk& chunk : chunks.Chunks)
		{
			if (chunk.bDrawn)
			{
				drawn[{ chunk.Level, chunk.Key.x, chunk.Key.y, chunk.Key.z }] = { chunk.Edits, chunk.Mesh.Mesh.get() };
			}
		}
		return drawn;
	}

	// Frames until settled with a collision part seen each frame, ready for the edits after `readyAfter`
	// seconds (never, below 0); the seconds it took
	double UpdateUntilSettledWithCollision(Registry& world, std::span<const glm::vec3> viewpoints, JobSystem& jobs, Entity terrain, double readyAfter)
	{
		const auto start = std::chrono::steady_clock::now();
		for (;;)
		{
			const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
			TerrainEditActivationComponent& activation = world.Get<TerrainEditActivationComponent>(terrain);
			activation.CollisionSeen = GetTerrainEditClock();
			if (readyAfter >= 0.0 && elapsed >= readyAfter)
			{
				activation.CollisionReady = activation.Latest;
			}
			UpdateTerrainChunks(world, viewpoints, jobs);
			const TerrainChunksStats& stats = world.Get<TerrainChunksComponent>(terrain).Stats;
			if (stats.bSettled && stats.Pending == 0)
			{
				return elapsed;
			}
			Require(elapsed < 60.0, "The chunks should settle within a minute");
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}

	// Edits: only the chunks they reach are made again, the others kept as they were, each with the
	// edits reaching it and the same bits as made directly; undone, the chunks are as before. With no
	// collision running a dig is drawn as soon as its chunks are made; with collision running but not
	// ready, at the deadline; with it ready, at once.
	void TestEdits()
	{
		JobSystem jobs(4);
		Registry world;
		const Entity terrain = AddTerrain(world, MakeTerrain());
		const glm::vec3 viewpoint(5.0f, 12.0f, -3.0f);
		UpdateUntilSettled(world, { &viewpoint, 1 }, jobs, terrain);
		const auto before = DrawnEdits(world.Get<TerrainChunksComponent>(terrain));
		const uint64_t madeBefore = world.Get<TerrainChunksComponent>(terrain).Stats.Made;

		const std::optional<TerrainRayHit> ground = RayCastTerrain(world.Get<SdfTerrainComponent>(terrain), viewpoint, glm::vec3(0.0f, -1.0f, 0.0f), 100.0f);
		Require(ground.has_value(), "There should be ground under the viewpoint");
		SdfTerrainEdit dig;
		dig.Position = ground->Point;
		dig.Radius = 3.0f;
		AppendTerrainEdit(world.Get<SdfTerrainComponent>(terrain), dig);
		const auto start = std::chrono::steady_clock::now();
		UpdateUntilSettled(world, { &viewpoint, 1 }, jobs, terrain);
		const double digSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

		const SdfTerrainComponent& values = world.Get<SdfTerrainComponent>(terrain);
		const TerrainChunksComponent& chunks = world.Get<TerrainChunksComponent>(terrain);
		const auto after = DrawnEdits(chunks);
		Require(after.size() == before.size(), "A dig standing still should draw the same chunks");
		uint32_t changed = 0;
		for (const TerrainChunk& chunk : chunks.Chunks)
		{
			const TerrainChunkId id{ chunk.Level, chunk.Key };
			glm::vec3 low;
			glm::vec3 high;
			GetTerrainChunkReach(id, low, high);
			Require(chunk.Edits == GetTerrainEditsReaching(values, low, high).Hash, "A chunk should be made with the edits reaching it");
			const auto old = before.find({ chunk.Level, chunk.Key.x, chunk.Key.y, chunk.Key.z });
			Require(old != before.end(), "A dig standing still should draw the same chunks");
			if (old->second.first == chunk.Edits)
			{
				Require(old->second.second == chunk.Mesh.Mesh.get(), "A chunk the dig doesn't reach should be kept");
				continue;
			}
			++changed;
			const MeshData direct = MakeTerrainChunkMesh(values, id, chunk.Neighbourhood);
			Require(chunk.Mesh.Mesh ? SameMesh(*chunk.Mesh.Mesh, direct) : direct.Indices.empty(), "A chunk made again should be the same bits as made directly");
		}
		const uint64_t made = chunks.Stats.Made - madeBefore;
		Require(changed > 0 && made == changed, "Only the chunks a dig reaches should be made again: " + std::to_string(made) + " made, " + std::to_string(changed) + " reached");
		const TerrainEditActivationComponent& activation = world.Get<TerrainEditActivationComponent>(terrain);
		Require(activation.Live == HashTerrainEdits(values) && activation.Late == 0, "Without collision a dig should go live as soon as it is drawn");
		std::string byLevel;
		for (int level = TerrainFinestLevel; level <= TerrainCoarsestLevel; ++level)
		{
			uint32_t of = 0;
			uint32_t again = 0;
			for (const TerrainChunk& chunk : chunks.Chunks)
			{
				if (chunk.Level == level)
				{
					++of;
					again += before.at({ chunk.Level, chunk.Key.x, chunk.Key.y, chunk.Key.z }).first != chunk.Edits ? 1u : 0u;
				}
			}
			if (of > 0)
			{
				byLevel += " " + std::to_string(level) + ": " + std::to_string(again) + " of " + std::to_string(of) + ",";
			}
		}
		byLevel.pop_back();
		std::cout << "A dig (slope bound " << GetTerrainMaxSlope(values) << "): " << changed << " of " << after.size() << " chunks made again (by level" << byLevel << "), drawn after " << std::fixed
				  << std::setprecision(1) << digSeconds * 1000.0 << " ms\n";

		// Undone: as before
		world.Get<SdfTerrainComponent>(terrain).Edits.pop_back();
		UpdateUntilSettled(world, { &viewpoint, 1 }, jobs, terrain);
		for (const TerrainChunk& chunk : world.Get<TerrainChunksComponent>(terrain).Chunks)
		{
			const auto old = before.find({ chunk.Level, chunk.Key.x, chunk.Key.y, chunk.Key.z });
			Require(old != before.end() && old->second.first == chunk.Edits, "Undone, every chunk should be made with the edits before");
		}

		// Edits every frame while the chunks for those before are being made, one undone on the way:
		// settled, nothing made for edits since gone is drawn
		for (int n = 0; n < 12; ++n)
		{
			SdfTerrainEdit stroke = dig;
			stroke.Position += glm::vec3(float(n) * 1.5f, 0.0f, 0.0f);
			stroke.Radius = 1.5f;
			stroke.Mode = n % 3 == 2 ? ESdfTerrainEditMode::Add : ESdfTerrainEditMode::Subtract;
			AppendTerrainEdit(world.Get<SdfTerrainComponent>(terrain), stroke);
			if (n == 7)
			{
				world.Get<SdfTerrainComponent>(terrain).Edits.erase(world.Get<SdfTerrainComponent>(terrain).Edits.begin() + 3);
			}
			UpdateTerrainChunks(world, { &viewpoint, 1 }, jobs);
			std::this_thread::sleep_for(std::chrono::milliseconds(3));
		}
		UpdateUntilSettled(world, { &viewpoint, 1 }, jobs, terrain);
		for (const TerrainChunk& chunk : world.Get<TerrainChunksComponent>(terrain).Chunks)
		{
			const TerrainChunkId id{ chunk.Level, chunk.Key };
			glm::vec3 low;
			glm::vec3 high;
			GetTerrainChunkReach(id, low, high);
			Require(chunk.Edits == GetTerrainEditsReaching(values, low, high).Hash, "After a stroke, every chunk should be made with the edits reaching it now");
			const MeshData direct = MakeTerrainChunkMesh(values, id, chunk.Neighbourhood);
			Require(chunk.Mesh.Mesh ? SameMesh(*chunk.Mesh.Mesh, direct) : direct.Indices.empty(), "After a stroke, every chunk should be the same bits as made directly");
		}
		world.Get<SdfTerrainComponent>(terrain).Edits.clear();
		UpdateUntilSettled(world, { &viewpoint, 1 }, jobs, terrain);

		// Collision running and never ready: drawn at the deadline
		const float deadline = world.Get<SdfTerrainComponent>(terrain).ActivationDeadline;
		AppendTerrainEdit(world.Get<SdfTerrainComponent>(terrain), dig);
		const double waited = UpdateUntilSettledWithCollision(world, { &viewpoint, 1 }, jobs, terrain, -1.0);
		Require(waited >= double(deadline) && world.Get<TerrainEditActivationComponent>(terrain).Late == 1, "With collision not ready, a dig should be drawn at the deadline");

		// Collision ready soon after, the deadline far off: drawn once both are
		world.Get<SdfTerrainComponent>(terrain).ActivationDeadline = 30.0f;
		SdfTerrainEdit heap = dig;
		heap.Mode = ESdfTerrainEditMode::Add;
		heap.Radius = 2.0f;
		AppendTerrainEdit(world.Get<SdfTerrainComponent>(terrain), heap);
		const double together = UpdateUntilSettledWithCollision(world, { &viewpoint, 1 }, jobs, terrain, 0.05);
		Require(together >= 0.05 && world.Get<TerrainEditActivationComponent>(terrain).Late == 1, "With collision ready, an edit should be drawn with it");
		std::cout << "With collision: drawn at the deadline after " << waited * 1000.0 << " ms; with it ready at 50 ms, after " << together * 1000.0 << " ms\n";
	}

	// The terrain brush: a stroke's edits a spacing apart, those between filled in, a capsule's rods
	// joined end to end; a ray meeting the nearest of two terrains through their transforms, or the
	// one asked for; its ring on the surface's plane; its dig a dig
	void TestBrush()
	{
		TerrainBrushSettings settings;
		settings.Radius = 2.0f;
		settings.Spacing = 0.5f;
		TerrainBrushStroke balls(settings, Entity{});
		Require(balls.MoveTo(glm::vec3(0.0f)).size() == 1, "A stroke should make its first edit where it starts");
		Require(balls.MoveTo(glm::vec3(0.4f, 0.0f, 0.0f)).empty(), "A stroke shouldn't make another before it has gone its spacing");
		const std::vector<SdfTerrainEdit> jump = balls.MoveTo(glm::vec3(3.4f, 0.0f, 0.0f));
		Require(jump.size() == 3 && std::fabs(jump[0].Position.x - 1.0f) < 1e-5f && std::fabs(jump[2].Position.x - 3.0f) < 1e-5f,
			"A stroke that jumps should fill in its edits a spacing apart");

		settings.Shape = ESdfTerrainEditShape::Capsule;
		TerrainBrushStroke rods(settings, Entity{});
		rods.MoveTo(glm::vec3(0.0f));
		const std::vector<SdfTerrainEdit> joined = rods.MoveTo(glm::vec3(0.0f, 0.0f, 2.5f));
		Require(joined.size() == 2 && joined[0].Position == glm::vec3(0.0f) && joined[0].End == joined[1].Position && joined[1].End.z == 2.0f,
			"A capsule stroke's rods should join end to end");

		Registry world;
		SdfTerrainComponent flat;
		flat.Layers[0].Height = 0.0f;
		flat.Layers[0].Amplitude = 0.0f;
		const Entity low = AddTerrain(world, flat, glm::vec3(10.0f, 5.0f, 0.0f));
		const Entity high = AddTerrain(world, flat, glm::vec3(0.0f, 20.0f, 0.0f));
		const Ray down{ glm::vec3(12.0f, 50.0f, 3.0f), glm::vec3(0.0f, -1.0f, 0.0f) };
		const std::optional<TerrainBrushHit> hit = CastTerrainBrush(world, down, 1000.0f);
		Require(hit && hit->Terrain == high && std::fabs(hit->Point.y - 20.0f) < 0.02f && std::fabs(hit->Distance - 30.0f) < 0.02f,
			"A ray should meet the nearest terrain where it is");
		const std::optional<TerrainBrushHit> lower = CastTerrainBrush(world, down, 1000.0f, low);
		Require(lower && lower->Terrain == low && std::fabs(lower->Point.y - 5.0f) < 0.02f && std::fabs(lower->LocalPoint.y) < 0.02f &&
					std::fabs(lower->LocalPoint.x - 2.0f) < 1e-4f && glm::length(lower->Normal - glm::vec3(0.0f, 1.0f, 0.0f)) < 1e-3f,
			"A ray should meet the terrain asked for, in its space too");
		Require(!CastTerrainBrush(world, down, 20.0f, low), "A ray shouldn't meet a terrain past its reach");

		for (const glm::vec3& point : GetTerrainBrushOutline(*lower, 2.0f))
		{
			Require(std::fabs(glm::length(point - lower->Point) - 2.0f) < 1e-4f && std::fabs(point.y - lower->Point.y) < 1e-4f,
				"The brush's ring should be its radius round the hit, on the surface's plane");
		}

		SdfTerrainComponent& dug = world.Get<SdfTerrainComponent>(low);
		settings.Shape = ESdfTerrainEditShape::Sphere;
		TerrainBrushStroke dig(settings, low);
		for (const SdfTerrainEdit& edit : dig.MoveTo(lower->LocalPoint))
		{
			AppendTerrainEdit(dug, edit);
		}
		Require(dug.Edits.size() == 1 && SampleTerrainValue(dug, lower->LocalPoint - glm::vec3(0.0f, 1.0f, 0.0f)) > 0.0f, "The brush should dig where it meets the terrain");
	}

	// Moving away lets the chunks behind go and makes those ahead, the chunks drawn changing only all
	// at once; another view distance alone makes nothing again
	void TestMovingAndViewDistance()
	{
		JobSystem jobs(4);
		Registry world;
		const Entity terrain = AddTerrain(world, MakeTerrain());
		glm::vec3 viewpoint(0.0f, 10.0f, 0.0f);
		UpdateUntilSettled(world, { &viewpoint, 1 }, jobs, terrain);
		const uint64_t madeHere = world.Get<TerrainChunksComponent>(terrain).Stats.Made;

		// Flying away a few metres a frame
		auto drawn = DrawnChunks(world.Get<TerrainChunksComponent>(terrain));
		uint64_t swaps = world.Get<TerrainChunksComponent>(terrain).Stats.Swaps;
		const auto start = std::chrono::steady_clock::now();
		while (viewpoint.x < 600.0f)
		{
			viewpoint.x += 4.0f;
			UpdateTerrainChunks(world, { &viewpoint, 1 }, jobs);
			const TerrainChunksComponent& chunks = world.Get<TerrainChunksComponent>(terrain);
			const auto now = DrawnChunks(chunks);
			Require(now == drawn || chunks.Stats.Swaps > swaps, "The chunks drawn should change only all at once");
			drawn = now;
			swaps = chunks.Stats.Swaps;
			Require(std::chrono::steady_clock::now() - start < std::chrono::seconds(60), "Flying away should take less than a minute");
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		UpdateUntilSettled(world, { &viewpoint, 1 }, jobs, terrain);
		const TerrainChunksComponent& moved = world.Get<TerrainChunksComponent>(terrain);
		for (const TerrainChunk& chunk : moved.Chunks)
		{
			Require(DistanceTo(viewpoint, { chunk.Level, chunk.Key }) <= world.Get<SdfTerrainComponent>(terrain).ViewDistance, "Chunks left far behind should go");
		}
		Require(moved.Stats.Made > madeHere, "Chunks ahead should be made");
		Require(moved.Stats.Swaps > 2, "Flying, the chunks drawn should be replaced many times");

		const uint64_t made = moved.Stats.Made;
		world.Get<SdfTerrainComponent>(terrain).NearShadowLevels = 1;
		UpdateUntilSettled(world, { &viewpoint, 1 }, jobs, terrain);
		Require(world.Get<TerrainChunksComponent>(terrain).Stats.Made == made, "Other shadows alone should make nothing again");
		for (const TerrainChunk& chunk : world.Get<TerrainChunksComponent>(terrain).Chunks)
		{
			Require(chunk.FirstShadowCascade == uint32_t(chunk.Level), "With one near shadow level, a chunk should cast from its level's cascade");
		}
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
		Require(world.Get<TerrainChunksComponent>(terrain).Stats.Resident == before, "Chunks should stay drawn while they are made again");
		UpdateUntilSettled(world, { &viewpoint, 1 }, jobs, terrain);
		const SdfTerrainComponent& values = world.Get<SdfTerrainComponent>(terrain);
		for (const TerrainChunk& chunk : world.Get<TerrainChunksComponent>(terrain).Chunks)
		{
			Require(chunk.Shape == HashSdfTerrainShape(values), "Every chunk should be made from the new values");
			if (chunk.Mesh.Mesh)
			{
				Require(SameMesh(*chunk.Mesh.Mesh, MakeTerrainChunkMesh(values, { chunk.Level, chunk.Key }, chunk.Neighbourhood)), "A chunk made again should be the new values'");
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
		const auto start = std::chrono::steady_clock::now();
		do
		{
			UpdateTerrainChunks(world, { &viewpoint, 1 }, jobs);
			Require(std::chrono::steady_clock::now() - start < std::chrono::seconds(10), "Chunks should be being made");
		} while (world.Get<TerrainChunksComponent>(terrain).Stats.Pending == 0);
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
			Require(DistanceTo(glm::vec3(0.0f, 10.0f, 0.0f), { chunk.Level, chunk.Key }) <= 160.0f, "A moved terrain's chunks should be round it, in its own space");
		}
	}

	// Not a test: how long a view takes to make on all the workers
	void TimeAView()
	{
		Registry world;
		SdfTerrainComponent values = MakeTerrain();
		values.ViewDistance = 1024.0f;
		values.LevelDistance = 4.0f;
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
		const TerrainChunksComponent& chunks = world.Get<TerrainChunksComponent>(terrain);
		const TerrainChunksStats& stats = chunks.Stats;
		std::cout << "An update with nothing to make: " << frameMilliseconds << " ms\n";
		std::cout << "A view of 1024 m: " << stats.Resident << " chunks, " << stats.Skipped << " skipped, " << stats.Triangles << " triangles, made in "
				  << seconds << " s on " << JobSystem::Get().GetWorkerCount() << " workers; chosen in " << stats.LastPlanMilliseconds
				  << " ms; the last chunk sampled in " << stats.LastSampleMilliseconds << " ms, meshed in " << stats.LastMeshMilliseconds << " ms\n";
		for (const InspectedValue& value : DescribeTerrainChunks(chunks))
		{
			std::cout << "  " << value.Name << ": " << value.Value << "\n";
		}
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
	// their vertex colours, out to 2 km at coarser levels, a sun, a camera that flies, and balls that
	// fall on the hills in Play
	SdfTerrainComponent MakeSampleTerrain()
	{
		SdfTerrainComponent terrain;
		terrain.Seed = 7;
		terrain.ViewDistance = 2048.0f;
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

		// Balls above the hills in front of the camera, which fall and roll on them in Play
		for (int n = 0; n < 6; ++n)
		{
			const Entity ball = AddNamed(world, "Ball " + std::to_string(n + 1),
				TransformComponent{ .Position = glm::vec3(-45.0f + float(n % 3) * 6.0f, 45.0f + float(n) * 4.0f, 50.0f - float(n / 3) * 6.0f), .Scale = glm::vec3(2.0f) });
			world.Add<MeshRendererComponent>(ball, MeshRendererComponent{ .Mesh = AssetReference{ .Type = "Mesh", .Path = "Meshes/Sphere.nyxmesh" },
				.Material = AssetReference{ .Type = "Material", .Path = "Materials/Untextured.nyxmat" }, .bVisible = true });
			world.Add<SphereColliderComponent>(ball, SphereColliderComponent{});
			world.Add<RigidBodyComponent>(ball, RigidBodyComponent{});
		}

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
				component.ViewDistance = 300.0f;
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
		// Each line out at once, so a crash leaves what came before it
		std::cout << std::unitbuf;
		Nyx::Logger::Get().Init();
		Nyx::RegisterComponentTypes();
		if (argc > 1 && std::strcmp(argv[1], "--write-scene") == 0)
		{
			WriteSampleScene();
			return 0;
		}

		// --only <name>[,<name>...] runs only those
		const std::vector<std::pair<std::string, void (*)()>> tests{ { "tree", TestLevelTree },
			{ "seams", [] {
				 TestSeamsAreClosed(ESdfTerrainMesher::Dual, 0);
				 TestSeamsAreClosed(ESdfTerrainMesher::MarchingCubes, 0);
				 TestSeamsAreClosed(ESdfTerrainMesher::Dual, -1);
				 TestSeamsAreClosed(ESdfTerrainMesher::MarchingCubes, -1);
			 } },
			{ "viewpoint", TestChunksRoundAViewpoint }, { "moving", TestMovingAndViewDistance }, { "values", TestNewValues },
			{ "removed", TestRemovedEmptyAndMoved }, { "edits", TestEdits }, { "brush", TestBrush }, { "scene", TestTheSampleScene }, { "time", TimeAView } };
		const char* only = argc > 2 && std::strcmp(argv[1], "--only") == 0 ? argv[2] : nullptr;
		for (const auto& [name, test] : tests)
		{
			if (only == nullptr || ("," + std::string(only) + ",").find("," + name + ",") != std::string::npos)
			{
				test();
			}
		}
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All terrain chunk tests passed\n";
	return 0;
}
