#include "NyxPCH.h"
#include "TerrainNavigation.h"

#include "DebugDraw.h"
#include "FieldMesher.h"
#include "Profiler.h"
#include "SdfTerrain.h"
#include "TerrainChunks.h"
#include "TerrainWater.h"
#include "TerrainWorldPlan.h"
#include "TransformComponent.h"

#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <Recast.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <map>

namespace Nyx
{
	namespace
	{
		// A slab of the column meshed at once, in metres up, with a cell more each way so slabs meet
		constexpr float SlabHeight = 32.0f;
		constexpr float CellHeight = 0.2f;
		// The tiles being made at once a terrain, and kept past the distance (so a viewpoint moving
		// back and forth doesn't make them again)
		constexpr uint32_t MostInFlight = 8;
		constexpr float KeepBeyond = 1.5f;
		// Detour's navigation mesh: tiles and polygons a tile its references can tell apart
		constexpr int MostTiles = 1024;
		constexpr int MostTilePolygons = 4096;
		constexpr int MostPathPolygons = 2048;
		const glm::vec3 SearchExtents(4.0f, 8.0f, 4.0f);

		uint64_t ColumnKey(const glm::ivec2& column)
		{
			return (uint64_t(uint32_t(column.x)) << 32) | uint64_t(uint32_t(column.y));
		}

		template <typename T>
		void AddToHash(uint64_t& hash, const T& value)
		{
			static_assert(std::is_trivially_copyable_v<T>);
			const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
			for (size_t index = 0; index < sizeof(T); ++index)
			{
				hash = (hash ^ uint64_t(bytes[index])) * 0x100000001B3ull;
			}
		}

		// The cells a character needs, in Recast's units
		struct Agent
		{
			int Height = 0;
			int Climb = 0;
			int Radius = 0;
			int Border = 0;
			float CellSize = 0.25f;
		};
		Agent AgentOf(const SdfTerrainNavigation& values)
		{
			Agent agent;
			agent.CellSize = std::clamp(values.CellSize, 0.1f, 1.0f);
			agent.Height = std::max(int(std::ceil(values.AgentHeight / CellHeight)), 3);
			agent.Climb = std::max(int(std::floor(values.StepHeight / CellHeight)), 0);
			agent.Radius = std::max(int(std::ceil(values.AgentRadius / agent.CellSize)), 0);
			agent.Border = agent.Radius + 3;
			return agent;
		}
	}

	TerrainNavigationTile MakeTerrainNavigationTile(const SdfTerrainComponent& terrain, std::span<const SdfTerrainEdit> edits, const glm::ivec2& column, const TerrainWater* water)
	{
		NYX_PROFILE_FUNCTION();
		const auto start = std::chrono::steady_clock::now();
		TerrainNavigationTile tile;
		tile.Column = column;
		const SdfTerrainNavigation& values = terrain.Navigation;
		const Agent agent = AgentOf(values);
		const float border = float(agent.Border) * agent.CellSize;
		const glm::vec2 low = glm::vec2(column) * TerrainNavigationTileSize;
		const glm::vec2 high = low + TerrainNavigationTileSize;

		// Where the surface can be up the column: from the plan's bounds, else a kilometre either way
		float top = 1024.0f;
		float bottom = -1024.0f;
		if (UsesTerrainWorldPlan(terrain))
		{
			const std::shared_ptr<const TerrainWorldPlan> plan = GetTerrainWorldPlan(terrain);
			const TerrainWorldPlan::Bounds bounds = plan->GetRange(low - TerrainWorldPlan::CellSize, high + TerrainWorldPlan::CellSize);
			const SdfTerrainWorldPlan& made = plan->Made;
			top = bounds.HighHeight + bounds.HighRough + bounds.HighPlains * 2.0f + values.AgentHeight + 8.0f;
			bottom = bounds.LowHeight - made.Thickness - std::fabs(made.UndersideDepth) - std::fabs(made.RiftDepth) - 50.0f;
		}
		for (const SdfTerrainEdit& edit : edits)
		{
			glm::vec3 editLow;
			glm::vec3 editHigh;
			GetTerrainEditBounds(edit, editLow, editHigh);
			top = std::max(top, editHigh.y + values.AgentHeight);
		}

		// The field meshed at 1 m cells, as the finest chunks are, slab by slab where the surface can
		// be, over the tile and a border round it; each triangle walkable or not by its slope, and
		// under water to swim
		const float reach = std::ceil(border) + 1.0f;
		const glm::vec2 meshLow = glm::floor(low - reach);
		const glm::vec2 meshHigh = glm::ceil(high + reach);
		std::vector<float> vertices;
		std::vector<int> triangles;
		for (float slab = std::floor(bottom); slab < top; slab += SlabHeight)
		{
			const glm::vec3 boxMin(meshLow.x, slab - 1.0f, meshLow.y);
			const glm::vec3 boxMax(meshHigh.x, slab + SlabHeight + 1.0f, meshHigh.y);
			const TerrainValueRange range = GetTerrainValueRange(terrain, boxMin, boxMax);
			if (range.IsAllAir() || range.Highest < 0.0f)
			{
				continue;
			}
			const std::vector<SdfTerrainEdit> reaching = GetTerrainEditsReaching(terrain, edits, boxMin, boxMax);
			const glm::ivec3 size(int(meshHigh.x - meshLow.x) + 1, int(SlabHeight) + 3, int(meshHigh.y - meshLow.y) + 1);
			const FieldGrid grid = SampleTerrainGrid(terrain, reaching, boxMin, 1.0f, size);
			const MeshData mesh = MakeFieldMesh(grid);
			++tile.Slabs;
			const int first = int(vertices.size() / 3);
			for (const Vertex& vertex : mesh.Vertices)
			{
				vertices.push_back(vertex.Position.x);
				vertices.push_back(vertex.Position.y);
				vertices.push_back(vertex.Position.z);
			}
			for (const uint32_t index : mesh.Indices)
			{
				triangles.push_back(first + int(index));
			}
		}
		tile.SampleMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		const int vertexCount = int(vertices.size() / 3);
		const int triangleCount = int(triangles.size() / 3);
		tile.Triangles = uint32_t(triangleCount);
		if (triangleCount == 0)
		{
			tile.Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
			return tile;
		}
		float verticalLow = std::numeric_limits<float>::max();
		float verticalHigh = -std::numeric_limits<float>::max();
		for (int index = 0; index < vertexCount; ++index)
		{
			verticalLow = std::min(verticalLow, vertices[size_t(index) * 3 + 1]);
			verticalHigh = std::max(verticalHigh, vertices[size_t(index) * 3 + 1]);
		}

		// Recast, a tile with a border its neighbours' edges meet in
		rcConfig config{};
		config.cs = agent.CellSize;
		config.ch = CellHeight;
		config.walkableSlopeAngle = std::clamp(values.WalkSlope, 0.0f, 89.0f);
		config.walkableHeight = agent.Height;
		config.walkableClimb = agent.Climb;
		config.walkableRadius = agent.Radius;
		config.maxEdgeLen = int(12.0f / agent.CellSize);
		config.maxSimplificationError = 1.3f;
		config.minRegionArea = 8 * 8;
		config.mergeRegionArea = 20 * 20;
		config.maxVertsPerPoly = 6;
		config.tileSize = int(TerrainNavigationTileSize / agent.CellSize);
		config.borderSize = agent.Border;
		config.width = config.tileSize + config.borderSize * 2;
		config.height = config.tileSize + config.borderSize * 2;
		config.detailSampleDist = agent.CellSize * 6.0f;
		config.detailSampleMaxError = CellHeight;
		config.bmin[0] = low.x - border;
		config.bmin[1] = verticalLow - 1.0f;
		config.bmin[2] = low.y - border;
		config.bmax[0] = high.x + border;
		config.bmax[1] = verticalHigh + values.AgentHeight + 1.0f;
		config.bmax[2] = high.y + border;

		rcContext context(false);
		rcHeightfield* heightfield = rcAllocHeightfield();
		rcCompactHeightfield* compact = rcAllocCompactHeightfield();
		rcContourSet* contours = rcAllocContourSet();
		rcPolyMesh* polygons = rcAllocPolyMesh();
		rcPolyMeshDetail* detail = rcAllocPolyMeshDetail();
		auto finish = [&]
		{
			rcFreeHeightField(heightfield);
			rcFreeCompactHeightfield(compact);
			rcFreeContourSet(contours);
			rcFreePolyMesh(polygons);
			rcFreePolyMeshDetail(detail);
			tile.Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		};
		std::vector<unsigned char> areas(size_t(triangleCount), RC_NULL_AREA);
		rcMarkWalkableTriangles(&context, config.walkableSlopeAngle, vertices.data(), vertexCount, triangles.data(), triangleCount, areas.data());
		for (int triangle = 0; triangle < triangleCount; ++triangle)
		{
			if (areas[size_t(triangle)] == RC_NULL_AREA)
			{
				continue;
			}
			areas[size_t(triangle)] = TerrainNavigationWalk;
			if (water != nullptr && water->Cells > 0)
			{
				glm::vec3 middle(0.0f);
				for (int corner = 0; corner < 3; ++corner)
				{
					const size_t index = size_t(triangles[size_t(triangle) * 3 + size_t(corner)]) * 3;
					middle += glm::vec3(vertices[index], vertices[index + 1], vertices[index + 2]) / 3.0f;
				}
				const glm::ivec2 cell = water->CellOf(glm::vec2(middle.x, middle.z));
				const size_t at = water->IndexOf(cell.x, cell.y);
				if (water->Body[at] != TerrainWater::EBody::None && water->Level[at] > middle.y + 0.3f)
				{
					areas[size_t(triangle)] = TerrainNavigationSwim;
				}
			}
		}
		if (!rcCreateHeightfield(&context, *heightfield, config.width, config.height, config.bmin, config.bmax, config.cs, config.ch)
			|| !rcRasterizeTriangles(&context, vertices.data(), vertexCount, triangles.data(), areas.data(), triangleCount, *heightfield, config.walkableClimb))
		{
			finish();
			return tile;
		}
		rcFilterLowHangingWalkableObstacles(&context, config.walkableClimb, *heightfield);
		rcFilterLedgeSpans(&context, config.walkableHeight, config.walkableClimb, *heightfield);
		rcFilterWalkableLowHeightSpans(&context, config.walkableHeight, *heightfield);
		if (!rcBuildCompactHeightfield(&context, config.walkableHeight, config.walkableClimb, *heightfield, *compact) || !rcErodeWalkableArea(&context, config.walkableRadius, *compact)
			|| !rcBuildDistanceField(&context, *compact) || !rcBuildRegions(&context, *compact, config.borderSize, config.minRegionArea, config.mergeRegionArea)
			|| !rcBuildContours(&context, *compact, config.maxSimplificationError, config.maxEdgeLen, *contours) || !rcBuildPolyMesh(&context, *contours, config.maxVertsPerPoly, *polygons)
			|| !rcBuildPolyMeshDetail(&context, *polygons, *compact, config.detailSampleDist, config.detailSampleMaxError, *detail) || polygons->npolys == 0)
		{
			finish();
			return tile;
		}
		for (int polygon = 0; polygon < polygons->npolys; ++polygon)
		{
			polygons->flags[polygon] = polygons->areas[polygon] == TerrainNavigationSwim ? uint16_t(TerrainNavigationSwim) : uint16_t(TerrainNavigationWalk);
		}

		// Detour's form of it
		dtNavMeshCreateParams params{};
		params.verts = polygons->verts;
		params.vertCount = polygons->nverts;
		params.polys = polygons->polys;
		params.polyAreas = polygons->areas;
		params.polyFlags = polygons->flags;
		params.polyCount = polygons->npolys;
		params.nvp = polygons->nvp;
		params.detailMeshes = detail->meshes;
		params.detailVerts = detail->verts;
		params.detailVertsCount = detail->nverts;
		params.detailTris = detail->tris;
		params.detailTriCount = detail->ntris;
		params.walkableHeight = values.AgentHeight;
		params.walkableRadius = values.AgentRadius;
		params.walkableClimb = values.StepHeight;
		params.tileX = column.x;
		params.tileY = column.y;
		params.tileLayer = 0;
		std::memcpy(params.bmin, polygons->bmin, sizeof(params.bmin));
		std::memcpy(params.bmax, polygons->bmax, sizeof(params.bmax));
		params.cs = config.cs;
		params.ch = config.ch;
		params.buildBvTree = true;
		unsigned char* data = nullptr;
		int size = 0;
		if (polygons->npolys <= MostTilePolygons && dtCreateNavMeshData(&params, &data, &size))
		{
			tile.Data.assign(data, data + size);
			tile.Polygons = uint32_t(polygons->npolys);
			dtFree(data);
		}
		finish();
		return tile;
	}

	namespace Detail
	{
		// A tile being made on a worker
		struct TerrainNavigationJob
		{
			uint64_t Signature = 0;
			JobHandle Job;
			TerrainNavigationTile Tile;
		};

		struct TerrainNavigationState
		{
			dtNavMesh* Mesh = nullptr;
			dtNavMeshQuery* Query = nullptr;
			// The tiles in, by their column, with what they were made from
			struct Placed
			{
				dtTileRef Ref = 0;
				uint64_t Signature = 0;
				uint32_t Polygons = 0;
			};
			std::map<uint64_t, Placed> Tiles;
			std::map<uint64_t, std::shared_ptr<TerrainNavigationJob>> Jobs;
			// What the terrain was last time, and each wanted column's signature then
			uint64_t TerrainKey = 0;
			std::shared_ptr<const SdfTerrainComponent> From;
			std::map<uint64_t, uint64_t> Signatures;
			bool bSettled = false;

			TerrainNavigationState()
			{
				Mesh = dtAllocNavMesh();
				dtNavMeshParams params{};
				params.orig[0] = params.orig[1] = params.orig[2] = 0.0f;
				params.tileWidth = TerrainNavigationTileSize;
				params.tileHeight = TerrainNavigationTileSize;
				params.maxTiles = MostTiles;
				params.maxPolys = MostTilePolygons;
				Mesh->init(&params);
				Query = dtAllocNavMeshQuery();
				Query->init(Mesh, 4096);
			}

			~TerrainNavigationState()
			{
				dtFreeNavMeshQuery(Query);
				dtFreeNavMesh(Mesh);
			}
		};
	}

	void UpdateTerrainNavigation(Registry& world, std::span<const glm::vec3> viewpoints, JobSystem& jobs)
	{
		NYX_PROFILE_FUNCTION();
		std::vector<Entity> terrains;
		world.Each<SdfTerrainComponent>([&](Entity entity, SdfTerrainComponent&) { terrains.push_back(entity); });
		std::vector<Entity> gone;
		world.Each<TerrainNavigationComponent>([&](Entity entity, TerrainNavigationComponent&)
			{
				if (!world.Has<SdfTerrainComponent>(entity) || !world.Get<SdfTerrainComponent>(entity).Navigation.bEnabled)
				{
					gone.push_back(entity);
				}
			});
		for (const Entity entity : gone)
		{
			world.Remove<TerrainNavigationComponent>(entity);
		}

		for (const Entity entity : terrains)
		{
			const SdfTerrainComponent& terrain = world.Get<SdfTerrainComponent>(entity);
			if (!terrain.Navigation.bEnabled)
			{
				continue;
			}
			if (!world.Has<TerrainNavigationComponent>(entity))
			{
				world.Add<TerrainNavigationComponent>(entity, TerrainNavigationComponent{ std::make_shared<Detail::TerrainNavigationState>() });
			}
			TerrainNavigationComponent& component = world.Get<TerrainNavigationComponent>(entity);
			Detail::TerrainNavigationState& state = *component.State;

			// Made: in, in place of what was there
			for (auto it = state.Jobs.begin(); it != state.Jobs.end();)
			{
				Detail::TerrainNavigationJob& job = *it->second;
				if (!job.Job.IsOver())
				{
					++it;
					continue;
				}
				const auto placed = state.Tiles.find(it->first);
				if (placed != state.Tiles.end())
				{
					state.Mesh->removeTile(placed->second.Ref, nullptr, nullptr);
					state.Tiles.erase(placed);
				}
				Detail::TerrainNavigationState::Placed tile;
				tile.Signature = job.Signature;
				if (!job.Tile.Data.empty())
				{
					auto* data = static_cast<unsigned char*>(dtAlloc(int(job.Tile.Data.size()), DT_ALLOC_PERM));
					std::memcpy(data, job.Tile.Data.data(), job.Tile.Data.size());
					if (dtStatusFailed(state.Mesh->addTile(data, int(job.Tile.Data.size()), DT_TILE_FREE_DATA, 0, &tile.Ref)))
					{
						dtFree(data);
						tile.Ref = 0;
					}
					tile.Polygons = job.Tile.Polygons;
				}
				state.Tiles[it->first] = tile;
				++component.Made;
				component.LastMilliseconds = job.Tile.Milliseconds;
				it = state.Jobs.erase(it);
			}

			// The terrain as it is, again only when its values or edits changed
			const glm::mat4 toTerrain = world.Has<TransformComponent>(entity) ? glm::inverse(world.Get<TransformComponent>(entity).ToMatrix()) : glm::mat4(1.0f);
			uint64_t key = HashSdfTerrainShape(terrain);
			AddToHash(key, terrain.Navigation.Distance);
			AddToHash(key, terrain.Navigation.AgentRadius);
			AddToHash(key, terrain.Navigation.AgentHeight);
			AddToHash(key, terrain.Navigation.StepHeight);
			AddToHash(key, terrain.Navigation.WalkSlope);
			AddToHash(key, terrain.Navigation.CellSize);
			// Whether the water is in yet: the tiles made before it are made again once it is, to swim
			AddToHash(key, world.Has<TerrainWaterComponent>(entity) && world.Get<TerrainWaterComponent>(entity).Water != nullptr);
			// A tile by those and the edits reaching it; all of them looked at again when any came or went
			const uint64_t tileKey = key;
			AddToHash(key, terrain.Edits.size());
			AddToHash(key, terrain.Edits.empty() ? uint64_t(0) : terrain.Edits.back().Revision);
			const bool bTerrainChanged = key != state.TerrainKey;
			if (bTerrainChanged)
			{
				state.TerrainKey = key;
				auto from = std::make_shared<SdfTerrainComponent>(terrain);
				from->Planned = terrain.Planned;
				state.From = std::move(from);
				state.Signatures.clear();
			}

			// The columns wanted round the viewpoints, nearest first; those far past let go of
			std::vector<std::pair<float, glm::ivec2>> wanted;
			std::vector<glm::vec2> around;
			for (const glm::vec3& viewpoint : viewpoints)
			{
				const glm::vec3 local = glm::vec3(toTerrain * glm::vec4(viewpoint, 1.0f));
				around.emplace_back(local.x, local.z);
			}
			const float distance = std::max(terrain.Navigation.Distance, 1.0f);
			std::map<uint64_t, bool> seen;
			for (const glm::vec2& point : around)
			{
				const glm::ivec2 low = glm::ivec2(glm::floor((point - distance) / TerrainNavigationTileSize));
				const glm::ivec2 high = glm::ivec2(glm::floor((point + distance) / TerrainNavigationTileSize));
				for (int j = low.y; j <= high.y; ++j)
				{
					for (int i = low.x; i <= high.x; ++i)
					{
						const glm::vec2 middle = (glm::vec2(float(i), float(j)) + 0.5f) * TerrainNavigationTileSize;
						const float apart = glm::length(middle - point);
						if (apart <= distance + TerrainNavigationTileSize && seen.try_emplace(ColumnKey({ i, j }), true).second)
						{
							wanted.emplace_back(apart, glm::ivec2(i, j));
						}
					}
				}
			}
			std::sort(wanted.begin(), wanted.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
			for (auto it = state.Tiles.begin(); it != state.Tiles.end();)
			{
				const glm::ivec2 column(int32_t(uint32_t(it->first >> 32)), int32_t(uint32_t(it->first)));
				const glm::vec2 middle = (glm::vec2(column) + 0.5f) * TerrainNavigationTileSize;
				bool bNear = false;
				for (const glm::vec2& point : around)
				{
					bNear = bNear || glm::length(middle - point) <= distance * KeepBeyond + TerrainNavigationTileSize;
				}
				if (bNear)
				{
					++it;
					continue;
				}
				if (it->second.Ref != 0)
				{
					state.Mesh->removeTile(it->second.Ref, nullptr, nullptr);
				}
				it = state.Tiles.erase(it);
			}

			// Each wanted column made where it isn't, or was made from other edits than reach it now
			const std::shared_ptr<const TerrainWater> water = world.Has<TerrainWaterComponent>(entity) ? world.Get<TerrainWaterComponent>(entity).Water : nullptr;
			const Agent agent = AgentOf(terrain.Navigation);
			bool bAllIn = true;
			for (const auto& [apart, column] : wanted)
			{
				const uint64_t columnKey = ColumnKey(column);
				auto signature = state.Signatures.find(columnKey);
				if (signature == state.Signatures.end())
				{
					// The edits whose shapes meet it and its border, by their revisions: the ground changes
					// only within an edit's shape, though its values do farther (bakes, which change
					// nothing, left out); and the terrain's shape and values
					const float reach = std::ceil(float(agent.Border) * agent.CellSize) + 2.0f;
					const glm::vec2 low = glm::vec2(column) * TerrainNavigationTileSize - reach;
					const glm::vec2 high = glm::vec2(column + 1) * TerrainNavigationTileSize + reach;
					uint64_t made = tileKey;
					for (const SdfTerrainEdit& edit : state.From->Edits)
					{
						if (edit.Kind == ESdfTerrainEditKind::Bake)
						{
							continue;
						}
						glm::vec3 editLow;
						glm::vec3 editHigh;
						GetTerrainEditBounds(edit, editLow, editHigh);
						if (editHigh.x >= low.x && editLow.x <= high.x && editHigh.z >= low.y && editLow.z <= high.y)
						{
							AddToHash(made, edit.Revision);
						}
					}
					signature = state.Signatures.emplace(columnKey, made).first;
				}
				const auto placed = state.Tiles.find(columnKey);
				const bool bCurrent = placed != state.Tiles.end() && placed->second.Signature == signature->second;
				const auto job = state.Jobs.find(columnKey);
				if (bCurrent)
				{
					continue;
				}
				bAllIn = false;
				if (job != state.Jobs.end() || state.Jobs.size() >= MostInFlight)
				{
					continue;
				}
				auto next = std::make_shared<Detail::TerrainNavigationJob>();
				next->Signature = signature->second;
				next->Job = jobs.Schedule(
					[making = next, from = state.From, water, column]
					{
						making->Tile = MakeTerrainNavigationTile(*from, from->Edits, column, water.get());
					},
					EJobPriority::Normal);
				state.Jobs.emplace(columnKey, std::move(next));
			}
			state.bSettled = bAllIn && state.Jobs.empty();
			component.Tiles = 0;
			component.Polygons = 0;
			for (const auto& [columnKey, tile] : state.Tiles)
			{
				component.Tiles += tile.Ref != 0 ? 1u : 0u;
				component.Polygons += tile.Polygons;
			}
			component.Pending = uint32_t(state.Jobs.size());
		}
	}

	bool IsTerrainNavigationSettled(const TerrainNavigationComponent& component)
	{
		return component.State && component.State->bSettled;
	}

	namespace
	{
		dtQueryFilter MakeFilter(bool bSwim)
		{
			dtQueryFilter filter;
			filter.setIncludeFlags(uint16_t(TerrainNavigationWalk | (bSwim ? TerrainNavigationSwim : 0)));
			filter.setExcludeFlags(0);
			filter.setAreaCost(TerrainNavigationWalk, 1.0f);
			filter.setAreaCost(TerrainNavigationSwim, 3.0f);
			return filter;
		}
	}

	std::optional<glm::vec3> FindTerrainNavigationPoint(const TerrainNavigationComponent& component, const glm::vec3& point)
	{
		if (!component.State)
		{
			return std::nullopt;
		}
		const dtQueryFilter filter = MakeFilter(true);
		dtPolyRef ref = 0;
		float nearest[3];
		if (dtStatusFailed(component.State->Query->findNearestPoly(&point.x, &SearchExtents.x, &filter, &ref, nearest)) || ref == 0)
		{
			return std::nullopt;
		}
		return glm::vec3(nearest[0], nearest[1], nearest[2]);
	}

	std::optional<std::vector<glm::vec3>> FindTerrainNavigationPath(const TerrainNavigationComponent& component, const glm::vec3& from, const glm::vec3& to, bool bSwim)
	{
		if (!component.State)
		{
			return std::nullopt;
		}
		dtNavMeshQuery& query = *component.State->Query;
		const dtQueryFilter filter = MakeFilter(bSwim);
		dtPolyRef start = 0;
		dtPolyRef end = 0;
		float startPoint[3];
		float endPoint[3];
		if (dtStatusFailed(query.findNearestPoly(&from.x, &SearchExtents.x, &filter, &start, startPoint)) || start == 0
			|| dtStatusFailed(query.findNearestPoly(&to.x, &SearchExtents.x, &filter, &end, endPoint)) || end == 0)
		{
			return std::nullopt;
		}
		std::vector<dtPolyRef> corridor(MostPathPolygons);
		int count = 0;
		if (dtStatusFailed(query.findPath(start, end, startPoint, endPoint, &filter, corridor.data(), &count, MostPathPolygons)) || count == 0 || corridor[size_t(count - 1)] != end)
		{
			return std::nullopt;
		}
		std::vector<float> corners(size_t(MostPathPolygons) * 3);
		std::vector<unsigned char> flags(MostPathPolygons);
		std::vector<dtPolyRef> refs(MostPathPolygons);
		int cornerCount = 0;
		if (dtStatusFailed(query.findStraightPath(startPoint, endPoint, corridor.data(), count, corners.data(), flags.data(), refs.data(), &cornerCount, MostPathPolygons)))
		{
			return std::nullopt;
		}
		std::vector<glm::vec3> path;
		for (int corner = 0; corner < cornerCount; ++corner)
		{
			path.emplace_back(corners[size_t(corner) * 3], corners[size_t(corner) * 3 + 1], corners[size_t(corner) * 3 + 2]);
		}
		return path;
	}

	void DrawTerrainNavigation(DebugDraw& drawing, const TerrainNavigationComponent& component, const glm::mat4& toWorld, const glm::vec3& around, float radius)
	{
		if (!component.State)
		{
			return;
		}
		const dtNavMesh& mesh = *component.State->Mesh;
		const glm::vec3 walk(0.1f, 0.9f, 0.2f);
		const glm::vec3 swim(0.2f, 0.5f, 1.0f);
		for (const auto& [columnKey, placed] : component.State->Tiles)
		{
			const dtMeshTile* tile = placed.Ref != 0 ? mesh.getTileByRef(placed.Ref) : nullptr;
			if (tile == nullptr || tile->header == nullptr)
			{
				continue;
			}
			const glm::vec3 low(tile->header->bmin[0], tile->header->bmin[1], tile->header->bmin[2]);
			const glm::vec3 high(tile->header->bmax[0], tile->header->bmax[1], tile->header->bmax[2]);
			if (glm::length(glm::clamp(around, low, high) - around) > radius)
			{
				continue;
			}
			for (int index = 0; index < tile->header->polyCount; ++index)
			{
				const dtPoly& polygon = tile->polys[index];
				const glm::vec3 colour = polygon.getArea() == TerrainNavigationSwim ? swim : walk;
				for (int corner = 0; corner < polygon.vertCount; ++corner)
				{
					const float* a = &tile->verts[size_t(polygon.verts[corner]) * 3];
					const float* b = &tile->verts[size_t(polygon.verts[(corner + 1) % polygon.vertCount]) * 3];
					const glm::vec3 from = glm::vec3(toWorld * glm::vec4(a[0], a[1] + 0.1f, a[2], 1.0f));
					const glm::vec3 to = glm::vec3(toWorld * glm::vec4(b[0], b[1] + 0.1f, b[2], 1.0f));
					drawing.Line(from, to, colour);
				}
			}
		}
	}

	std::vector<InspectedValue> DescribeTerrainNavigation(const TerrainNavigationComponent& component)
	{
		return { { "Tiles", std::format("{} in, {} being made, {} polygons", component.Tiles, component.Pending, component.Polygons) },
			{ "Made", std::format("{} tiles, the last in {:.1f} ms", component.Made, component.LastMilliseconds) } };
	}
}
