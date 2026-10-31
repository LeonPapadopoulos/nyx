// An SDF terrain's chunks (TerrainChunks.h) without a GPU: the tree of levels round viewpoints,
// covering the view distance without overlap, balanced, each level where its distance says, held
// split by the hysteresis; the chunks of a tree, meshed against their neighbourhoods, meet without a
// gap at every seam between levels, with both meshers and with half-metre cells; made on the
// workers round a viewpoint, each the same bits as the chunk made directly, all within the view
// distance; drawn all at once, a tree at a time; moving away lets them go and makes new ones; new
// values make them again while the old ones stay drawn; another view distance alone makes nothing
// again; edits make only the chunks they reach again, drawn with collision's or at the deadline;
// an undo costing what an edit does; the terrain brush's strokes, rays and ring; two worlds, a
// host and machines joining it, coming to the same region hashes, saved and loaded, a mismatch
// repaired;
// removing the terrain takes them and its jobs away; a terrain with no layers has none; a
// terrain entity moved has its chunks round it; the sample scene's terrain is made. Prints how long
// a view takes to make.
//
// NyxTerrainChunksTests --write-scene writes the sample scene, Assets/Scenes/SdfTerrainTest.nyxscene.
#include "BinaryArchive.h"
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
#include "TerrainSave.h"
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

	// Edits: only the chunks they can change are made again, the others kept as they were, every
	// one the same bits as made directly now; undone, the chunks are as before. With no
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
			// Kept or made again, the same bits as made now
			const TerrainChunkId id{ chunk.Level, chunk.Key };
			const MeshData direct = MakeTerrainChunkMesh(values, id, chunk.Neighbourhood);
			Require(chunk.Mesh.Mesh ? SameMesh(*chunk.Mesh.Mesh, direct) : direct.Indices.empty(), "Every chunk should be the same bits as made now");
			const auto old = before.find({ chunk.Level, chunk.Key.x, chunk.Key.y, chunk.Key.z });
			Require(old != before.end(), "A dig standing still should draw the same chunks");
			if (old->second.first == chunk.Edits)
			{
				Require(old->second.second == chunk.Mesh.Mesh.get(), "A chunk the dig can't change should be kept");
				continue;
			}
			++changed;
		}
		const uint64_t made = chunks.Stats.Made - madeBefore;
		Require(changed > 0 && made == changed, "Only the chunks a dig can change should be made again: " + std::to_string(made) + " made, " + std::to_string(changed) + " changed");
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
			const MeshData direct = MakeTerrainChunkMesh(values, id, chunk.Neighbourhood);
			Require(chunk.Mesh.Mesh ? SameMesh(*chunk.Mesh.Mesh, direct) : direct.Indices.empty(), "After a stroke, every chunk should be the same bits as made directly");
		}
		// Many edits of every shape and mode, some blended, some down by the tunnels' sharp walls, in
		// batches: settled, every chunk kept or made again the same bits as made now
		uint32_t seed = 0x9E3779B9u;
		auto next = [&](float low, float high)
		{
			seed = seed * 1664525u + 1013904223u;
			return low + (high - low) * float(seed >> 8) * (1.0f / 16777216.0f);
		};
		for (int batch = 0; batch < 4; ++batch)
		{
			for (int n = 0; n < 6; ++n)
			{
				SdfTerrainEdit random;
				random.Shape = static_cast<ESdfTerrainEditShape>((batch * 6 + n) % 3);
				random.Mode = static_cast<ESdfTerrainEditMode>((batch * 6 + n) / 3 % 3);
				random.Position = ground->Point + glm::vec3(next(-40.0f, 40.0f), next(-25.0f, 4.0f), next(-40.0f, 40.0f));
				random.End = random.Position + glm::vec3(next(-5.0f, 5.0f), next(-2.0f, 2.0f), next(-5.0f, 5.0f));
				random.HalfSize = glm::vec3(next(0.5f, 3.0f), next(0.5f, 3.0f), next(0.5f, 3.0f));
				random.Rotation = glm::vec3(next(-30.0f, 30.0f), next(0.0f, 360.0f), next(-30.0f, 30.0f));
				random.Radius = next(0.5f, 4.0f);
				random.BlendRadius = n % 2 == 0 ? next(0.2f, 1.5f) : 0.0f;
				random.Material = 1 + uint32_t(n % 3);
				AppendTerrainEdit(world.Get<SdfTerrainComponent>(terrain), random);
			}
			UpdateUntilSettled(world, { &viewpoint, 1 }, jobs, terrain);
			for (const TerrainChunk& chunk : world.Get<TerrainChunksComponent>(terrain).Chunks)
			{
				const MeshData direct = MakeTerrainChunkMesh(values, { chunk.Level, chunk.Key }, chunk.Neighbourhood);
				Require(chunk.Mesh.Mesh ? SameMesh(*chunk.Mesh.Mesh, direct) : direct.Indices.empty(), "After many edits, every chunk should be the same bits as made directly");
			}
		}

		// Painting in one place, baked as it goes: settled, every chunk the same bits as made directly;
		// the bakes' values lost (as loading or an undo leaves them), the same, and made again; undone
		// to before the painting, the same
		const size_t beforePainting = values.Edits.size();
		TerrainBakeCounts counts;
		for (int n = 0; n < 40; ++n)
		{
			SdfTerrainEdit dab = dig;
			dab.Position += glm::vec3(next(-3.0f, 3.0f), next(-1.0f, 1.0f), next(-3.0f, 3.0f));
			dab.Radius = next(1.0f, 2.0f);
			dab.BlendRadius = 0.5f;
			dab.Mode = n % 4 == 3 ? ESdfTerrainEditMode::Add : ESdfTerrainEditMode::Subtract;
			AppendTerrainEdit(world.Get<SdfTerrainComponent>(terrain), dab);
			BakeTerrainBricks(world.Get<SdfTerrainComponent>(terrain), counts);
			if (n % 8 == 0)
			{
				UpdateTerrainChunks(world, { &viewpoint, 1 }, jobs);
			}
		}
		const size_t painted = size_t(std::count_if(values.Edits.begin(), values.Edits.end(), [](const SdfTerrainEdit& edit) { return edit.Kind == ESdfTerrainEditKind::Bake; }));
		Require(painted > 0, "Painting in one place should bake bricks there");
		auto sameAsDirect = [&](const char* what)
		{
			UpdateUntilSettled(world, { &viewpoint, 1 }, jobs, terrain);
			for (const TerrainChunk& chunk : world.Get<TerrainChunksComponent>(terrain).Chunks)
			{
				const MeshData direct = MakeTerrainChunkMesh(values, { chunk.Level, chunk.Key }, chunk.Neighbourhood);
				Require(chunk.Mesh.Mesh ? SameMesh(*chunk.Mesh.Mesh, direct) : direct.Indices.empty(), what);
			}
		};
		sameAsDirect("Painted and baked, every chunk should be the same bits as made directly");
		const uint64_t madeBeforeLoss = world.Get<TerrainChunksComponent>(terrain).Stats.Made;
		for (SdfTerrainEdit& edit : world.Get<SdfTerrainComponent>(terrain).Edits)
		{
			edit.Baked.reset();
		}
		sameAsDirect("Its bakes' values lost, every chunk should be the same bits as made directly");
		Require(world.Get<TerrainChunksComponent>(terrain).Stats.Made == madeBeforeLoss, "Bakes losing their values should make no chunk again");
		Require(std::all_of(values.Edits.begin(), values.Edits.end(), [](const SdfTerrainEdit& edit) { return edit.Kind != ESdfTerrainEditKind::Bake || edit.Baked; }),
			"The bakes' values should be made again");
		world.Get<SdfTerrainComponent>(terrain).Edits.resize(beforePainting);
		sameAsDirect("The painting undone, every chunk should be the same bits as made directly");
		std::cout << "Painting: 40 edits, " << painted << " bakes\n";
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

		// Rays over hills dug and heaped 300 times meet them where a ray through every edit does
		SdfTerrainComponent& hills = world.Get<SdfTerrainComponent>(high);
		hills = MakeTerrain();
		uint32_t seed = 0x2545F491u;
		auto next = [&](float low, float high)
		{
			seed = seed * 1664525u + 1013904223u;
			return low + (high - low) * float(seed >> 8) * (1.0f / 16777216.0f);
		};
		for (int n = 0; n < 300; ++n)
		{
			SdfTerrainEdit edit;
			edit.Mode = n % 3 == 0 ? ESdfTerrainEditMode::Add : ESdfTerrainEditMode::Subtract;
			edit.Position = glm::vec3(next(-120.0f, 120.0f), next(-12.0f, 12.0f), next(-120.0f, 120.0f));
			edit.Radius = next(1.0f, 4.0f);
			AppendTerrainEdit(hills, edit);
		}
		double brushMs = 0.0;
		double wholeMs = 0.0;
		int met = 0;
		for (int n = 0; n < 40; ++n)
		{
			const glm::vec3 from = glm::vec3(next(-100.0f, 100.0f), 60.0f, next(-100.0f, 100.0f)) + glm::vec3(0.0f, 20.0f, 0.0f);
			const glm::vec3 to = glm::vec3(next(-100.0f, 100.0f), -5.0f, next(-100.0f, 100.0f)) + glm::vec3(0.0f, 20.0f, 0.0f);
			const Ray ray{ from, glm::normalize(to - from) };
			auto clock = std::chrono::steady_clock::now();
			const std::optional<TerrainBrushHit> brush = CastTerrainBrush(world, ray, 1000.0f, high);
			brushMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - clock).count();
			clock = std::chrono::steady_clock::now();
			const std::optional<TerrainRayHit> whole = RayCastTerrain(hills, ray.Origin - glm::vec3(0.0f, 20.0f, 0.0f), ray.Direction, 1000.0f);
			wholeMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - clock).count();
			Require(brush.has_value() == whole.has_value(), "A ray past many edits should meet the terrain where a ray through them all does");
			if (brush)
			{
				Require(std::fabs(brush->Distance - whole->Distance) < 0.02f, "A ray past many edits should meet it at the same place, not " +
					std::to_string(brush->Distance) + " m against " + std::to_string(whole->Distance));
				++met;
			}
		}
		Require(met > 30, "Most rays should meet the hills");
		std::cout << "Brush rays past 300 edits: " << std::fixed << std::setprecision(2) << brushMs / 40.0 << " ms a ray, through every edit "
				  << wholeMs / 40.0 << " ms\n";
	}

	// The longest a frame took on the main thread until the chunks settled, in milliseconds
	double LongestFrameUntilSettled(Registry& world, std::span<const glm::vec3> viewpoints, JobSystem& jobs, Entity terrain)
	{
		const auto start = std::chrono::steady_clock::now();
		double longest = 0.0;
		for (;;)
		{
			const auto frame = std::chrono::steady_clock::now();
			UpdateTerrainChunks(world, viewpoints, jobs);
			longest = std::max(longest, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frame).count());
			const TerrainChunksStats& stats = world.Get<TerrainChunksComponent>(terrain).Stats;
			if (stats.bSettled && stats.Pending == 0)
			{
				return longest;
			}
			Require(std::chrono::steady_clock::now() - start < std::chrono::seconds(60), "The chunks should settle within a minute");
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}

	// Undo costs what an edit does: with 400 edits on the hills, undoing the last, or one in the
	// middle, takes the main thread no longer than making one; after each, every chunk the same bits
	// as made now
	void TestUndoCost()
	{
		JobSystem jobs(4);
		Registry world;
		const Entity terrain = AddTerrain(world, MakeTerrain());
		const glm::vec3 viewpoint(5.0f, 12.0f, -3.0f);
		uint32_t seed = 0x51ED270Bu;
		auto next = [&](float low, float high)
		{
			seed = seed * 1664525u + 1013904223u;
			return low + (high - low) * float(seed >> 8) * (1.0f / 16777216.0f);
		};
		for (int n = 0; n < 400; ++n)
		{
			SdfTerrainEdit edit;
			edit.Mode = n % 4 == 0 ? ESdfTerrainEditMode::Add : ESdfTerrainEditMode::Subtract;
			edit.Position = glm::vec3(next(-150.0f, 150.0f), next(-8.0f, 10.0f), next(-150.0f, 150.0f));
			edit.Radius = next(1.0f, 3.0f);
			AppendTerrainEdit(world.Get<SdfTerrainComponent>(terrain), edit);
		}
		UpdateUntilSettled(world, { &viewpoint, 1 }, jobs, terrain);
		auto requireAsMadeNow = [&](const char* when)
		{
			const SdfTerrainComponent& values = world.Get<SdfTerrainComponent>(terrain);
			for (const TerrainChunk& chunk : world.Get<TerrainChunksComponent>(terrain).Chunks)
			{
				const MeshData direct = MakeTerrainChunkMesh(values, { chunk.Level, chunk.Key }, chunk.Neighbourhood);
				Require(chunk.Mesh.Mesh ? SameMesh(*chunk.Mesh.Mesh, direct) : direct.Indices.empty(), std::string(when) + ", every chunk should be the same bits as made now");
			}
		};

		SdfTerrainEdit dig;
		dig.Position = glm::vec3(8.0f, 0.0f, -1.0f);
		dig.Radius = 2.5f;
		AppendTerrainEdit(world.Get<SdfTerrainComponent>(terrain), dig);
		const double edited = LongestFrameUntilSettled(world, { &viewpoint, 1 }, jobs, terrain);
		requireAsMadeNow("After an edit");

		world.Get<SdfTerrainComponent>(terrain).Edits.pop_back();
		const double undone = LongestFrameUntilSettled(world, { &viewpoint, 1 }, jobs, terrain);
		requireAsMadeNow("After an undo");

		std::vector<SdfTerrainEdit>& edits = world.Get<SdfTerrainComponent>(terrain).Edits;
		edits.erase(edits.begin() + 200);
		const double middle = LongestFrameUntilSettled(world, { &viewpoint, 1 }, jobs, terrain);
		requireAsMadeNow("After an edit in the middle taken away");

		Require(undone < std::max(4.0, 3.0 * edited) && middle < std::max(8.0, 6.0 * edited),
			"An undo should cost the main thread about what an edit does, not " + std::to_string(undone) + " and " + std::to_string(middle) + " ms against " +
				std::to_string(edited));
		std::cout << "With 400 edits, the longest frame: " << std::fixed << std::setprecision(2) << edited << " ms for an edit, " << undone
				  << " ms for its undo, " << middle << " ms for one in the middle taken away\n";
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

	// Not a test: digs in the sample scene as the editor makes them, on every worker at 60 frames a
	// second, where its camera looks; how long each takes to be drawn, and where the time goes
	void TimeAnEdit()
	{
		Registry world;
		Require(SceneSerializer::LoadFromFile(Nyx::Paths::GetAssetsDir() / SampleScenePath, world),
			"The sample scene can't be loaded; NyxTerrainChunksTests --write-scene writes it");
		Entity terrain;
		world.Each<SdfTerrainComponent>([&](Entity entity, SdfTerrainComponent&) { terrain = entity; });
		TransformComponent camera;
		world.Each<CameraComponent>([&](Entity entity, CameraComponent&) { camera = world.Get<TransformComponent>(entity); });
		const glm::vec3 viewpoint = camera.Position;
		JobSystem& jobs = JobSystem::Get();
		UpdateUntilSettled(world, { &viewpoint, 1 }, jobs, terrain);

		const glm::vec3 forward = camera.Rotation * glm::vec3(0.0f, 0.0f, -1.0f);
		for (int dig = 0; dig < 6; ++dig)
		{
			const glm::vec3 aim = glm::normalize(forward + glm::vec3(0.04f * float(dig - 3), -0.15f, 0.0f));
			const std::optional<TerrainBrushHit> hit = CastTerrainBrush(world, Ray{ viewpoint, aim }, 2000.0f, terrain);
			Require(hit.has_value(), "The camera should look at the terrain");
			const TerrainChunksStats before = world.Get<TerrainChunksComponent>(terrain).Stats;
			SdfTerrainEdit edit;
			edit.Position = hit->LocalPoint;
			edit.Radius = 2.0f;
			edit.BlendRadius = 0.5f;
			AppendTerrainEdit(world.Get<SdfTerrainComponent>(terrain), edit);

			const auto start = std::chrono::steady_clock::now();
			int frames = 0;
			double longest = 0.0;
			double planned = -1.0;
			for (;;)
			{
				const auto due = std::chrono::steady_clock::now() + std::chrono::microseconds(16667);
				const auto frame = std::chrono::steady_clock::now();
				UpdateTerrainChunks(world, { &viewpoint, 1 }, jobs);
				longest = std::max(longest, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frame).count());
				++frames;
				const TerrainChunksStats& stats = world.Get<TerrainChunksComponent>(terrain).Stats;
				if (planned < 0.0 && (stats.Pending > 0 || stats.Swaps > before.Swaps))
				{
					planned = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
				}
				if (stats.bSettled && stats.Pending == 0)
				{
					break;
				}
				while (std::chrono::steady_clock::now() < due)
				{
					std::this_thread::yield();
				}
			}
			const double drawn = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
			const TerrainChunksComponent& chunks = world.Get<TerrainChunksComponent>(terrain);
			std::cout << std::fixed << std::setprecision(1) << "A dig " << glm::length(hit->Point - viewpoint) << " m away: drawn after " << drawn << " ms, "
					  << frames << " frames; " << (chunks.Stats.Made - before.Made) << " chunks made, jobs begun after " << planned
					  << " ms; the last chunk sampled in " << chunks.Stats.LastSampleMilliseconds << " ms, meshed in " << chunks.Stats.LastMeshMilliseconds
					  << " ms; the longest frame " << longest << " ms; finest level " << world.Get<SdfTerrainComponent>(terrain).FinestLevel << "\n";
		}
	}

	// Two worlds in one process, a host and a machine that joined it, their chunks made in different
	// orders (other viewpoints, other numbers of workers) while digs, heaps and paint overlap and the
	// host bakes: the joiner taking the host's edits as they come and making its bakes' values
	// itself, a second one joining late from a checkpoint; settled, all three the same region hashes,
	// and the chunks both have the same bits. Saved, the worlds let go and the save loaded into a new
	// one: the same hashes and samples. A bake planted with other values in one region: found there
	// alone, and repaired from the host's.
	void TestTwoWorlds()
	{
		JobSystem hostJobs(4);
		JobSystem joinerJobs(2);
		Registry hostWorld;
		Registry joinerWorld;
		const Entity hostTerrain = AddTerrain(hostWorld, MakeTerrain());
		const Entity joinerTerrain = AddTerrain(joinerWorld, MakeTerrain());
		const glm::vec3 hostView(5.0f, 12.0f, -3.0f);
		const glm::vec3 joinerView(14.0f, 16.0f, 4.0f);
		SdfTerrainComponent& host = hostWorld.Get<SdfTerrainComponent>(hostTerrain);
		SdfTerrainComponent& joiner = joinerWorld.Get<SdfTerrainComponent>(joinerTerrain);
		host.BakeAfter = 6;
		TerrainBakeCounts counts;
		const std::optional<TerrainRayHit> ground = RayCastTerrain(host, hostView, glm::vec3(0.0f, -1.0f, 0.0f), 100.0f);
		Require(ground.has_value(), "There should be ground under the host's viewpoint");

		// What the joiner is sent: the host's edits after its own latest, as bytes, read back without
		// the bakes' values so it makes them itself
		auto send = [&](SdfTerrainComponent& to)
		{
			BinaryWriter bytes;
			WriteTerrainEdits(host, bytes, GetTerrainRevision(to));
			BinaryReader in;
			in.LoadFromMemory(bytes.GetBytes());
			std::vector<SdfTerrainEdit> edits;
			Require(ReadTerrainEdits(in, edits), "The host's edits should be read");
			for (SdfTerrainEdit& edit : edits)
			{
				edit.Baked.reset();
			}
			Require(TakeTerrainEdits(to, edits), "The host's edits should be taken after the joiner's own");
		};
		uint32_t seed = 0x1234567u;
		auto next = [&](float low, float high)
		{
			seed = seed * 1664525u + 1013904223u;
			return low + (high - low) * float(seed >> 8) * (1.0f / 16777216.0f);
		};
		SdfTerrainComponent late;
		for (int round = 0; round < 30; ++round)
		{
			for (int n = 0; n < 3; ++n)
			{
				SdfTerrainEdit edit;
				edit.Shape = static_cast<ESdfTerrainEditShape>((round + n) % 3);
				edit.Mode = static_cast<ESdfTerrainEditMode>((round * 3 + n) % 5 == 4 ? 2 : ((round * 3 + n) % 5 == 3 ? 1 : 0));
				edit.Position = ground->Point + glm::vec3(next(-8.0f, 8.0f), next(-3.0f, 2.0f), next(-8.0f, 8.0f));
				edit.End = edit.Position + glm::vec3(next(-3.0f, 3.0f), 0.0f, next(-3.0f, 3.0f));
				edit.HalfSize = glm::vec3(next(0.5f, 2.0f));
				edit.Radius = next(1.0f, 2.5f);
				edit.BlendRadius = n == 0 ? 0.5f : 0.0f;
				edit.Material = 1 + uint32_t(n);
				AppendTerrainEdit(host, edit);
				BakeTerrainBricks(host, counts);
			}
			if (round % 4 == 0)
			{
				send(joiner);
			}
			if (round == 15)
			{
				// Joining late, from a checkpoint
				BinaryWriter checkpoint;
				WriteTerrainCheckpoint(host, checkpoint);
				BinaryReader in;
				in.LoadFromMemory(checkpoint.GetBytes());
				late = MakeTerrain();
				Require(ReadTerrainCheckpoint(in, late).Result == ETerrainCheckpointRead::Read, "A late joiner should read the host's checkpoint");
			}
			UpdateTerrainChunks(hostWorld, { &hostView, 1 }, hostJobs);
			UpdateTerrainChunks(joinerWorld, { &joinerView, 1 }, joinerJobs);
		}
		send(joiner);
		send(late);
		UpdateUntilSettled(hostWorld, { &hostView, 1 }, hostJobs, hostTerrain);
		UpdateUntilSettled(joinerWorld, { &joinerView, 1 }, joinerJobs, joinerTerrain);
		const size_t bakes = size_t(std::count_if(host.Edits.begin(), host.Edits.end(), [](const SdfTerrainEdit& edit) { return edit.Kind == ESdfTerrainEditKind::Bake; }));
		Require(bakes > 0, "The host should have baked where the edits overlap");
		const std::vector<TerrainRegionHash> hashes = HashTerrainRegions(host);
		Require(hashes == HashTerrainRegions(joiner) && hashes == HashTerrainRegions(late), "Both joiners should come to the host's region hashes");

		// The chunks both made, the same bits
		std::map<std::tuple<int, int, int, int>, const TerrainChunk*> hostChunks;
		for (const TerrainChunk& chunk : hostWorld.Get<TerrainChunksComponent>(hostTerrain).Chunks)
		{
			hostChunks[{ chunk.Level, chunk.Key.x, chunk.Key.y, chunk.Key.z }] = &chunk;
		}
		int shared = 0;
		for (const TerrainChunk& chunk : joinerWorld.Get<TerrainChunksComponent>(joinerTerrain).Chunks)
		{
			const auto found = hostChunks.find({ chunk.Level, chunk.Key.x, chunk.Key.y, chunk.Key.z });
			if (found == hostChunks.end() || found->second->Neighbourhood != chunk.Neighbourhood || !chunk.Mesh.Mesh || !found->second->Mesh.Mesh)
			{
				continue;
			}
			Require(SameMesh(*chunk.Mesh.Mesh, *found->second->Mesh.Mesh), "A chunk both worlds made should be the same bits");
			++shared;
		}
		Require(shared > 10, "The worlds should share many chunks, not " + std::to_string(shared));

		// Saved, let go of, loaded into a new world
		const std::filesystem::path path = std::filesystem::temp_directory_path() / "NyxTerrainChunksTests.nyxterrain";
		Require(SaveTerrainCheckpoint(host, path), "The host's terrain should be saved");
		const FieldGrid before = SampleTerrainGrid(host, ground->Point - glm::vec3(12.0f), 0.5f, glm::ivec3(48));
		{
			Registry loadedWorld;
			const Entity loadedTerrain = AddTerrain(loadedWorld, MakeTerrain());
			SdfTerrainComponent& loaded = loadedWorld.Get<SdfTerrainComponent>(loadedTerrain);
			const TerrainCheckpointRead read = LoadTerrainCheckpoint(path, loaded);
			Require(read.Result == ETerrainCheckpointRead::Read && read.Mismatched.empty() && HashTerrainRegions(loaded) == hashes, "The save loaded should have the same region hashes");
			const FieldGrid after = SampleTerrainGrid(loaded, ground->Point - glm::vec3(12.0f), 0.5f, glm::ivec3(48));
			Require(after.Values == before.Values && after.Materials == before.Materials, "The save loaded should sample the same bits");
			UpdateUntilSettled(loadedWorld, { &hostView, 1 }, hostJobs, loadedTerrain);
		}
		std::filesystem::remove(path);

		// A bake planted with other values: found in its region alone, repaired from the host's
		size_t bakeAt = joiner.Edits.size();
		while (joiner.Edits[--bakeAt].Kind != ESdfTerrainEditKind::Bake)
		{
		}
		std::array<float, SdfTerrainBakedBrick::Count> other = GetTerrainBakeValues(joiner, bakeAt)->Values;
		other[123] += 0.02f;
		joiner.Edits[bakeAt].Baked = MakeTerrainBakedBrick(other, GetTerrainBakeValues(joiner, bakeAt)->Materials, HashTerrainLayers(joiner));
		const std::vector<glm::ivec3> mismatched = FindTerrainRegionMismatches(hashes, HashTerrainRegions(joiner));
		const glm::ivec3 region(glm::floor(joiner.Edits[bakeAt].Position / TerrainRegionSize));
		Require(mismatched.size() == 1 && mismatched[0] == region, "A planted mismatch should be found in its region alone");
		BinaryWriter repair;
		WriteTerrainRegionRepair(host, region, repair);
		BinaryReader repairIn;
		repairIn.LoadFromMemory(repair.GetBytes());
		Require(ApplyTerrainRegionRepair(joiner, region, repairIn) && HashTerrainRegions(joiner) == hashes, "The mismatch should be repaired from the host's bakes");
		std::cout << "Two worlds: " << host.Edits.size() << " edits, " << bakes << " bakes, " << hashes.size() << " regions the same, " << shared
				  << " chunks made by both the same bits; saved, loaded and a planted mismatch repaired\n";
	}

	SdfTerrainComponent MakeSampleTerrain();

	// Not a test: how long the finest chunk at a place in the sample terrain takes to sample, on one
	// thread, under more and more brush edits there (digs and heaps where a ray down meets the
	// ground, within 12 m), against none; with and without baking
	void TimeSamplingUnderEdits(uint32_t bakeAfter)
	{
		SdfTerrainComponent terrain = MakeSampleTerrain();
		terrain.BakeAfter = bakeAfter;
		TerrainBakeCounts counts;
		const std::optional<TerrainRayHit> ground = RayCastTerrain(terrain, glm::vec3(-36.0f, 200.0f, 44.0f), glm::vec3(0.0f, -1.0f, 0.0f), 400.0f);
		Require(ground.has_value(), "The sample terrain should have ground there");
		const TerrainChunkId id{ 0, glm::ivec3(glm::floor(ground->Point / GetTerrainChunkSize(0))) };
		auto time = [&]
		{
			double best = 1e9;
			for (int run = 0; run < 5; ++run)
			{
				TerrainChunkTimes times;
				MakeTerrainChunkMesh(terrain, id, TerrainNeighbourhood{}, &times);
				best = std::min(best, times.SampleMilliseconds);
			}
			return best;
		};
		const double none = time();
		std::cout << std::fixed << std::setprecision(2) << "Bake After " << bakeAfter << ": a chunk sampled in " << none << " ms with no edits\n";
		uint32_t seed = 12345u;
		auto random = [&]
		{
			seed = seed * 1664525u + 1013904223u;
			return float(seed >> 8) / float(1u << 24);
		};
		size_t shapes = 0;
		for (const size_t count : { 50u, 200u, 800u, 1600u })
		{
			double longest = 0.0;
			while (shapes < count)
			{
				const glm::vec3 above = ground->Point + glm::vec3((random() - 0.5f) * 24.0f, 60.0f, (random() - 0.5f) * 24.0f);
				const std::optional<TerrainRayHit> hit = RayCastTerrain(terrain, above, glm::vec3(0.0f, -1.0f, 0.0f), 200.0f);
				if (!hit)
				{
					continue;
				}
				SdfTerrainEdit edit;
				edit.Mode = random() < 0.7f ? ESdfTerrainEditMode::Subtract : ESdfTerrainEditMode::Add;
				edit.Position = hit->Point;
				edit.Radius = 1.5f + random() * 1.5f;
				edit.BlendRadius = 0.5f;
				const auto start = std::chrono::steady_clock::now();
				AppendTerrainEdit(terrain, edit);
				BakeTerrainBricks(terrain, counts);
				longest = std::max(longest, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
				++shapes;
			}
			const double with = time();
			const size_t bakes = size_t(std::count_if(terrain.Edits.begin(), terrain.Edits.end(), [](const SdfTerrainEdit& made) { return made.Kind == ESdfTerrainEditKind::Bake; }));
			std::cout << "  under " << count << " edits: " << with << " ms, " << (with / none) << " times; " << bakes << " bakes, an edit and its bakes made in "
					  << longest << " ms at most\n";
		}
	}

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
			{ "removed", TestRemovedEmptyAndMoved }, { "edits", TestEdits }, { "undo", TestUndoCost }, { "brush", TestBrush }, { "twoworlds", TestTwoWorlds }, { "scene", TestTheSampleScene }, { "time", TimeAView } };
		// Not run unless asked for: a measure of the sample scene, not a check
		const std::vector<std::pair<std::string, void (*)()>> measures{ { "editdelay", TimeAnEdit }, { "editcost", [] { TimeSamplingUnderEdits(0); TimeSamplingUnderEdits(16); } } };
		const char* only = argc > 2 && std::strcmp(argv[1], "--only") == 0 ? argv[2] : nullptr;
		for (const auto& [name, test] : tests)
		{
			if (only == nullptr || ("," + std::string(only) + ",").find("," + name + ",") != std::string::npos)
			{
				test();
			}
		}
		for (const auto& [name, measure] : measures)
		{
			if (only != nullptr && ("," + std::string(only) + ",").find("," + name + ",") != std::string::npos)
			{
				measure();
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
