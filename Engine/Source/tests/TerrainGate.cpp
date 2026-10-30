// Gate 1 of the SDF terrain, measured on the CPU (no GPU; NyxTerrainGateGpu has the GPU's numbers):
// the test volume (TerrainGateVolume.h) with each mesher, at 1 m and at half-metre cells. Prints the
// report as Markdown, each criterion passed or failed with its numbers:
// - Field: no skipped chunk where dense sampling (every quarter cell) finds the surface, and how
//   many chunks sampled hold it; rays within a centimetre of dense marching; none through the 2 m
//   wall.
// - Meshes: a flythrough from high above, along the features and under and over the landmass,
//   stopping at each place until its chunks are in: no open edge, how many edges four triangles
//   share, triangles wound against their neighbours, triangles facing against the field's gradient.
// - Features: each, at every level, there or not, where its two-cell limit says it must be.
// - Collision: characters walking the tunnel, the arch, between the columns and into the cave,
//   stopped by the wall, standing on the ledge and on the landmass, and one falling past the
//   landmass's edge at 60 m/s, never without collision.
// - Performance: the main thread's terrain work each frame of the flythrough, a chunk on one
//   thread, a full view after a jump; the memory the drawn meshes take.
// Not a test of ctest's: it takes minutes.
//
// NyxTerrainGate --write-scene writes Assets/Scenes/SdfTerrainGate.nyxscene, the volume with a
// character to walk it by hand. --only <dual|marching>[,<0|-1>] runs one configuration.
#include "CameraComponent.h"
#include "CharacterComponent.h"
#include "ComponentRegistration.h"
#include "DirectionalLightComponent.h"
#include "GameWorld.h"
#include "GuidComponent.h"
#include "JobSystem.h"
#include "Log.h"
#include "MeshRendererComponent.h"
#include "NameComponent.h"
#include "OrbitCameraComponent.h"
#include "Paths.h"
#include "SceneSerializer.h"
#include "SdfTerrain.h"
#include "TerrainChunks.h"
#include "TerrainGateVolume.h"
#include "TestSupport.h"
#include "TransformComponent.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace
{
	using namespace Nyx;
	using namespace Nyx::TerrainGate;
	using Clock = std::chrono::steady_clock;

	double MillisecondsSince(Clock::time_point start)
	{
		return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
	}

	std::string Fixed(double value, int digits = 2)
	{
		std::ostringstream text;
		text << std::fixed << std::setprecision(digits) << value;
		return text.str();
	}

	std::string Describe(const glm::vec3& v)
	{
		return "(" + Fixed(v.x, 1) + ", " + Fixed(v.y, 1) + ", " + Fixed(v.z, 1) + ")";
	}

	struct Config
	{
		ESdfTerrainMesher Mesher = ESdfTerrainMesher::Dual;
		int FinestLevel = 0;

		std::string Name() const { return std::string(Mesher == ESdfTerrainMesher::Dual ? "Dual" : "Marching cubes") + (FinestLevel < 0 ? ", 0.5 m" : ", 1 m"); }
	};

	SdfTerrainComponent MakeTerrain(const Config& config)
	{
		SdfTerrainComponent terrain = MakeVolume();
		terrain.Mesher = config.Mesher;
		terrain.FinestLevel = config.FinestLevel;
		return terrain;
	}

	// What the report says about one criterion
	struct Line
	{
		std::string Criterion;
		bool bPassed = true;
		std::string Numbers;
	};

	struct Report
	{
		std::vector<Line> Lines;
		std::vector<std::string> Tables;

		void Add(const std::string& criterion, bool bPassed, const std::string& numbers)
		{
			Lines.push_back({ criterion, bPassed, numbers });
			std::cout << (bPassed ? "  pass  " : "  FAIL  ") << criterion << ": " << numbers << std::endl;
		}
	};

	// Runs `count` pieces of work on the program's workers and waits for them
	template <typename Work>
	void ForEachOnWorkers(size_t count, Work&& work)
	{
		JobGroup group;
		for (size_t index = 0; index < count; ++index)
		{
			JobSystem::Get().Schedule([&work, index] { work(index); }, EJobPriority::Normal, &group);
		}
		JobSystem::Get().Wait(group);
	}

	glm::vec3 CornerOf(const TerrainChunkId& id)
	{
		return glm::vec3(id.Key) * GetTerrainChunkSize(id.Level);
	}

	// The chunks of a level whose boxes meet a box
	std::vector<TerrainChunkId> ChunksIn(int level, const glm::vec3& low, const glm::vec3& high)
	{
		std::vector<TerrainChunkId> chunks;
		const float size = GetTerrainChunkSize(level);
		const glm::ivec3 from(glm::floor(low / size));
		const glm::ivec3 to(glm::floor(high / size));
		for (int z = from.z; z <= to.z; ++z)
		{
			for (int y = from.y; y <= to.y; ++y)
			{
				for (int x = from.x; x <= to.x; ++x)
				{
					chunks.push_back({ level, glm::ivec3(x, y, z) });
				}
			}
		}
		return chunks;
	}

	// The test volume's chunks: round the features, and round the landmass
	std::vector<TerrainChunkId> VolumeChunks(int level)
	{
		// From deep in the ground to high in the air, so some are all solid and some all air
		std::vector<TerrainChunkId> chunks = ChunksIn(level, glm::vec3(-100.0f, -70.0f, -24.0f), glm::vec3(72.0f, 130.0f, 24.0f));
		const std::vector<TerrainChunkId> island = ChunksIn(level, glm::vec3(IslandX - 40.0f, IslandTop - 50.0f, IslandZ - 40.0f), glm::vec3(IslandX + 40.0f, IslandTop + 8.0f, IslandZ + 40.0f));
		chunks.insert(chunks.end(), island.begin(), island.end());
		return chunks;
	}

	bool IsSkipped(const SdfTerrainComponent& terrain, const TerrainChunkId& id)
	{
		const float cell = GetTerrainCellSize(id.Level);
		const TerrainValueRange range = GetTerrainValueRange(terrain, CornerOf(id) - 2.0f * cell, CornerOf(id) + GetTerrainChunkSize(id.Level) + cell);
		return range.IsAllAir() || range.IsAllSolid();
	}

	// Whether sampling a chunk's samples' box every quarter cell finds both sides of the surface
	bool DenseFindsSurface(const SdfTerrainComponent& terrain, const TerrainChunkId& id)
	{
		const float cell = GetTerrainCellSize(id.Level);
		const glm::vec3 low = CornerOf(id) - 2.0f * cell;
		const int steps = (TerrainChunkCells + 3) * 4;
		const float step = cell * 0.25f;
		std::vector<float> x(size_t(steps + 1) * size_t(steps + 1));
		std::vector<float> y(x.size());
		std::vector<float> z(x.size());
		std::vector<float> values(x.size());
		bool bInside = false;
		bool bOutside = false;
		for (int k = 0; k <= steps; ++k)
		{
			for (int j = 0; j <= steps; ++j)
			{
				for (int i = 0; i <= steps; ++i)
				{
					const size_t n = size_t(i) + size_t(steps + 1) * size_t(j);
					x[n] = low.x + float(i) * step;
					y[n] = low.y + float(j) * step;
					z[n] = low.z + float(k) * step;
				}
			}
			SampleTerrainBatch(terrain, x.data(), y.data(), z.data(), x.size(), values.data());
			for (const float value : values)
			{
				bInside = bInside || value < 0.0f;
				bOutside = bOutside || value >= 0.0f;
			}
			if (bInside && bOutside)
			{
				return true;
			}
		}
		return false;
	}

	// Numbers spread over [0, 1), from integers only
	struct Sequence
	{
		uint32_t State = 0x13579BDFu;

		float Next()
		{
			State = State * 1664525u + 1013904223u;
			return static_cast<float>(State >> 8) * (1.0f / 16777216.0f);
		}

		float Between(float low, float high) { return low + (high - low) * Next(); }
	};

	// Field: skipping against dense sampling, rays against dense marching, the thin wall
	void MeasureField(const Config& config, Report& report)
	{
		const SdfTerrainComponent terrain = MakeTerrain(config);
		const int level = std::clamp(config.FinestLevel, TerrainFinestLevel, TerrainCoarsestLevel);
		const std::vector<TerrainChunkId> chunks = VolumeChunks(level);
		std::vector<uint8_t> skipped(chunks.size());
		std::vector<uint8_t> surface(chunks.size());
		const auto start = Clock::now();
		ForEachOnWorkers(chunks.size(), [&](size_t index)
			{
				skipped[index] = IsSkipped(terrain, chunks[index]) ? 1 : 0;
				surface[index] = DenseFindsSurface(terrain, chunks[index]) ? 1 : 0;
			});
		uint32_t wrongSkips = 0;
		uint32_t sampled = 0;
		uint32_t holding = 0;
		for (size_t index = 0; index < chunks.size(); ++index)
		{
			wrongSkips += skipped[index] && surface[index] ? 1u : 0u;
			sampled += skipped[index] ? 0u : 1u;
			holding += surface[index] ? 1u : 0u;
		}
		report.Add("Field: no chunk skipped where dense sampling finds the surface", wrongSkips == 0,
			std::to_string(chunks.size()) + " chunks, " + std::to_string(wrongSkips) + " wrongly skipped; " + std::to_string(sampled) + " sampled, " +
				std::to_string(holding) + " of them hold the surface (" + Fixed(100.0 * holding / std::max(sampled, 1u), 0) + " %); " +
				Fixed(MillisecondsSince(start) / 1000.0, 1) + " s");

		// Rays from all over the volume, against marching a centimetre at a time
		Sequence sequence;
		uint32_t rays = 0;
		uint32_t hits = 0;
		uint32_t wrong = 0;
		float worst = 0.0f;
		for (int n = 0; n < 600; ++n)
		{
			const glm::vec3 origin(sequence.Between(-100.0f, 70.0f), sequence.Between(-2.0f, 25.0f), sequence.Between(-20.0f, 20.0f));
			const glm::vec3 direction = glm::normalize(glm::vec3(sequence.Between(-1.0f, 1.0f), sequence.Between(-1.0f, 0.5f), sequence.Between(-1.0f, 1.0f)));
			if (SampleTerrainValue(terrain, origin) <= 0.0f)
			{
				continue;
			}
			++rays;
			constexpr float Length = 40.0f;
			std::optional<float> marched;
			for (float distance = 0.01f; distance <= Length; distance += 0.01f)
			{
				if (SampleTerrainValue(terrain, origin + direction * distance) <= 0.0f)
				{
					marched = distance;
					break;
				}
			}
			const std::optional<TerrainRayHit> hit = RayCastTerrain(terrain, origin, direction, Length);
			hits += hit ? 1u : 0u;
			if (marched.has_value() != hit.has_value())
			{
				++wrong;
				continue;
			}
			if (marched)
			{
				worst = std::max(worst, std::fabs(hit->Distance - *marched));
				wrong += std::fabs(hit->Distance - *marched) > 0.01f ? 1u : 0u;
			}
		}
		report.Add("Field: rays within 1 cm of dense marching", wrong == 0,
			std::to_string(rays) + " rays, " + std::to_string(hits) + " hit, " + std::to_string(wrong) + " off; at worst " + Fixed(worst * 100.0f, 2) + " cm");

		// The 2 m wall from all sides it shows, never passed through
		uint32_t through = 0;
		for (int n = 0; n < 400; ++n)
		{
			const float side = n % 2 == 0 ? -1.0f : 1.0f;
			// Aimed at it: within its ends and under its top where they reach it
			const glm::vec3 origin = glm::vec3(sequence.Between(-55.0f, -45.0f), sequence.Between(1.5f, 4.5f), side * 6.0f) + Offset;
			const glm::vec3 direction = glm::normalize(glm::vec3(sequence.Between(-0.4f, 0.4f), sequence.Between(-0.2f, 0.2f), -side));
			through += RayCastTerrain(terrain, origin, direction, 12.0f).has_value() ? 0u : 1u;
		}
		report.Add("Field: no ray through the 2 m wall", through == 0, "400 rays, " + std::to_string(through) + " through");
	}

	// Where a ray first meets a mesh's triangles, either side
	std::optional<float> RayMesh(const MeshData& mesh, const glm::vec3& corner, const glm::vec3& origin, const glm::vec3& direction, float length)
	{
		std::optional<float> nearest;
		for (size_t t = 0; t + 2 < mesh.Indices.size(); t += 3)
		{
			const glm::vec3 a = mesh.Vertices[mesh.Indices[t]].Position + corner;
			const glm::vec3 ab = mesh.Vertices[mesh.Indices[t + 1]].Position + corner - a;
			const glm::vec3 ac = mesh.Vertices[mesh.Indices[t + 2]].Position + corner - a;
			const glm::vec3 p = glm::cross(direction, ac);
			const float determinant = glm::dot(ab, p);
			if (std::fabs(determinant) < 1e-12f)
			{
				continue;
			}
			const glm::vec3 s = origin - a;
			const float u = glm::dot(s, p) / determinant;
			const glm::vec3 q = glm::cross(s, ab);
			const float v = glm::dot(direction, q) / determinant;
			const float distance = glm::dot(ac, q) / determinant;
			if (u >= 0.0f && v >= 0.0f && u + v <= 1.0f && distance >= 0.0f && distance <= length && (!nearest || distance < *nearest))
			{
				nearest = distance;
			}
		}
		return nearest;
	}

	// Features: each at every level, by its probes through the meshes of that level's chunks
	void MeasureFeatures(const Config& config, Report& report)
	{
		const SdfTerrainComponent terrain = MakeTerrain(config);
		const std::vector<Feature> features = MakeFeatures();
		const int finest = std::clamp(config.FinestLevel, TerrainFinestLevel, TerrainCoarsestLevel);
		std::map<std::pair<int, std::tuple<int, int, int>>, MeshData> meshes;
		std::ostringstream table;
		table << "| Feature (size) |";
		for (int level = finest; level <= 3; ++level)
		{
			table << " Level " << level << " (" << Fixed(GetTerrainCellSize(level), 1) << " m) |";
		}
		table << "\n|---|";
		for (int level = finest; level <= 3; ++level)
		{
			table << "---|";
		}
		table << "\n";
		uint32_t missing = 0;
		for (const Feature& feature : features)
		{
			table << "| " << feature.Name << " (" << Fixed(feature.Size, 0) << " m) |";
			for (int level = finest; level <= 3; ++level)
			{
				bool bShown = true;
				for (const Probe& probe : feature.Probes)
				{
					const glm::vec3 end = probe.Origin + probe.Direction * probe.Length;
					std::optional<float> nearest;
					for (const TerrainChunkId& id : ChunksIn(level, glm::min(probe.Origin, end) - 1.0f, glm::max(probe.Origin, end) + 1.0f))
					{
						const auto key = std::make_pair(id.Level, std::make_tuple(id.Key.x, id.Key.y, id.Key.z));
						auto found = meshes.find(key);
						if (found == meshes.end())
						{
							found = meshes.emplace(key, MakeTerrainChunkMesh(terrain, id)).first;
						}
						const std::optional<float> distance = RayMesh(found->second, CornerOf(id), probe.Origin, probe.Direction, probe.Length);
						if (distance && (!nearest || *distance < *nearest))
						{
							nearest = distance;
						}
					}
					bShown = bShown && (probe.bOpen ? !nearest.has_value() : nearest.has_value() && *nearest >= probe.HitFrom && *nearest <= probe.HitTo);
				}
				const bool bMust = feature.Size >= 2.0f * GetTerrainCellSize(level);
				missing += bMust && !bShown ? 1u : 0u;
				table << " " << (bShown ? "shown" : "lost") << (bMust ? "" : " (may be lost)") << " |";
			}
			table << "\n";
		}
		report.Tables.push_back(table.str());
		std::cout << table.str();
		report.Add("Features: each shown at every level its two-cell limit says it must be", missing == 0, std::to_string(missing) + " lost where they must be shown");
	}

	// The drawn chunks put together: every edge within `radius` of `centre` should have two
	// triangles, one each way round; and how many triangles face against the field's gradient
	struct MeshCheck
	{
		uint64_t Triangles = 0;
		uint32_t Edges = 0;
		uint32_t Open = 0;
		uint32_t Shared = 0;
		uint32_t Wound = 0;
		uint64_t Against = 0;
		uint64_t Bytes = 0;
	};

	MeshCheck CheckDrawn(const SdfTerrainComponent& terrain, const TerrainChunksComponent& chunks, const glm::vec3& centre, float radius)
	{
		using Point = std::array<uint32_t, 3>;
		auto bitsOf = [](const glm::vec3& position)
		{
			const glm::vec3 same = position + glm::vec3(0.0f);
			Point point;
			std::memcpy(point.data(), &same, sizeof(glm::vec3));
			return point;
		};
		struct PointHash
		{
			// Each value mixed through all the bits (SplitMix64's finaliser): positions at 4096ths of a
			// metre end in zeros, which a plain multiply would leave in the low bits the buckets use
			static uint64_t Mix(uint64_t value)
			{
				value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
				value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
				return value ^ (value >> 31);
			}

			size_t operator()(const std::pair<Point, Point>& edge) const
			{
				uint64_t hash = 0;
				for (const uint32_t value : edge.first)
				{
					hash = Mix(hash ^ value);
				}
				for (const uint32_t value : edge.second)
				{
					hash = Mix(hash ^ value);
				}
				return hash;
			}
		};
		MeshCheck check;
		std::vector<const TerrainChunk*> drawn;
		for (const TerrainChunk& chunk : chunks.Chunks)
		{
			if (chunk.bDrawn && chunk.Mesh.Mesh)
			{
				drawn.push_back(&chunk);
				check.Triangles += chunk.Mesh.Mesh->Indices.size() / 3;
				check.Bytes += chunk.Mesh.Mesh->Vertices.size() * sizeof(Vertex) + chunk.Mesh.Mesh->Indices.size() * sizeof(uint32_t);
			}
		}

		// Against the gradient: each chunk on a worker
		std::atomic<uint64_t> against = 0;
		ForEachOnWorkers(drawn.size(), [&](size_t index)
			{
				const MeshData& mesh = *drawn[index]->Mesh.Mesh;
				const glm::vec3 corner = drawn[index]->Origin;
				const float step = 0.05f * GetTerrainCellSize(drawn[index]->Level);
				uint64_t count = 0;
				for (size_t t = 0; t + 2 < mesh.Indices.size(); t += 3)
				{
					const glm::vec3 a = mesh.Vertices[mesh.Indices[t]].Position;
					const glm::vec3 b = mesh.Vertices[mesh.Indices[t + 1]].Position;
					const glm::vec3 c = mesh.Vertices[mesh.Indices[t + 2]].Position;
					const glm::vec3 facing = glm::cross(b - a, c - a);
					if (glm::dot(facing, facing) < 1e-12f)
					{
						continue;
					}
					count += glm::dot(facing, GetTerrainNormal(terrain, corner + (a + b + c) / 3.0f, step)) < 0.0f ? 1u : 0u;
				}
				against += count;
			});
		check.Against = against;

		std::unordered_map<std::pair<Point, Point>, std::array<int, 2>, PointHash> edges;
		edges.reserve(check.Triangles * 2);
		for (const TerrainChunk* chunk : drawn)
		{
			const MeshData& mesh = *chunk->Mesh.Mesh;
			for (size_t t = 0; t + 2 < mesh.Indices.size(); t += 3)
			{
				glm::vec3 positions[3];
				for (int n = 0; n < 3; ++n)
				{
					positions[n] = mesh.Vertices[mesh.Indices[t + n]].Position + chunk->Origin;
				}
				for (int n = 0; n < 3; ++n)
				{
					const glm::vec3& from = positions[n];
					const glm::vec3& to = positions[(n + 1) % 3];
					if (glm::length(from - centre) > radius || glm::length(to - centre) > radius)
					{
						continue;
					}
					const Point a = bitsOf(from);
					const Point b = bitsOf(to);
					if (a == b)
					{
						continue;
					}
					const bool bForward = a < b;
					edges[bForward ? std::make_pair(a, b) : std::make_pair(b, a)][bForward ? 0 : 1] += 1;
				}
			}
		}
		for (const auto& [ends, count] : edges)
		{
			++check.Edges;
			const int total = count[0] + count[1];
			check.Open += total % 2 != 0 ? 1u : 0u;
			check.Shared += total > 2 && total % 2 == 0 ? 1u : 0u;
			check.Wound += total == 2 && count[0] != 1 ? 1u : 0u;
		}
		return check;
	}

	// The workers the flythrough makes chunks on: the program's, or as many as NYX_GATE_WORKERS says,
	// to see what the main thread does with cores to spare
	JobSystem& FlythroughJobs()
	{
		static std::unique_ptr<JobSystem> own;
		const char* workers = std::getenv("NYX_GATE_WORKERS");
		if (workers == nullptr || std::atoi(workers) <= 0)
		{
			return JobSystem::Get();
		}
		if (!own)
		{
			own = std::make_unique<JobSystem>(uint32_t(std::atoi(workers)));
		}
		return *own;
	}

	void UpdateUntilSettled(Registry& world, Entity terrain, const glm::vec3& viewpoint)
	{
		const auto start = Clock::now();
		for (;;)
		{
			UpdateTerrainChunks(world, { &viewpoint, 1 }, FlythroughJobs());
			const TerrainChunksStats& stats = world.Get<TerrainChunksComponent>(terrain).Stats;
			if (stats.bSettled && stats.Pending == 0)
			{
				return;
			}
			if (Clock::now() - start > std::chrono::seconds(120))
			{
				std::cout << "  (the chunks didn't settle in two minutes)" << std::endl;
				return;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}

	// Meshes and the main thread: the flythrough, in real time at 50 m/s, checked at each stop
	void MeasureFlythrough(const Config& config, Report& report)
	{
		Registry world;
		const Entity terrain = world.CreateEntity();
		world.Add<TransformComponent>(terrain, TransformComponent{});
		world.Add<SdfTerrainComponent>(terrain, MakeTerrain(config));
		const SdfTerrainComponent& values = world.Get<SdfTerrainComponent>(terrain);
		const std::vector<glm::vec3> stops = MakeFlythrough();
		constexpr float Speed = 50.0f;
		constexpr float FrameSeconds = 1.0f / 60.0f;

		glm::vec3 viewpoint = stops.front();
		UpdateUntilSettled(world, terrain, viewpoint);
		double frames = 0.0;
		double total = 0.0;
		double worst = 0.0;
		int slow = 0;
		uint64_t swaps = world.Get<TerrainChunksComponent>(terrain).Stats.Swaps;
		MeshCheck all;
		uint64_t mostBytes = 0;
		uint64_t mostTriangles = 0;
		uint32_t mostChunks = 0;
		std::ostringstream stopsTable;
		stopsTable << "| Stop | Chunks | Triangles | Edges checked | Open | Shared by 4 | Wound | Against the gradient |\n|---|---|---|---|---|---|---|---|\n";
		for (size_t stop = 0; stop < stops.size(); ++stop)
		{
			std::cout << "  to " << Describe(stops[stop]) << std::endl;
			// There, a frame at a time
			while (glm::length(stops[stop] - viewpoint) > 0.01f)
			{
				const auto due = Clock::now() + std::chrono::microseconds(int64_t(FrameSeconds * 1e6f));
				const glm::vec3 toward = stops[stop] - viewpoint;
				viewpoint += toward * std::min(1.0f, Speed * FrameSeconds / glm::length(toward));
				const auto start = Clock::now();
				UpdateTerrainChunks(world, { &viewpoint, 1 }, FlythroughJobs());
				const double milliseconds = MillisecondsSince(start);
				total += milliseconds;
				worst = std::max(worst, milliseconds);
				slow += milliseconds > 2.0 ? 1 : 0;
				frames += 1.0;
				std::this_thread::sleep_until(due);
			}
			UpdateUntilSettled(world, terrain, viewpoint);
			std::cout << "  there, checking" << std::endl;
			const TerrainChunksComponent& chunks = world.Get<TerrainChunksComponent>(terrain);
			const MeshCheck check = CheckDrawn(values, chunks, viewpoint, values.ViewDistance - 32.0f);
			all.Triangles += check.Triangles;
			all.Edges += check.Edges;
			all.Open += check.Open;
			all.Shared += check.Shared;
			all.Wound += check.Wound;
			all.Against += check.Against;
			mostBytes = std::max(mostBytes, check.Bytes);
			mostTriangles = std::max(mostTriangles, check.Triangles);
			mostChunks = std::max(mostChunks, chunks.Stats.Resident);
			stopsTable << "| " << Describe(viewpoint) << " | " << chunks.Stats.Resident << " | " << check.Triangles << " | " << check.Edges << " | " << check.Open
					   << " | " << check.Shared << " (" << Fixed(100.0 * check.Shared / std::max(check.Edges, 1u), 2) << " %) | " << check.Wound << " | "
					   << check.Against << " (" << Fixed(100.0 * double(check.Against) / double(std::max<uint64_t>(check.Triangles, 1)), 2) << " %) |\n";
		}
		swaps = world.Get<TerrainChunksComponent>(terrain).Stats.Swaps - swaps;
		report.Tables.push_back(stopsTable.str());
		std::cout << stopsTable.str();
		report.Add("Meshes: no crack anywhere within the view", all.Open == 0, std::to_string(all.Open) + " open edges of " + std::to_string(all.Edges) + " at " + std::to_string(stops.size()) + " stops");
		report.Add("Meshes: no edge shared by four triangles", all.Shared == 0, std::to_string(all.Shared) + " (" + Fixed(100.0 * all.Shared / std::max(all.Edges, 1u), 2) + " % of edges)");
		report.Add("Meshes: triangles wound with their neighbours", all.Wound == 0, std::to_string(all.Wound) + " wound against");
		report.Add("Meshes: no triangle facing against the gradient", all.Against == 0,
			std::to_string(all.Against) + " of " + std::to_string(all.Triangles) + " (" + Fixed(100.0 * double(all.Against) / double(std::max<uint64_t>(all.Triangles, 1)), 2) + " %)");
		report.Add("Main thread: at most 0.5 ms a frame on average, 2 ms in the worst", total / std::max(frames, 1.0) <= 0.5 && worst <= 2.0,
			Fixed(total / std::max(frames, 1.0), 3) + " ms on average, " + Fixed(worst, 2) + " ms at worst, over " + std::to_string(int(frames)) + " frames flying " +
				std::to_string(swaps) + " trees, " + std::to_string(FlythroughJobs().GetWorkerCount()) + " workers; " + std::to_string(slow) + " frames over 2 ms");
		report.Add("Memory: the drawn meshes at most 512 MB", mostBytes <= 512ull << 20,
			Fixed(double(mostBytes) / double(1 << 20), 1) + " MB at most (" + std::to_string(mostChunks) + " chunks, " + std::to_string(mostTriangles) + " triangles); the same again on the GPU");
	}

	// Collision: the walks and stands of the features, and a fall at 60 m/s, in real time
	void MeasureCollision(const Config& config, Report& report)
	{
		GameWorld world;
		Registry& registry = world.GetRegistry();
		const Entity terrain = registry.CreateEntity();
		registry.Add<TransformComponent>(terrain, TransformComponent{});
		registry.Add<SdfTerrainComponent>(terrain, MakeTerrain(config));
		const SdfTerrainComponent& values = registry.Get<SdfTerrainComponent>(terrain);

		struct Walker
		{
			std::string Name;
			Walk Plan;
			Entity Character;
			glm::vec3 From{ 0.0f };
			bool bDone = false;
			float Deepest = 0.0f;
		};
		std::vector<Walker> walkers;
		for (const Feature& feature : MakeFeatures())
		{
			for (const Walk& walk : feature.Walks)
			{
				glm::vec3 start = walk.Start;
				if (!walk.bStand)
				{
					const std::optional<TerrainRayHit> ground = RayCastTerrain(values, start + glm::vec3(0.0f, 3.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 10.0f);
					start.y = (ground ? ground->Point.y : 0.0f) + 0.05f;
				}
				const Entity character = registry.CreateEntity();
				registry.Add<TransformComponent>(character, TransformComponent{ .Position = start });
				registry.Add<CharacterComponent>(character, CharacterComponent{ .GravityFactor = 0.0f });
				walkers.push_back({ feature.Name, walk, character, start });
			}
		}
		// Falling from 300 m just past the landmass's edge
		const glm::vec3 fallFrom(IslandX + IslandRadius + 7.0f, 300.0f, IslandZ);
		const Entity faller = registry.CreateEntity();
		registry.Add<TransformComponent>(faller, TransformComponent{ .Position = fallFrom });
		registry.Add<CharacterComponent>(faller, CharacterComponent{ .GravityFactor = 0.0f });

		uint64_t next = 0;
		auto step = [&]
		{
			const auto due = Clock::now() + std::chrono::microseconds(16667);
			world.Step(StepTime{ 1.0f / 60.0f, next, double(next) / 60.0 });
			++next;
			std::this_thread::sleep_until(due);
		};
		// Collision round them all first, floating where they start
		const auto start = Clock::now();
		do
		{
			step();
		} while ((registry.Get<TerrainCollisionComponent>(terrain).Stats.Pending > 0 ||
					 registry.Get<TerrainCollisionComponent>(terrain).Stats.Resident < registry.Get<TerrainCollisionComponent>(terrain).Stats.Wanted) &&
			Clock::now() - start < std::chrono::seconds(60));
		for (Walker& walker : walkers)
		{
			registry.Get<CharacterComponent>(walker.Character).GravityFactor = 1.0f;
			registry.Get<CharacterComponent>(walker.Character).DesiredMove = walker.Plan.Move;
		}
		registry.Get<CharacterComponent>(faller).GravityFactor = 1.0f;

		uint32_t worstUncovered = 0;
		uint32_t worstOutrunning = 0;
		float fastest = 0.0f;
		bool bLanded = false;
		for (int frame = 0; frame < 60 * 16; ++frame)
		{
			step();
			const TerrainCollisionStats& stats = registry.Get<TerrainCollisionComponent>(terrain).Stats;
			worstUncovered = std::max(worstUncovered, stats.Uncovered);
			worstOutrunning = std::max(worstOutrunning, stats.Outrunning);
			const float seconds = float(frame) / 60.0f;
			for (Walker& walker : walkers)
			{
				const glm::vec3 feet = registry.Get<TransformComponent>(walker.Character).Position;
				walker.Deepest = std::min(walker.Deepest, SampleTerrainValue(values, feet + glm::vec3(0.0f, 0.3f, 0.0f)));
				if (walker.bDone || seconds > walker.Plan.Seconds)
				{
					continue;
				}
				if (walker.Plan.bStand)
				{
					walker.bDone = registry.Get<CharacterComponent>(walker.Character).bOnGround && seconds > 1.0f &&
						std::fabs(feet.y - walker.Plan.StandHeight) < walker.Plan.Tolerance;
					continue;
				}
				const float along = glm::dot(feet - walker.From, walker.Plan.Move);
				if (along >= walker.Plan.Distance)
				{
					walker.bDone = true;
				}
			}
			const CharacterComponent& falling = registry.Get<CharacterComponent>(faller);
			fastest = std::max(fastest, -falling.Velocity.y);
			bLanded = bLanded || (falling.bOnGround && registry.Get<TransformComponent>(faller).Position.y < 10.0f);
		}
		std::string walks;
		uint32_t failed = 0;
		for (const Walker& walker : walkers)
		{
			const bool bPassed = (walker.bDone != walker.Plan.bBlocked) && walker.Deepest > -0.1f;
			failed += bPassed ? 0u : 1u;
			walks += (walks.empty() ? "" : "; ") + walker.Name + (walker.Plan.bStand ? " stood on " : (walker.Plan.bBlocked ? " walked into " : " walked through ")) +
				(bPassed ? "as it should" : "NOT as it should (at " + Describe(registry.Get<TransformComponent>(walker.Character).Position) + ")");
		}
		report.Add("Collision: the features walkable where they should be", failed == 0, walks);
		report.Add("Collision: a fall at 60 m/s past the landmass's edge never without collision", worstUncovered == 0 && worstOutrunning == 0 && bLanded && fastest > 55.0f,
			"up to " + Fixed(fastest, 1) + " m/s, " + (bLanded ? "landed" : "NOT landed") + "; bodies without collision at worst " + std::to_string(worstUncovered) +
				", outrunning it " + std::to_string(worstOutrunning) + "; the worst chunk " +
				Fixed(registry.Get<TerrainCollisionComponent>(terrain).Stats.WorstSeconds * 1000.0, 0) + " ms");
	}

	// A chunk on one thread, and a full view after a jump
	void MeasureTimes(const Config& config, Report& report)
	{
		const SdfTerrainComponent terrain = MakeTerrain(config);
		const int level = std::clamp(config.FinestLevel, TerrainFinestLevel, TerrainCoarsestLevel);
		std::vector<TerrainChunkId> chunks;
		for (const TerrainChunkId& id : VolumeChunks(level))
		{
			if (!IsSkipped(terrain, id) && chunks.size() < 40)
			{
				chunks.push_back(id);
			}
		}
		double sample = 0.0;
		double mesh = 0.0;
		double worst = 0.0;
		uint64_t triangles = 0;
		for (const TerrainChunkId& id : chunks)
		{
			TerrainChunkTimes times;
			MakeTerrainChunkMesh(terrain, id, {}, &times);
			sample += times.SampleMilliseconds;
			mesh += times.MeshMilliseconds;
			worst = std::max(worst, times.SampleMilliseconds + times.MeshMilliseconds);
			triangles += times.Triangles;
		}
		const double count = double(std::max<size_t>(chunks.size(), 1));
		report.Add("A chunk sampled and meshed on one thread in at most 3 ms", (sample + mesh) / count <= 3.0,
			Fixed(sample / count) + " ms sampling and " + Fixed(mesh / count) + " ms meshing on average, " + Fixed(worst) + " ms at worst, " +
				std::to_string(triangles / uint64_t(count)) + " triangles, over " + std::to_string(chunks.size()) + " chunks of the volume");

		Registry world;
		const Entity entity = world.CreateEntity();
		world.Add<TransformComponent>(entity, TransformComponent{});
		world.Add<SdfTerrainComponent>(entity, terrain);
		UpdateUntilSettled(world, entity, glm::vec3(0.0f, 3.0f, -10.0f));
		const glm::vec3 there(4000.0f, 3.0f, 4000.0f);
		const auto start = Clock::now();
		UpdateUntilSettled(world, entity, there);
		const double seconds = MillisecondsSince(start) / 1000.0;
		report.Add("A full view after a jump within 1 s", seconds <= 1.0,
			Fixed(seconds) + " s for " + std::to_string(world.Get<TerrainChunksComponent>(entity).Stats.Resident) + " chunks on " +
				std::to_string(JobSystem::Get().GetWorkerCount()) + " workers");
	}

	// The volume to walk by hand: the dual mesher, a character at the tunnel's mouth, a camera
	// following it, a sun
	void WriteScene()
	{
		Registry world;
		auto addNamed = [&](const std::string& name, const TransformComponent& transform)
		{
			const Entity entity = world.CreateEntity();
			world.Add<NameComponent>(entity, NameComponent{ name });
			world.Add<GuidComponent>(entity, GuidComponent{ EntityGuid::Generate() });
			world.Add<TransformComponent>(entity, transform);
			return entity;
		};
		const Entity terrain = addNamed("Terrain", TransformComponent{});
		world.Add<SdfTerrainComponent>(terrain, MakeVolume());
		world.Add<MeshRendererComponent>(terrain, MeshRendererComponent{ .Material = AssetReference{ .Type = "Material", .Path = "Materials/VertexColor.nyxmat" }, .bVisible = true });

		const Entity character = addNamed("Character", TransformComponent{ .Position = glm::vec3(-80.0f, 0.3f, -18.0f) });
		world.Add<MeshRendererComponent>(character, MeshRendererComponent{ .Mesh = AssetReference{ .Type = "Mesh", .Path = "Meshes/Capsule.nyxmesh" },
			.Material = AssetReference{ .Type = "Material", .Path = "Materials/Untextured.nyxmat" }, .bVisible = true });
		world.Add<CharacterComponent>(character, CharacterComponent{});

		const Entity camera = addNamed("Camera", TransformComponent{ .Position = glm::vec3(-80.0f, 3.0f, -26.0f), .Rotation = glm::angleAxis(glm::radians(-12.0f), glm::vec3(1.0f, 0.0f, 0.0f)) });
		world.Add<CameraComponent>(camera, CameraComponent{});
		world.Add<OrbitCameraComponent>(camera, OrbitCameraComponent{ .Target = world.Get<GuidComponent>(character).Guid });

		const Entity sun = addNamed("Sun", TransformComponent{ .Position = glm::vec3(0.0f, 50.0f, 0.0f), .Rotation = glm::quat(glm::radians(glm::vec3(-50.0f, 30.0f, 0.0f))) });
		world.Add<DirectionalLightComponent>(sun, DirectionalLightComponent{});

		const std::filesystem::path path = Nyx::Paths::GetAssetsDir() / "Scenes/SdfTerrainGate.nyxscene";
		Nyx::Tests::Require(SceneSerializer::SaveToFile(world, path), "The gate scene can't be saved");
		std::cout << "Wrote " << path.string() << "\n";
	}
}

int main(int argc, char** argv)
{
	std::cout << std::unitbuf;
	Nyx::Logger::Get().Init();
	Nyx::RegisterComponentTypes();
	if (argc > 1 && std::strcmp(argv[1], "--write-scene") == 0)
	{
		WriteScene();
		return 0;
	}
	std::vector<Config> configs{ { ESdfTerrainMesher::Dual, 0 }, { ESdfTerrainMesher::MarchingCubes, 0 }, { ESdfTerrainMesher::Dual, -1 }, { ESdfTerrainMesher::MarchingCubes, -1 } };
	if (argc > 2 && std::strcmp(argv[1], "--only") == 0)
	{
		const std::string only = argv[2];
		std::erase_if(configs, [&](const Config& config)
			{
				const bool bMesher = only.find(config.Mesher == ESdfTerrainMesher::Dual ? "dual" : "marching") != std::string::npos;
				const bool bLevel = only.find(",") == std::string::npos || only.find(config.FinestLevel < 0 ? ",-1" : ",0") != std::string::npos;
				return !(bMesher && bLevel);
			});
	}

	std::vector<std::pair<Config, Report>> reports;
	for (const Config& config : configs)
	{
		std::cout << "\n## " << config.Name() << "\n" << std::endl;
		Report report;
		// NYX_GATE_SECTIONS=flythrough runs that alone
		const char* sections = std::getenv("NYX_GATE_SECTIONS");
		const std::string only = sections != nullptr ? sections : "";
		auto wanted = [&](const char* name) { return only.empty() || only.find(name) != std::string::npos; };
		if (wanted("field"))
		{
			MeasureField(config, report);
		}
		if (wanted("features"))
		{
			MeasureFeatures(config, report);
		}
		if (wanted("flythrough"))
		{
			MeasureFlythrough(config, report);
		}
		if (wanted("collision"))
		{
			MeasureCollision(config, report);
		}
		if (wanted("times"))
		{
			MeasureTimes(config, report);
		}
		reports.push_back({ config, report });
	}

	std::cout << "\n## Summary\n\n| Criterion |";
	for (const auto& [config, report] : reports)
	{
		std::cout << " " << config.Name() << " |";
	}
	std::cout << "\n|---|";
	for (size_t n = 0; n < reports.size(); ++n)
	{
		std::cout << "---|";
	}
	std::cout << "\n";
	for (size_t line = 0; !reports.empty() && line < reports.front().second.Lines.size(); ++line)
	{
		std::cout << "| " << reports.front().second.Lines[line].Criterion << " |";
		for (const auto& [config, report] : reports)
		{
			std::cout << " " << (report.Lines[line].bPassed ? "pass" : "**fail**") << " |";
		}
		std::cout << "\n";
	}
	return 0;
}
