#include "NyxPCH.h"
#include "TerrainChunks.h"

#include "FieldMesher.h"
#include "JobSystem.h"
#include "SdfTerrain.h"
#include "TransformComponent.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <format>
#include <limits>
#include <mutex>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace Nyx
{
	namespace Detail
	{
		// What a terrain's chunks are being made from and what is in flight, shared with the jobs, so a
		// job that finishes after its terrain went writes into this and nothing else
		struct TerrainStreaming
		{
			// The values the chunks are being made from now, and a copy of them for the jobs
			uint64_t Shape = 0;
			std::shared_ptr<const SdfTerrainComponent> Snapshot;
			// Whether the surface crosses a chunk, for the values now: asked once a chunk
			std::unordered_map<uint64_t, bool> HasSurface;

			struct InFlight
			{
				JobHandle Job;
				uint64_t Shape = 0;
			};
			std::unordered_map<uint64_t, InFlight> Jobs;

			struct Done
			{
				glm::ivec3 Key{ 0 };
				uint64_t Shape = 0;
				std::shared_ptr<const MeshData> Mesh;
				TerrainChunkTimes Times;
			};
			std::mutex Lock;
			std::vector<Done> Finished;
			// Where the jobs went, to cancel them when the terrain goes
			JobSystem* Workers = nullptr;
		};
	}

	namespace
	{
		// At most this many chunks a terrain in flight, as many as there are workers twice over, and
		// at most so many swapped in a frame, so a burst of them never costs a frame much
		uint32_t GetMaxJobs(const JobSystem& jobs)
		{
			return std::max(jobs.GetWorkerCount() * 2, 2u);
		}
		constexpr uint32_t MaxSwapsPerFrame = 48;

		uint64_t KeyOf(const glm::ivec3& key)
		{
			auto part = [](int value) { return uint64_t(uint32_t(value + (1 << 20)) & 0x1FFFFFu); };
			return part(key.x) | (part(key.y) << 21) | (part(key.z) << 42);
		}

		struct Hasher
		{
			uint64_t Hash = 0xCBF29CE484222325ull;

			void Bytes(const void* data, size_t size)
			{
				const auto* bytes = static_cast<const uint8_t*>(data);
				for (size_t i = 0; i < size; ++i)
				{
					Hash ^= bytes[i];
					Hash *= 0x100000001B3ull;
				}
			}

			template <typename T>
			void Value(const T& value)
			{
				Bytes(&value, sizeof(T));
			}
		};

		glm::vec3 ChunkCorner(const glm::ivec3& key)
		{
			return glm::vec3(key) * TerrainChunkSize;
		}

		float DistanceToChunk(const glm::vec3& point, const glm::ivec3& key)
		{
			const glm::vec3 low = ChunkCorner(key);
			const glm::vec3 nearest = glm::clamp(point, low, low + TerrainChunkSize);
			return glm::length(point - nearest);
		}

		float NearestViewpoint(std::span<const glm::vec3> viewpoints, const glm::ivec3& key)
		{
			float nearest = std::numeric_limits<float>::max();
			for (const glm::vec3& viewpoint : viewpoints)
			{
				nearest = std::min(nearest, DistanceToChunk(viewpoint, key));
			}
			return nearest;
		}

		bool HasSurface(Detail::TerrainStreaming& streaming, const SdfTerrainComponent& terrain, const glm::ivec3& key)
		{
			const uint64_t packed = KeyOf(key);
			const auto found = streaming.HasSurface.find(packed);
			if (found != streaming.HasSurface.end())
			{
				return found->second;
			}
			// Over every sample the chunk's mesh is made from
			const glm::vec3 low = ChunkCorner(key) - 2.0f * TerrainCellSize;
			const glm::vec3 high = ChunkCorner(key) + TerrainChunkSize + TerrainCellSize;
			const TerrainValueRange range = GetTerrainValueRange(terrain, low, high);
			const bool bSurface = !range.IsAllAir() && !range.IsAllSolid();
			streaming.HasSurface.emplace(packed, bSurface);
			return bSurface;
		}

		void UpdateTerrain(Registry& world, Entity entity, const SdfTerrainComponent& terrain, std::span<const glm::vec3> worldViewpoints, JobSystem& jobs)
		{
			if (!world.Has<TerrainChunksComponent>(entity))
			{
				world.Add<TerrainChunksComponent>(entity, TerrainChunksComponent{});
			}
			TerrainChunksComponent& chunks = world.Get<TerrainChunksComponent>(entity);
			if (!chunks.Streaming)
			{
				chunks.Streaming = std::make_shared<Detail::TerrainStreaming>();
			}
			Detail::TerrainStreaming& streaming = *chunks.Streaming;
			streaming.Workers = &jobs;

			// New values: a copy for the jobs, what was asked of the old ones forgotten, their jobs
			// cancelled; the chunks made from them are drawn until their new meshes are in
			const uint64_t shape = HashSdfTerrainShape(terrain);
			if (shape != streaming.Shape || !streaming.Snapshot)
			{
				streaming.Shape = shape;
				streaming.Snapshot = std::make_shared<const SdfTerrainComponent>(terrain);
				streaming.HasSurface.clear();
				for (auto& [key, job] : streaming.Jobs)
				{
					jobs.Cancel(job.Job);
				}
				streaming.Jobs.clear();
			}

			// The viewpoints in the terrain's own space
			const glm::mat4 toTerrain = world.Has<TransformComponent>(entity) ? glm::inverse(world.Get<TransformComponent>(entity).ToMatrix()) : glm::mat4(1.0f);
			std::vector<glm::vec3> viewpoints;
			for (const glm::vec3& viewpoint : worldViewpoints)
			{
				viewpoints.push_back(glm::vec3(toTerrain * glm::vec4(viewpoint, 1.0f)));
			}
			const float reach = std::max(terrain.ViewDistance, 0.0f);

			// The chunks wanted, nearest first
			struct Candidate
			{
				glm::ivec3 Key{ 0 };
				float Distance = 0.0f;
			};
			std::vector<Candidate> wanted;
			std::unordered_set<uint64_t> wantedKeys;
			uint32_t skipped = 0;
			for (const glm::vec3& viewpoint : viewpoints)
			{
				const glm::ivec3 low(glm::floor((viewpoint - reach) / TerrainChunkSize));
				const glm::ivec3 high(glm::floor((viewpoint + reach) / TerrainChunkSize));
				for (int z = low.z; z <= high.z; ++z)
				{
					for (int y = low.y; y <= high.y; ++y)
					{
						for (int x = low.x; x <= high.x; ++x)
						{
							const glm::ivec3 key(x, y, z);
							if (DistanceToChunk(viewpoint, key) > reach || wantedKeys.contains(KeyOf(key)))
							{
								continue;
							}
							if (!HasSurface(streaming, terrain, key))
							{
								++skipped;
								continue;
							}
							wantedKeys.insert(KeyOf(key));
							wanted.push_back({ key, NearestViewpoint(viewpoints, key) });
						}
					}
				}
			}
			std::sort(wanted.begin(), wanted.end(), [](const Candidate& a, const Candidate& b)
				{
					return a.Distance != b.Distance ? a.Distance < b.Distance : std::tie(a.Key.x, a.Key.y, a.Key.z) < std::tie(b.Key.x, b.Key.y, b.Key.z);
				});

			// What the workers made, in: its own chunk replaced, a new one added while still wanted
			// with a chunk to spare; made from values since changed, let go of
			std::vector<Detail::TerrainStreaming::Done> finished;
			{
				std::lock_guard lock(streaming.Lock);
				finished.swap(streaming.Finished);
			}
			std::unordered_map<uint64_t, size_t> resident;
			for (size_t index = 0; index < chunks.Chunks.size(); ++index)
			{
				resident.emplace(KeyOf(chunks.Chunks[index].Key), index);
			}
			uint32_t swapped = 0;
			std::vector<Detail::TerrainStreaming::Done> later;
			for (Detail::TerrainStreaming::Done& done : finished)
			{
				const uint64_t packed = KeyOf(done.Key);
				if (done.Shape != streaming.Shape)
				{
					++chunks.Stats.Dropped;
					continue;
				}
				if (swapped >= MaxSwapsPerFrame)
				{
					later.push_back(std::move(done));
					continue;
				}
				streaming.Jobs.erase(packed);
				++chunks.Stats.Made;
				chunks.Stats.LastSampleMilliseconds = done.Times.SampleMilliseconds;
				chunks.Stats.LastMeshMilliseconds = done.Times.MeshMilliseconds;
				if (NearestViewpoint(viewpoints, done.Key) > reach + TerrainChunkSize)
				{
					continue;
				}
				TerrainChunk chunk;
				chunk.Key = done.Key;
				chunk.Origin = ChunkCorner(done.Key);
				chunk.Shape = done.Shape;
				const auto found = resident.find(packed);
				if (found != resident.end())
				{
					chunk.Mesh = chunks.Chunks[found->second].Mesh;
				}
				if (done.Mesh && !done.Mesh->Indices.empty())
				{
					SetRuntimeMesh(chunk.Mesh, std::move(done.Mesh));
				}
				else
				{
					chunk.Mesh = RuntimeMeshComponent{};
				}
				if (found != resident.end())
				{
					chunks.Chunks[found->second] = std::move(chunk);
				}
				else
				{
					resident.emplace(packed, chunks.Chunks.size());
					chunks.Chunks.push_back(std::move(chunk));
				}
				++swapped;
			}
			if (!later.empty())
			{
				std::lock_guard lock(streaming.Lock);
				streaming.Finished.insert(streaming.Finished.begin(), std::make_move_iterator(later.begin()), std::make_move_iterator(later.end()));
			}

			// Chunks a chunk past the view distance go, and those made from values since changed that
			// aren't wanted (the surface no longer crosses them, or they are past the view distance, so
			// nothing would make them again); jobs for chunks that far are cancelled
			std::erase_if(chunks.Chunks, [&](const TerrainChunk& chunk)
				{
					return NearestViewpoint(viewpoints, chunk.Key) > reach + TerrainChunkSize ||
						(chunk.Shape != streaming.Shape && !wantedKeys.contains(KeyOf(chunk.Key)));
				});
			for (auto it = streaming.Jobs.begin(); it != streaming.Jobs.end();)
			{
				const glm::ivec3 key(int((it->first & 0x1FFFFFu)) - (1 << 20), int(((it->first >> 21) & 0x1FFFFFu)) - (1 << 20),
					int(((it->first >> 42) & 0x1FFFFFu)) - (1 << 20));
				if (NearestViewpoint(viewpoints, key) > reach + TerrainChunkSize && jobs.Cancel(it->second.Job))
				{
					it = streaming.Jobs.erase(it);
					continue;
				}
				++it;
			}

			// The nearest chunks missing, or made from other values, to the workers
			resident.clear();
			for (size_t index = 0; index < chunks.Chunks.size(); ++index)
			{
				resident.emplace(KeyOf(chunks.Chunks[index].Key), index);
			}
			const uint32_t maxJobs = GetMaxJobs(jobs);
			for (const Candidate& candidate : wanted)
			{
				if (streaming.Jobs.size() >= maxJobs)
				{
					break;
				}
				const uint64_t packed = KeyOf(candidate.Key);
				const auto found = resident.find(packed);
				if ((found != resident.end() && chunks.Chunks[found->second].Shape == streaming.Shape) || streaming.Jobs.contains(packed))
				{
					continue;
				}
				std::shared_ptr<Detail::TerrainStreaming> shared = chunks.Streaming;
				std::shared_ptr<const SdfTerrainComponent> snapshot = streaming.Snapshot;
				const glm::ivec3 key = candidate.Key;
				const uint64_t jobShape = streaming.Shape;
				JobHandle job = jobs.Schedule(
					[shared, snapshot, key, jobShape]
					{
						Detail::TerrainStreaming::Done done;
						done.Key = key;
						done.Shape = jobShape;
						done.Mesh = std::make_shared<const MeshData>(MakeTerrainChunkMesh(*snapshot, key, &done.Times));
						std::lock_guard lock(shared->Lock);
						shared->Finished.push_back(std::move(done));
					},
					EJobPriority::Normal);
				streaming.Jobs.emplace(packed, Detail::TerrainStreaming::InFlight{ job, jobShape });
			}

			chunks.Stats.Wanted = static_cast<uint32_t>(wanted.size());
			chunks.Stats.Resident = static_cast<uint32_t>(chunks.Chunks.size());
			chunks.Stats.Pending = static_cast<uint32_t>(streaming.Jobs.size());
			chunks.Stats.Skipped = skipped;
			chunks.Stats.Triangles = 0;
			for (const TerrainChunk& chunk : chunks.Chunks)
			{
				chunks.Stats.Triangles += chunk.Mesh.Mesh ? chunk.Mesh.Mesh->Indices.size() / 3 : 0;
			}
		}
	}

	MeshData MakeTerrainChunkMesh(const SdfTerrainComponent& terrain, const glm::ivec3& key, TerrainChunkTimes* outTimes)
	{
		const auto start = std::chrono::steady_clock::now();
		const glm::vec3 corner = ChunkCorner(key);
		// Two cells before the corner and one past the far side: the cells the chunk's quads join, and
		// the samples either side of their corners
		const FieldGrid grid = SampleTerrainGrid(terrain, corner - 2.0f * TerrainCellSize, TerrainCellSize, glm::ivec3(TerrainChunkCells + 4));
		const auto sampled = std::chrono::steady_clock::now();

		FieldMeshSettings settings;
		settings.QuadMin = glm::ivec3(2);
		settings.QuadMax = glm::ivec3(2 + TerrainChunkCells);
		for (const uint32_t material : terrain.SharpMaterials)
		{
			if (material < 256)
			{
				settings.SharpMaterials.resize(std::max<size_t>(settings.SharpMaterials.size(), material + 1), false);
				settings.SharpMaterials[material] = true;
			}
		}
		settings.Field = [&terrain](const float* x, const float* y, const float* z, size_t count, float* out) { SampleTerrainBatch(terrain, x, y, z, count, out); };
		MeshData mesh = terrain.Mesher == ESdfTerrainMesher::MarchingCubes ? MakeMarchingFieldMesh(grid, settings) : MakeFieldMesh(grid, settings);

		// Around the chunk's corner: whole metres from both, so the subtraction is exact and a vertex
		// two chunks share is the same bits in both once the corner is added back
		for (Vertex& vertex : mesh.Vertices)
		{
			vertex.Position -= corner;
			const uint32_t material = static_cast<uint32_t>(vertex.Custom.y);
			vertex.Color = material < terrain.MaterialColors.size() ? terrain.MaterialColors[material] : glm::vec3(1.0f);
		}
		if (outTimes != nullptr)
		{
			outTimes->SampleMilliseconds = std::chrono::duration<double, std::milli>(sampled - start).count();
			outTimes->MeshMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - sampled).count();
			outTimes->Triangles = static_cast<uint32_t>(mesh.Indices.size() / 3);
		}
		return mesh;
	}

	uint64_t HashSdfTerrainShape(const SdfTerrainComponent& terrain)
	{
		Hasher hasher;
		hasher.Value(terrain.Seed);
		hasher.Value(terrain.Mesher);
		hasher.Value(terrain.Layers.size());
		for (const SdfTerrainLayer& layer : terrain.Layers)
		{
			hasher.Value(layer.bEnabled);
			hasher.Value(layer.Kind);
			hasher.Value(layer.Combine);
			hasher.Value(layer.BlendRadius);
			hasher.Value(layer.Material);
			hasher.Value(layer.Height);
			hasher.Value(layer.Depth);
			hasher.Value(layer.Amplitude);
			hasher.Value(layer.Center);
			hasher.Value(layer.Radius);
			hasher.Value(layer.UndersideDepth);
			hasher.Value(layer.EdgeRoughness);
			hasher.Value(layer.Rounding);
			hasher.Value(layer.Openness);
			hasher.Value(layer.TerraceHeight);
			hasher.Value(layer.TerraceSharpness);
			hasher.Value(layer.Seed);
			hasher.Value(layer.Octaves);
			hasher.Value(layer.Frequency);
			hasher.Value(layer.Lacunarity);
			hasher.Value(layer.Gain);
			hasher.Value(layer.bRidged);
		}
		hasher.Value(terrain.SharpMaterials.size());
		for (const uint32_t material : terrain.SharpMaterials)
		{
			hasher.Value(material);
		}
		hasher.Value(terrain.MaterialColors.size());
		for (const glm::vec3& color : terrain.MaterialColors)
		{
			hasher.Value(color);
		}
		return hasher.Hash;
	}

	void UpdateTerrainChunks(Registry& world, std::span<const glm::vec3> viewpoints, JobSystem& jobs)
	{
		std::vector<Entity> terrains;
		world.Each<SdfTerrainComponent>([&](Entity entity, SdfTerrainComponent&) { terrains.push_back(entity); });
		for (const Entity entity : terrains)
		{
			UpdateTerrain(world, entity, world.Get<SdfTerrainComponent>(entity), viewpoints, jobs);
		}
	}

	void RemoveTerrainChunks(Registry& world, Entity entity)
	{
		if (!world.Has<TerrainChunksComponent>(entity))
		{
			return;
		}
		// What hasn't started never runs; what runs finishes into the state it shares, then goes
		if (const std::shared_ptr<Detail::TerrainStreaming>& streaming = world.Get<TerrainChunksComponent>(entity).Streaming;
			streaming && streaming->Workers != nullptr)
		{
			for (auto& [key, job] : streaming->Jobs)
			{
				streaming->Workers->Cancel(job.Job);
			}
		}
		world.Remove<TerrainChunksComponent>(entity);
	}

	std::vector<InspectedValue> DescribeTerrainChunks(const TerrainChunksComponent& component)
	{
		const TerrainChunksStats& stats = component.Stats;
		return {
			{ "Chunks", std::format("{} drawn of {} wanted, {} being made", stats.Resident, stats.Wanted, stats.Pending) },
			{ "Skipped", std::format("{} all air or all solid", stats.Skipped) },
			{ "Triangles", std::to_string(stats.Triangles) },
			{ "Made", std::format("{}, {} let go of for values since changed", stats.Made, stats.Dropped) },
			{ "Last Chunk", std::format("sampled in {:.2f} ms, meshed in {:.2f} ms", stats.LastSampleMilliseconds, stats.LastMeshMilliseconds) },
		};
	}
}
