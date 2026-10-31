// Gate 2 of the SDF terrain, edits and persistence, measured on the sample terrain in a game world
// with characters, every worker and real time. Prints the report as Markdown, each criterion passed
// or failed with its numbers:
// - Edits: digs of 4 m beside characters and boxes heaped on them, each from its command until it is
//   drawn and in collision; a rift across many chunks the same; how far any character is ever
//   inside the terrain.
// - Jobs held up at random and so finishing out of order: every frame, the finest chunks drawn are
//   those of the edits live, and the collision under each edit is that of the live edits' meshes.
// - Two worlds, a host and machines joining it, their chunks made in other orders: the same region
//   hashes; saved, let go of and loaded, the same hashes and samples, and edited on alike; a
//   planted mismatch found and repaired.
// - Baking: a chunk sampled under more and more brush edits, against none.
// Not a test of ctest's: it takes minutes.
#include "BinaryArchive.h"
#include "CharacterComponent.h"
#include "ComponentRegistration.h"
#include "GameWorld.h"
#include "GuidComponent.h"
#include "JobSystem.h"
#include "Log.h"
#include "Paths.h"
#include "SceneSerializer.h"
#include "SdfTerrain.h"
#include "SdfTerrainEditCommand.h"
#include "TerrainChunks.h"
#include "TerrainSave.h"
#include "TestSupport.h"
#include "TransformComponent.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>

using namespace Nyx;
using Nyx::Tests::Require;

namespace
{
	constexpr float StepSeconds = 1.0f / 60.0f;

	struct Row
	{
		std::string Criterion;
		std::string Measured;
		bool bPassed = false;
	};
	std::vector<Row> Rows;

	void Report(std::string criterion, std::string measured, bool bPassed)
	{
		std::cout << (bPassed ? "PASS " : "FAIL ") << criterion << ": " << measured << "\n";
		Rows.push_back({ std::move(criterion), std::move(measured), bPassed });
	}

	std::string Fixed(double value, int digits = 1)
	{
		std::ostringstream text;
		text << std::fixed << std::setprecision(digits) << value;
		return text.str();
	}

	double Seconds(std::chrono::steady_clock::time_point since)
	{
		return std::chrono::duration<double>(std::chrono::steady_clock::now() - since).count();
	}

	SdfTerrainComponent LoadSampleTerrain()
	{
		Registry scene;
		Require(SceneSerializer::LoadFromFile(Nyx::Paths::GetAssetsDir() / "Scenes/SdfTerrainTest.nyxscene", scene),
			"The sample scene can't be loaded; NyxTerrainChunksTests --write-scene writes it");
		std::optional<SdfTerrainComponent> terrain;
		scene.Each<SdfTerrainComponent>([&](Entity, SdfTerrainComponent& component) { terrain = component; });
		Require(terrain.has_value(), "The sample scene should have a terrain");
		terrain->ViewDistance = 300.0f;
		return *terrain;
	}

	float SurfaceBelow(const SdfTerrainComponent& terrain, const glm::vec3& point)
	{
		const std::optional<TerrainRayHit> hit = RayCastTerrain(terrain, point + glm::vec3(0.0f, 60.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 200.0f);
		Require(hit.has_value(), "There should be ground below the point");
		return hit->Point.y;
	}

	std::optional<float> CollisionBelow(GameWorld& world, const glm::vec3& point)
	{
		QueryFilter filter;
		filter.bMoving = false;
		const std::optional<QueryHit> hit = world.GetPhysics().RayCast(point + glm::vec3(0.0f, 30.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 100.0f, filter);
		return hit ? std::optional<float>(hit->Point.y) : std::nullopt;
	}

	// How far a point is inside the terrain: up, a centimetre at a time, until it is out
	float DepthInside(const SdfTerrainComponent& terrain, const glm::vec3& point)
	{
		float depth = 0.0f;
		while (depth < 2.0f && SampleTerrainValue(terrain, point + glm::vec3(0.0f, depth, 0.0f)) < 0.0f)
		{
			depth += 0.01f;
		}
		return depth;
	}

	uint64_t PackChunk(const TerrainChunkId& id)
	{
		auto part = [](int value) { return uint64_t(uint32_t(value + (1 << 19)) & 0xFFFFFu); };
		return part(id.Key.x) | (part(id.Key.y) << 20) | (part(id.Key.z) << 40) | (uint64_t(uint32_t(id.Level + 8)) << 60);
	}

	// The highest the finest chunks' direct meshes are under a point: what collision made of them is
	std::optional<float> MeshBelow(const SdfTerrainComponent& terrain, const glm::vec3& point)
	{
		const float size = GetTerrainChunkSize(terrain.FinestLevel);
		std::optional<float> best;
		for (int dy = -2; dy <= 1; ++dy)
		{
			const glm::ivec3 key = glm::ivec3(glm::floor(point / size)) + glm::ivec3(0, dy, 0);
			const MeshData mesh = MakeTerrainChunkMesh(terrain, { terrain.FinestLevel, key }, TerrainNeighbourhood{});
			const glm::vec3 corner = glm::vec3(key) * size;
			for (size_t i = 0; i + 2 < mesh.Indices.size(); i += 3)
			{
				const glm::vec3 a = mesh.Vertices[mesh.Indices[i]].Position + corner;
				const glm::vec3 b = mesh.Vertices[mesh.Indices[i + 1]].Position + corner;
				const glm::vec3 c = mesh.Vertices[mesh.Indices[i + 2]].Position + corner;
				// Where the vertical line through the point crosses the triangle, by its barycentric weights in xz
				const float d = (b.z - c.z) * (a.x - c.x) + (c.x - b.x) * (a.z - c.z);
				if (std::fabs(d) < 1e-12f)
				{
					continue;
				}
				const float u = ((b.z - c.z) * (point.x - c.x) + (c.x - b.x) * (point.z - c.z)) / d;
				const float v = ((c.z - a.z) * (point.x - c.x) + (a.x - c.x) * (point.z - c.z)) / d;
				if (u < 0.0f || v < 0.0f || u + v > 1.0f)
				{
					continue;
				}
				const float y = u * a.y + v * b.y + (1.0f - u - v) * c.y;
				if (y <= point.y + 30.0f && (!best || y > *best))
				{
					best = y;
				}
			}
		}
		return best;
	}

	bool SameMesh(const MeshData& a, const MeshData& b)
	{
		if (a.Vertices.size() != b.Vertices.size() || a.Indices != b.Indices)
		{
			return false;
		}
		return std::memcmp(a.Vertices.data(), b.Vertices.data(), a.Vertices.size() * sizeof(Vertex)) == 0;
	}

	// A game world with the sample terrain round four characters, stepped as a game steps it
	struct Play
	{
		GameWorld World;
		Entity Terrain;
		EntityGuid Guid{ 0x6A7E2 };
		std::vector<Entity> Characters;
		glm::vec3 Viewpoint{ 0.0f };
		uint64_t Next = 0;
		float DeepestInside = 0.0f;
		double LongestFrame = 0.0;
		// The edits live as the last step ended: what the bodies were in that step, until the next
		uint64_t LiveAtStep = 0;
		std::chrono::steady_clock::time_point LastFrame = std::chrono::steady_clock::now();

		explicit Play(const SdfTerrainComponent& values)
		{
			Registry& registry = World.GetRegistry();
			Terrain = registry.CreateEntity();
			registry.Add<TransformComponent>(Terrain, TransformComponent{});
			registry.Add<SdfTerrainComponent>(Terrain, values);
			registry.Add<GuidComponent>(Terrain, GuidComponent{ Guid });
			const glm::vec3 middle(-40.0f, 0.0f, 40.0f);
			for (int n = 0; n < 4; ++n)
			{
				glm::vec3 stand = middle + glm::vec3(float(n % 2) * 9.0f, 0.0f, float(n / 2) * 9.0f);
				stand.y = SurfaceBelow(values, stand) + 0.05f;
				const Entity character = registry.CreateEntity();
				registry.Add<TransformComponent>(character, TransformComponent{ .Position = stand });
				registry.Add<CharacterComponent>(character, CharacterComponent{});
				Characters.push_back(character);
			}
			Viewpoint = middle + glm::vec3(4.5f, SurfaceBelow(values, middle) + 3.0f, 4.5f);
		}

		SdfTerrainComponent& Values() { return World.GetRegistry().Get<SdfTerrainComponent>(Terrain); }
		glm::vec3 At(size_t character) { return World.GetRegistry().Get<TransformComponent>(Characters[character]).Position; }

		// A frame: one step, the chunks, no sooner than the clock; then how far in each character is
		void Frame()
		{
			LongestFrame = std::max(LongestFrame, Seconds(LastFrame));
			LastFrame = std::chrono::steady_clock::now();
			const auto due = std::chrono::steady_clock::now() + std::chrono::microseconds(int64_t(StepSeconds * 1e6f));
			// As a game's frame: its step, then its chunks (OnLateUpdate()), then the picture
			World.Step(StepTime{ StepSeconds, Next, double(Next) * StepSeconds });
			++Next;
			LiveAtStep = World.GetRegistry().Get<TerrainEditActivationComponent>(Terrain).Live;
			UpdateTerrainChunks(World.GetRegistry(), { &Viewpoint, 1 }, JobSystem::Get());
			// Pushed out as the edits go live: how far in each is once they are
			for (size_t n = 0; n < Characters.size() && IsLive(); ++n)
			{
				const glm::vec3 feet = At(n);
				for (const float up : { 0.05f, 0.9f, 1.7f })
				{
					DeepestInside = std::max(DeepestInside, DepthInside(Values(), feet + glm::vec3(0.0f, up, 0.0f)));
				}
			}
			while (std::chrono::steady_clock::now() < due)
			{
				std::this_thread::yield();
			}
		}

		bool IsLive()
		{
			const TerrainEditActivationComponent& activation = World.GetRegistry().Get<TerrainEditActivationComponent>(Terrain);
			return activation.Live != 0 && activation.Live == HashTerrainEdits(Values());
		}

		void Settle()
		{
			const auto start = std::chrono::steady_clock::now();
			for (;;)
			{
				Frame();
				const TerrainChunksComponent& chunks = World.GetRegistry().Get<TerrainChunksComponent>(Terrain);
				const TerrainCollisionStats& collision = World.GetRegistry().Get<TerrainCollisionComponent>(Terrain).Stats;
				if (chunks.Stats.bSettled && chunks.Stats.Pending == 0 && collision.Pending == 0 && collision.Resident >= collision.Wanted && IsLive())
				{
					return;
				}
				Require(Seconds(start) < 60.0, "The terrain should settle within a minute");
			}
		}

		void Send(const SdfTerrainEdit& edit)
		{
			SdfTerrainEditCommand command;
			command.Terrain = Guid;
			command.Edit = edit;
			World.GetCommands().Add(command);
		}

		// From the commands to drawn and in collision together: the seconds, at most 10
		double UntilLive()
		{
			const auto start = std::chrono::steady_clock::now();
			Frame();
			while (!IsLive() && Seconds(start) < 10.0)
			{
				Frame();
			}
			return Seconds(start);
		}
	};

	// Digs of 4 m beside the characters and boxes heaped on them, then a rift through them
	void MeasureEdits()
	{
		Play play(LoadSampleTerrain());
		play.Settle();
		std::vector<double> digs;
		std::vector<double> heaps;
		for (int n = 0; n < 8; ++n)
		{
			const glm::vec3 beside = play.At(size_t(n) % 4) + glm::vec3(n % 2 == 0 ? 3.5f : -3.5f, 0.0f, n < 4 ? 2.0f : -2.0f);
			SdfTerrainEdit dig;
			dig.Position = glm::vec3(beside.x, SurfaceBelow(play.Values(), beside), beside.z);
			dig.Radius = 2.0f;
			dig.BlendRadius = 0.3f;
			play.Send(dig);
			digs.push_back(play.UntilLive());
			for (int frame = 0; frame < 10; ++frame)
			{
				play.Frame();
			}
		}
		for (int n = 0; n < 4; ++n)
		{
			SdfTerrainEdit box;
			box.Shape = ESdfTerrainEditShape::Box;
			box.Mode = ESdfTerrainEditMode::Add;
			box.Position = play.At(size_t(n)) + glm::vec3(0.3f, 0.6f, -0.2f);
			box.HalfSize = glm::vec3(1.5f, 1.0f, 1.5f);
			box.Rotation = glm::vec3(0.0f, 20.0f * float(n), 0.0f);
			box.Material = 2;
			play.Send(box);
			heaps.push_back(play.UntilLive());
			for (int frame = 0; frame < 30; ++frame)
			{
				play.Frame();
			}
		}
		const double worstDig = *std::max_element(digs.begin(), digs.end());
		const double worstHeap = *std::max_element(heaps.begin(), heaps.end());
		double sum = 0.0;
		for (const double seconds : digs)
		{
			sum += seconds;
		}
		Report("A dig of 4 m drawn and in collision within 100 ms", "8 digs beside characters: " + Fixed(sum / 8.0 * 1000.0) + " ms on average, " + Fixed(worstDig * 1000.0) +
			" ms at most; 4 boxes heaped on characters: " + Fixed(worstHeap * 1000.0) + " ms at most", std::max(worstDig, worstHeap) <= 0.1);

		// A rift: rods 3 m round from ground to ground, end to end for 120 m through the characters
		const uint64_t madeBefore = play.World.GetRegistry().Get<TerrainChunksComponent>(play.Terrain).Stats.Made;
		std::map<std::tuple<int, int, int, int>, uint64_t> before;
		for (const TerrainChunk& chunk : play.World.GetRegistry().Get<TerrainChunksComponent>(play.Terrain).Chunks)
		{
			before[{ chunk.Level, chunk.Key.x, chunk.Key.y, chunk.Key.z }] = chunk.Edits;
		}
		const glm::vec3 from = play.At(0) + glm::vec3(-60.0f, 0.0f, 4.0f);
		for (int n = 0; n < 6; ++n)
		{
			glm::vec3 a = from + glm::vec3(float(n) * 20.0f, 0.0f, 0.0f);
			glm::vec3 b = a + glm::vec3(20.0f, 0.0f, 0.0f);
			a.y = SurfaceBelow(play.Values(), a);
			b.y = SurfaceBelow(play.Values(), b);
			SdfTerrainEdit rod;
			rod.Shape = ESdfTerrainEditShape::Capsule;
			rod.Position = a;
			rod.End = b;
			rod.Radius = 3.0f;
			rod.BlendRadius = 0.5f;
			play.Send(rod);
		}
		const double rift = play.UntilLive();
		int finest = 0;
		std::map<int, int> byLevel;
		for (const TerrainChunk& chunk : play.World.GetRegistry().Get<TerrainChunksComponent>(play.Terrain).Chunks)
		{
			const auto old = before.find({ chunk.Level, chunk.Key.x, chunk.Key.y, chunk.Key.z });
			const bool bChanged = chunk.bDrawn && (old == before.end() || old->second != chunk.Edits);
			byLevel[chunk.Level] += bChanged ? 1 : 0;
			finest += bChanged && chunk.Level == play.Values().FinestLevel ? 1 : 0;
		}
		std::string levels;
		for (const auto& [level, count] : byLevel)
		{
			levels += (levels.empty() ? "" : ", ") + std::string("level ") + std::to_string(level) + " " + std::to_string(count);
		}
		const uint64_t made = play.World.GetRegistry().Get<TerrainChunksComponent>(play.Terrain).Stats.Made - madeBefore;
		Report("A rift across at least 8 chunks within 1 s", "120 m by 6 m: " + std::to_string(finest) + " of the finest chunks changed (" + std::to_string(made) +
			" chunks made; changed: " + levels + "), drawn and in collision after " + Fixed(rift * 1000.0) + " ms", finest >= 8 && rift <= 1.0);
		for (int frame = 0; frame < 60; ++frame)
		{
			play.Frame();
		}
		Report("No character more than 0.1 m inside the terrain", "4 characters, digs beside them, boxes heaped on them, the rift through them: " +
			Fixed(play.DeepestInside, 2) + " m in at most, any frame", play.DeepestInside <= 0.1f);
	}

	// Jobs held up at random: every frame the finest chunks drawn round the characters are those of the
	// edits live, and the collision under each edit is that of the live edits' meshes
	void MeasureHeldUpJobs()
	{
		SdfTerrainComponent values = LoadSampleTerrain();
		values.ActivationDeadline = 30.0f;
		Play play(values);
		play.Settle();
		std::atomic<uint32_t> seed = 1;
		JobSystem::Get().SetBeforeEachJob([&seed]
			{
				thread_local std::mt19937 random(seed.fetch_add(1));
				std::this_thread::sleep_for(std::chrono::microseconds(std::uniform_int_distribution<int>(0, 15000)(random)));
			});
		std::unordered_map<uint64_t, SdfTerrainComponent> history;
		history[HashTerrainEdits(play.Values())] = play.Values();
		std::vector<glm::vec3> probes;
		int frames = 0;
		int chunksChecked = 0;
		std::map<std::tuple<const MeshData*, uint64_t>, bool> verified;
		std::map<std::pair<uint64_t, size_t>, std::optional<float>> groundOf;
		int groundsChecked = 0;
		int stale = 0;
		int wrongGround = 0;
		std::mt19937 random(7);
		auto check = [&]
		{
			history.try_emplace(HashTerrainEdits(play.Values()), play.Values());
			const TerrainEditActivationComponent& activation = play.World.GetRegistry().Get<TerrainEditActivationComponent>(play.Terrain);
			const auto live = history.find(activation.Live);
			if (live == history.end())
			{
				return;
			}
			++frames;
			for (const TerrainChunk& chunk : play.World.GetRegistry().Get<TerrainChunksComponent>(play.Terrain).Chunks)
			{
				const TerrainChunkId id{ chunk.Level, chunk.Key };
				const glm::vec3 middle = glm::vec3(chunk.Key) * GetTerrainChunkSize(chunk.Level) + GetTerrainChunkSize(chunk.Level) * 0.5f;
				if (!chunk.bDrawn || chunk.Level != play.Values().FinestLevel || glm::length(middle - play.Viewpoint) > 40.0f)
				{
					continue;
				}
				// Each mesh drawn checked once against each set of live edits it is drawn with
				const auto [known, bNew] = verified.try_emplace({ chunk.Mesh.Mesh.get(), activation.Live ^ (uint64_t(PackChunk(id)) * 0x9E3779B97F4A7C15ull) }, true);
				if (!bNew)
				{
					continue;
				}
				const MeshData direct = MakeTerrainChunkMesh(live->second, id, chunk.Neighbourhood);
				++chunksChecked;
				known->second = chunk.Mesh.Mesh ? SameMesh(*chunk.Mesh.Mesh, direct) : direct.Indices.empty();
				stale += known->second ? 0 : 1;
			}
			// Under each edit, the collision the finest meshes of the edits live in the last step make
			// there, once a set: the bodies change only as a step begins
			const auto stepLive = history.find(play.LiveAtStep);
			for (size_t n = 0; n < probes.size() && stepLive != history.end(); ++n)
			{
				const std::optional<float> collision = CollisionBelow(play.World, probes[n]);
				const auto [known, bNew] = groundOf.try_emplace({ play.LiveAtStep, n }, std::nullopt);
				if (bNew)
				{
					known->second = MeshBelow(stepLive->second, probes[n]);
				}
				++groundsChecked;
				wrongGround += collision && known->second && std::fabs(*collision - *known->second) <= 1e-3f ? 0 : 1;
			}
		};
		const auto start = std::chrono::steady_clock::now();
		for (int n = 0; n < 16; ++n)
		{
			// A dig or a heap near the characters every few frames; now and then the latest undone
			if (n % 5 == 4 && play.Values().Edits.size() > 1)
			{
				play.Values().Edits.pop_back();
			}
			else
			{
				std::uniform_real_distribution<float> around(-8.0f, 8.0f);
				glm::vec3 place = play.Viewpoint + glm::vec3(around(random), 0.0f, around(random));
				place.y = SurfaceBelow(play.Values(), place);
				SdfTerrainEdit edit;
				edit.Mode = n % 3 == 2 ? ESdfTerrainEditMode::Add : ESdfTerrainEditMode::Subtract;
				edit.Position = place;
				edit.Radius = 1.5f + float(n % 4) * 0.5f;
				play.Send(edit);
				probes.push_back(place);
			}
			for (int frame = 0; frame < 8; ++frame)
			{
				play.Frame();
				check();
			}
		}
		for (int frame = 0; frame < 600 && !play.IsLive(); ++frame)
		{
			play.Frame();
			check();
		}
		JobSystem::Get().SetBeforeEachJob({});
		const uint32_t late = play.World.GetRegistry().Get<TerrainEditActivationComponent>(play.Terrain).Late;
		Report("With jobs held up and out of order, nothing stale drawn or collided with",
			"jobs held up 0 to 15 ms at random, 16 edits and undos over " + Fixed(Seconds(start), 0) + " s: " + std::to_string(frames) + " frames, " +
				std::to_string(chunksChecked) + " finest chunks checked against the live edits, " + std::to_string(stale) + " not theirs; the collision under the edits " + std::to_string(groundsChecked) + " times, " +
				std::to_string(wrongGround) + " not the live edits' meshes; " + std::to_string(late) + " edits past their deadline; the longest frame " +
				Fixed(play.LongestFrame * 1000.0, 0) + " ms",
			stale == 0 && wrongGround == 0 && frames > 50);
	}

	// Two worlds, saving and loading, a planted mismatch
	void MeasureTwoWorlds()
	{
		JobSystem hostJobs(6);
		JobSystem joinerJobs(3);
		Registry hostWorld;
		Registry joinerWorld;
		SdfTerrainComponent values = LoadSampleTerrain();
		auto add = [](Registry& world, const SdfTerrainComponent& terrain)
		{
			const Entity entity = world.CreateEntity();
			world.Add<TransformComponent>(entity, TransformComponent{});
			world.Add<SdfTerrainComponent>(entity, terrain);
			return entity;
		};
		const Entity hostTerrain = add(hostWorld, values);
		const Entity joinerTerrain = add(joinerWorld, values);
		SdfTerrainComponent& host = hostWorld.Get<SdfTerrainComponent>(hostTerrain);
		SdfTerrainComponent& joiner = joinerWorld.Get<SdfTerrainComponent>(joinerTerrain);
		const glm::vec3 place(-40.0f, SurfaceBelow(values, glm::vec3(-40.0f, 0.0f, 40.0f)), 40.0f);
		const glm::vec3 hostView = place + glm::vec3(0.0f, 4.0f, 0.0f);
		const glm::vec3 joinerView = place + glm::vec3(12.0f, 10.0f, -9.0f);
		TerrainBakeCounts counts;
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
			Require(TakeTerrainEdits(to, edits), "The host's edits should be taken");
		};
		std::mt19937 random(11);
		std::uniform_real_distribution<float> around(-10.0f, 10.0f);
		auto paint = [&](SdfTerrainComponent& terrain, TerrainBakeCounts& bakeCounts, int count)
		{
			for (int n = 0; n < count; ++n)
			{
				SdfTerrainEdit edit;
				edit.Shape = static_cast<ESdfTerrainEditShape>(n % 3);
				edit.Mode = static_cast<ESdfTerrainEditMode>(n % 7 == 6 ? 2 : (n % 4 == 3 ? 1 : 0));
				edit.Position = place + glm::vec3(around(random), around(random) * 0.3f, around(random));
				edit.End = edit.Position + glm::vec3(around(random) * 0.3f, 0.0f, around(random) * 0.3f);
				edit.HalfSize = glm::vec3(1.0f + float(n % 3) * 0.5f);
				edit.Radius = 1.0f + float(n % 4) * 0.5f;
				edit.BlendRadius = n % 2 == 0 ? 0.5f : 0.0f;
				edit.Material = uint32_t(n % 3);
				AppendTerrainEdit(terrain, edit);
				BakeTerrainBricks(terrain, bakeCounts);
			}
		};
		SdfTerrainComponent late;
		for (int round = 0; round < 40; ++round)
		{
			paint(host, counts, 5);
			if (round % 3 == 0)
			{
				send(joiner);
			}
			if (round == 20)
			{
				BinaryWriter checkpoint;
				WriteTerrainCheckpoint(host, checkpoint);
				BinaryReader in;
				in.LoadFromMemory(checkpoint.GetBytes());
				late = values;
				Require(ReadTerrainCheckpoint(in, late).Result == ETerrainCheckpointRead::Read, "A late joiner should read the host's checkpoint");
			}
			UpdateTerrainChunks(hostWorld, { &hostView, 1 }, hostJobs);
			UpdateTerrainChunks(joinerWorld, { &joinerView, 1 }, joinerJobs);
		}
		send(joiner);
		send(late);
		auto settle = [](Registry& world, const glm::vec3& view, JobSystem& jobs, Entity terrain)
		{
			const auto start = std::chrono::steady_clock::now();
			do
			{
				UpdateTerrainChunks(world, { &view, 1 }, jobs);
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
				Require(Seconds(start) < 120.0, "The chunks should settle within two minutes");
			} while (!world.Get<TerrainChunksComponent>(terrain).Stats.bSettled || world.Get<TerrainChunksComponent>(terrain).Stats.Pending > 0);
		};
		settle(hostWorld, hostView, hostJobs, hostTerrain);
		settle(joinerWorld, joinerView, joinerJobs, joinerTerrain);
		const size_t bakes = size_t(std::count_if(host.Edits.begin(), host.Edits.end(), [](const SdfTerrainEdit& edit) { return edit.Kind == ESdfTerrainEditKind::Bake; }));
		const std::vector<TerrainRegionHash> hashes = HashTerrainRegions(host);
		std::map<std::tuple<int, int, int, int>, const TerrainChunk*> hostChunks;
		for (const TerrainChunk& chunk : hostWorld.Get<TerrainChunksComponent>(hostTerrain).Chunks)
		{
			hostChunks[{ chunk.Level, chunk.Key.x, chunk.Key.y, chunk.Key.z }] = &chunk;
		}
		int shared = 0;
		int differ = 0;
		for (const TerrainChunk& chunk : joinerWorld.Get<TerrainChunksComponent>(joinerTerrain).Chunks)
		{
			const auto found = hostChunks.find({ chunk.Level, chunk.Key.x, chunk.Key.y, chunk.Key.z });
			if (found != hostChunks.end() && found->second->Neighbourhood == chunk.Neighbourhood && chunk.Mesh.Mesh && found->second->Mesh.Mesh)
			{
				++shared;
				differ += SameMesh(*chunk.Mesh.Mesh, *found->second->Mesh.Mesh) ? 0 : 1;
			}
		}
		const bool bSame = hashes == HashTerrainRegions(joiner) && hashes == HashTerrainRegions(late);
		Report("Two worlds, chunks made in other orders, with overlapping edits and bakes: the same region hashes",
			std::to_string(host.Edits.size() - bakes) + " edits and " + std::to_string(bakes) + " bakes; a joiner sent them every few rounds making its bakes itself (3 workers, another view), one joining late from a checkpoint: " +
				std::to_string(hashes.size()) + " regions " + (bSame ? "the same" : "NOT the same") + "; " + std::to_string(shared) + " chunks made by both, " + std::to_string(differ) + " different",
			bSame && differ == 0 && shared > 10);

		// Saved, let go of, loaded into a new world, edited on alike
		const std::filesystem::path path = std::filesystem::temp_directory_path() / "NyxTerrainGate2.nyxterrain";
		Require(SaveTerrainCheckpoint(host, path), "The host's terrain should be saved");
		const auto bytes = std::filesystem::file_size(path);
		const FieldGrid before = SampleTerrainGrid(host, place - glm::vec3(16.0f), 0.5f, glm::ivec3(64));
		{
			Registry loadedWorld;
			const Entity loadedTerrain = add(loadedWorld, values);
			SdfTerrainComponent& loaded = loadedWorld.Get<SdfTerrainComponent>(loadedTerrain);
			const auto start = std::chrono::steady_clock::now();
			const TerrainCheckpointRead read = LoadTerrainCheckpoint(path, loaded);
			const double loadSeconds = Seconds(start);
			const bool bLoaded = read.Result == ETerrainCheckpointRead::Read && read.Mismatched.empty() && HashTerrainRegions(loaded) == hashes;
			const FieldGrid after = SampleTerrainGrid(loaded, place - glm::vec3(16.0f), 0.5f, glm::ivec3(64));
			const bool bSameSamples = after.Values == before.Values && after.Materials == before.Materials;

			// Edited on: two loads of the save given the same 30 edits, each deciding its bakes from
			// the load on, alike; the loaded world's chunks the same bits as made directly
			SdfTerrainComponent second = values;
			LoadTerrainCheckpoint(path, second);
			TerrainBakeCounts loadedCounts;
			TerrainBakeCounts secondCounts;
			const std::mt19937 at = random;
			paint(loaded, loadedCounts, 30);
			random = at;
			paint(second, secondCounts, 30);
			settle(loadedWorld, hostView, hostJobs, loadedTerrain);
			int checked = 0;
			int wrong = 0;
			for (const TerrainChunk& chunk : loadedWorld.Get<TerrainChunksComponent>(loadedTerrain).Chunks)
			{
				if (chunk.Level > 0)
				{
					continue;
				}
				const MeshData direct = MakeTerrainChunkMesh(loaded, { chunk.Level, chunk.Key }, chunk.Neighbourhood);
				++checked;
				wrong += (chunk.Mesh.Mesh ? SameMesh(*chunk.Mesh.Mesh, direct) : direct.Indices.empty()) ? 0 : 1;
			}
			const bool bEditedOn = HashTerrainRegions(loaded) == HashTerrainRegions(second) && wrong == 0 && checked > 0;
			Report("A save, unload and load: the same hashes and samples, and edited on",
				std::to_string(bytes / 1024) + " KB saved, loaded in " + Fixed(loadSeconds * 1000.0) + " ms: hashes " + (bLoaded ? "the same" : "NOT the same") + ", samples " +
					(bSameSamples ? "the same bits" : "NOT the same") + "; 30 edits more on two loads, hashes " + (HashTerrainRegions(loaded) == HashTerrainRegions(second) ? "alike" : "NOT alike") +
					", " + std::to_string(checked - wrong) + " of " + std::to_string(checked) + " finest chunks the same bits as made directly",
				bLoaded && bSameSamples && bEditedOn);
		}
		std::filesystem::remove(path);

		// A bake planted with other values in the joiner: found in its region alone, repaired
		size_t bakeAt = joiner.Edits.size();
		while (joiner.Edits[--bakeAt].Kind != ESdfTerrainEditKind::Bake)
		{
		}
		std::array<float, SdfTerrainBakedBrick::Count> other = GetTerrainBakeValues(joiner, bakeAt)->Values;
		other[300] += 0.001f;
		joiner.Edits[bakeAt].Baked = MakeTerrainBakedBrick(other, GetTerrainBakeValues(joiner, bakeAt)->Materials, HashTerrainLayers(joiner));
		const std::vector<glm::ivec3> mismatched = FindTerrainRegionMismatches(hashes, HashTerrainRegions(joiner));
		const glm::ivec3 region(glm::floor(joiner.Edits[bakeAt].Position / TerrainRegionSize));
		BinaryWriter repair;
		WriteTerrainRegionRepair(host, region, repair);
		BinaryReader repairIn;
		repairIn.LoadFromMemory(repair.GetBytes());
		const bool bRepaired = ApplyTerrainRegionRepair(joiner, region, repairIn);
		const bool bSameAfter = HashTerrainRegions(joiner) == hashes;
		Report("A planted mismatch found and repaired",
			"one corner of one bake 1 mm off: " + std::to_string(mismatched.size()) + " region found" + (mismatched.size() == 1 && mismatched[0] == region ? ", its own" : ", NOT its own alone") +
				"; repaired from the host's " + std::to_string(repair.GetBytes().size() / 1024) + " KB of bakes there: " + (bRepaired && bSameAfter ? "the same hashes" : "NOT the same"),
			mismatched.size() == 1 && mismatched[0] == region && bRepaired && bSameAfter);
	}

	// A chunk of the sample terrain under brush edits, on one thread, against none
	void MeasureBaking()
	{
		SdfTerrainComponent terrain = LoadSampleTerrain();
		TerrainBakeCounts counts;
		const glm::vec3 place(-36.0f, SurfaceBelow(terrain, glm::vec3(-36.0f, 0.0f, 44.0f)), 44.0f);
		const TerrainChunkId id{ 0, glm::ivec3(glm::floor(place / GetTerrainChunkSize(0))) };
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
		std::mt19937 random(5);
		std::uniform_real_distribution<float> unit(0.0f, 1.0f);
		size_t shapes = 0;
		std::string measured = "no edits " + Fixed(none, 2) + " ms";
		double worst = 0.0;
		for (const size_t count : { 50u, 200u, 800u, 1600u })
		{
			while (shapes < count)
			{
				const glm::vec3 above = place + glm::vec3((unit(random) - 0.5f) * 24.0f, 60.0f, (unit(random) - 0.5f) * 24.0f);
				const std::optional<TerrainRayHit> hit = RayCastTerrain(terrain, above, glm::vec3(0.0f, -1.0f, 0.0f), 200.0f);
				if (!hit)
				{
					continue;
				}
				SdfTerrainEdit edit;
				edit.Mode = unit(random) < 0.7f ? ESdfTerrainEditMode::Subtract : ESdfTerrainEditMode::Add;
				edit.Position = hit->Point;
				edit.Radius = 1.5f + unit(random) * 1.5f;
				edit.BlendRadius = 0.5f;
				AppendTerrainEdit(terrain, edit);
				BakeTerrainBricks(terrain, counts);
				++shapes;
			}
			const double with = time();
			worst = std::max(worst, with / none);
			measured += "; " + std::to_string(count) + " edits " + Fixed(with, 2) + " ms (" + Fixed(with / none, 2) + " times)";
		}
		Report("Baking keeps sampling a chunk under many edits within 20% of without", "the finest chunk, brush edits within 12 m, one thread: " + measured, worst <= 1.2);
	}
}

int main()
{
	try
	{
		std::cout << std::unitbuf;
		Nyx::Logger::Get().Init();
		Nyx::RegisterComponentTypes();
		const auto start = std::chrono::steady_clock::now();
		MeasureEdits();
		MeasureHeldUpJobs();
		MeasureTwoWorlds();
		MeasureBaking();

		std::cout << "\n# SDF terrain, Gate 2: edits and persistence\n\n";
		std::cout << "Release, " << JobSystem::Get().GetWorkerCount() << " workers, the sample terrain, " << Fixed(Seconds(start), 0) << " s.\n\n";
		std::cout << "| Criterion | Measured | |\n|---|---|---|\n";
		bool bAll = true;
		for (const Row& row : Rows)
		{
			std::cout << "| " << row.Criterion << " | " << row.Measured << " | " << (row.bPassed ? "Passed" : "**Failed**") << " |\n";
			bAll = bAll && row.bPassed;
		}
		std::cout << "\n" << (bAll ? "Every criterion passed." : "Not every criterion passed.") << "\n";
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}
	return 0;
}
