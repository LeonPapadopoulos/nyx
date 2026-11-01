#include "NyxPCH.h"
#include "TerrainPlacement.h"

#include "GuidComponent.h"
#include "MeshInstancesComponent.h"
#include "Profiler.h"
#include "SdfTerrain.h"
#include "TerrainChunks.h"
#include "TerrainWorldPlan.h"
#include "TransformComponent.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <format>
#include <limits>
#include <numbers>

namespace Nyx
{
	namespace
	{
		// How many seeds a rule takes at most, and places it tries a place it asks for with each (at
		// most MostTries), checked a batch at a time
		constexpr uint32_t MostRolls = 8;
		constexpr uint32_t TriesPerPlace = 64;
		constexpr uint32_t MostTries = 8192;
		constexpr uint32_t Batch = 256;
		// The ground round a footprint, at its edge; the clearance sampled a metre apart from just
		// above the ground
		constexpr int FootprintPoints = 8;
		constexpr float ClearanceStart = 0.5f;
		constexpr float ClearanceStep = 1.0f;
		// How far above the water's level dry ground is
		constexpr float DryAbove = 0.25f;

		uint64_t Mix(uint64_t value)
		{
			value += 0x9E3779B97F4A7C15ull;
			value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
			value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
			return value ^ (value >> 31);
		}

		uint64_t HashText(std::string_view text)
		{
			uint64_t hash = 0xCBF29CE484222325ull;
			for (const char character : text)
			{
				hash = (hash ^ uint64_t(uint8_t(character))) * 0x100000001B3ull;
			}
			return hash;
		}

		// 0 to 1, from the top 24 bits
		float Unit(uint64_t value)
		{
			return float(value >> 40) * (1.0f / 16777216.0f);
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

		bool Allows(const SdfTerrainPlaceRule& rule, uint8_t landform)
		{
			switch (ESdfTerrainLandform(landform))
			{
			case ESdfTerrainLandform::Plains:
				return rule.bPlains;
			case ESdfTerrainLandform::Roughland:
				return rule.bRoughland;
			case ESdfTerrainLandform::Rift:
				return rule.bRifts;
			case ESdfTerrainLandform::Sea:
				return rule.bSea;
			case ESdfTerrainLandform::Tunnels:
				return rule.bTunnels;
			}
			return false;
		}

		// A place tried: where, and whether the terrain lets it be there (the distances come after)
		struct Try
		{
			glm::vec2 Point{ 0.0f };
			float Yaw = 0.0f;
			float Ground = 0.0f;
			bool bFits = false;
			ETerrainPlaceMiss Miss = ETerrainPlaceMiss::Ground;
		};

		// The column searches' reach at a place: from above the plan's highest top there to below
		// its lowest underside, past a rift's floor
		struct Column
		{
			float Top = 0.0f;
			float Bottom = 0.0f;
		};
		Column ColumnAt(const TerrainWorldPlan& plan, const glm::vec2& point, float reach)
		{
			const SdfTerrainWorldPlan& made = plan.Made;
			const TerrainWorldPlan::Bounds bounds = plan.GetRange(point - reach - TerrainWorldPlan::CellSize, point + reach + TerrainWorldPlan::CellSize);
			const float lowest = -made.Thickness - std::fabs(made.UndersideDepth) - std::fabs(made.RiftDepth) - 50.0f;
			return { bounds.HighHeight + bounds.HighRough + bounds.HighPlains * 2.0f + 4.0f, bounds.LowHeight + lowest };
		}

		// Whether the terrain lets a place of the rule be at the point
		void Check(const SdfTerrainComponent& terrain, const TerrainWorldPlan& plan, const SdfTerrainPlaceRule& rule, Try& place)
		{
			const SdfTerrainWorldPlan& made = plan.Made;
			const glm::vec2 point = place.Point;
			const glm::ivec2 node = plan.NodeOf(point);
			const uint8_t landform = plan.Landform[plan.IndexOf(node.x, node.y)];
			if (landform == TerrainWorldPlan::NotLand || plan.EdgeValue(point.x, point.y) > -rule.Radius || !Allows(rule, landform))
			{
				place.Miss = ETerrainPlaceMiss::Ground;
				return;
			}
			const float radius = std::max(rule.Radius, 0.0f);
			const Column column = ColumnAt(plan, point, radius);
			const std::optional<float> ground = FindTerrainGround(terrain, point, column.Top, column.Bottom);
			if (!ground || *ground < rule.HeightRange.x || *ground > rule.HeightRange.y)
			{
				place.Miss = ETerrainPlaceMiss::Height;
				return;
			}
			place.Ground = *ground;
			if (rule.bDry)
			{
				bool bWet = *ground < made.SeaLevel + DryAbove;
				for (const TerrainWorldPlan::Lake& lake : plan.Lakes)
				{
					bWet = bWet || (*ground < lake.Level + DryAbove && glm::length(point - lake.Center) < made.LakeRadius * 1.5f + radius);
				}
				if (bWet)
				{
					place.Miss = ETerrainPlaceMiss::Water;
					return;
				}
			}

			// The ground round its footprint, no steeper than asked from its middle
			std::array<glm::vec2, FootprintPoints + 1> points;
			std::array<float, FootprintPoints + 1> grounds;
			points[0] = point;
			grounds[0] = *ground;
			const float steepest = std::tan(glm::radians(std::clamp(rule.MaxSlope, 0.0f, 89.0f)));
			for (int n = 0; n < FootprintPoints && radius > 0.0f; ++n)
			{
				const float angle = float(n) * (2.0f * std::numbers::pi_v<float> / float(FootprintPoints));
				points[size_t(n + 1)] = point + glm::vec2(std::cos(angle), std::sin(angle)) * radius;
				const float rise = steepest * radius;
				const std::optional<float> around = FindTerrainGround(terrain, points[size_t(n + 1)], *ground + rise + 2.0f, *ground - rise - 2.0f);
				if (!around || std::fabs(*around - *ground) > rise)
				{
					place.Miss = ETerrainPlaceMiss::Slope;
					return;
				}
				grounds[size_t(n + 1)] = *around;
			}

			// Nothing over it within the clearance
			const size_t columns = radius > 0.0f ? points.size() : 1;
			const int steps = std::max(int(std::ceil((rule.Clearance - ClearanceStart) / ClearanceStep)) + 1, 0);
			std::vector<float> xs;
			std::vector<float> ys;
			std::vector<float> zs;
			for (size_t n = 0; n < columns; ++n)
			{
				for (int step = 0; step < steps; ++step)
				{
					xs.push_back(points[n].x);
					ys.push_back(grounds[n] + std::min(ClearanceStart + float(step) * ClearanceStep, std::max(rule.Clearance, ClearanceStart)));
					zs.push_back(points[n].y);
				}
			}
			std::vector<float> values(xs.size());
			SampleTerrainBatch(terrain, xs.data(), ys.data(), zs.data(), xs.size(), values.data());
			if (std::any_of(values.begin(), values.end(), [](float value) { return value <= 0.0f; }))
			{
				place.Miss = ETerrainPlaceMiss::Clearance;
				return;
			}
			place.bFits = true;
		}
	}

	const char* GetTerrainPlaceMissName(ETerrainPlaceMiss miss)
	{
		switch (miss)
		{
		case ETerrainPlaceMiss::Ground:
			return "kind of ground";
		case ETerrainPlaceMiss::Height:
			return "height";
		case ETerrainPlaceMiss::Water:
			return "water";
		case ETerrainPlaceMiss::Slope:
			return "slope";
		case ETerrainPlaceMiss::Clearance:
			return "clearance";
		case ETerrainPlaceMiss::Distance:
			return "distance";
		case ETerrainPlaceMiss::Count:
			break;
		}
		return "?";
	}

	TerrainPlacement PlaceOnTerrain(const SdfTerrainComponent& terrain, JobSystem* jobs)
	{
		NYX_PROFILE_FUNCTION();
		const auto start = std::chrono::steady_clock::now();
		TerrainPlacement placement;
		placement.Rules.resize(terrain.PlaceRules.size());
		for (TerrainPlacement::RuleResult& result : placement.Rules)
		{
			result.Misses.assign(size_t(ETerrainPlaceMiss::Count), 0);
		}
		const std::shared_ptr<const TerrainWorldPlan> plan = UsesTerrainWorldPlan(terrain) ? GetTerrainWorldPlan(terrain) : nullptr;
		if (!plan || plan->Nodes < 2)
		{
			return placement;
		}
		// The landmass's square, where places are tried
		const glm::vec2 low = plan->Origin;
		const glm::vec2 span = glm::vec2(float(plan->Nodes - 1) * TerrainWorldPlan::CellSize);

		for (uint32_t ruleIndex = 0; ruleIndex < uint32_t(terrain.PlaceRules.size()); ++ruleIndex)
		{
			const SdfTerrainPlaceRule& rule = terrain.PlaceRules[ruleIndex];
			TerrainPlacement::RuleResult& result = placement.Rules[ruleIndex];
			if (!rule.bEnabled || rule.Count == 0)
			{
				continue;
			}
			// An earlier rule's places it must be near; a Near no earlier rule has can't be met
			std::optional<uint32_t> near;
			if (!rule.Near.empty())
			{
				near = FindTerrainPlaceRule(terrain, rule.Near);
				if (near && *near >= ruleIndex)
				{
					near = UINT32_MAX;
				}
			}
			const bool bNearUnmet = !rule.Near.empty() && (!near || *near == UINT32_MAX);
			const size_t first = placement.Places.size();
			auto keeps = [&](const glm::vec2& point)
			{
				for (size_t index = 0; index < placement.Places.size(); ++index)
				{
					const TerrainPlace& other = placement.Places[index];
					const float apart = glm::length(glm::vec2(other.Position.x, other.Position.z) - point);
					const float least = index >= first ? rule.Spacing
														: std::max(rule.Radius, 0.0f) + std::max(terrain.PlaceRules[other.Rule].Radius, 0.0f) + rule.Apart;
					if (apart < least)
					{
						return false;
					}
				}
				if (near)
				{
					float nearest = std::numeric_limits<float>::max();
					for (const TerrainPlace& other : placement.Places)
					{
						if (other.Rule == *near)
						{
							nearest = std::min(nearest, glm::length(glm::vec2(other.Position.x, other.Position.z) - point));
						}
					}
					return nearest >= rule.NearDistance.x && nearest <= rule.NearDistance.y;
				}
				return true;
			};

			const uint64_t ruleSeed = Mix(Mix(uint64_t(terrain.Seed)) ^ HashText(rule.Name));
			const uint32_t tries = std::min(rule.Count * TriesPerPlace, MostTries);
			for (uint32_t roll = 0; roll < MostRolls && result.Found < rule.Count && !bNearUnmet; ++roll)
			{
				++result.Rolls;
				const uint64_t rollSeed = Mix(ruleSeed ^ Mix(uint64_t(roll) + 1));
				std::vector<Try> batch;
				for (uint32_t from = 0; from < tries && result.Found < rule.Count; from += Batch)
				{
					const uint32_t count = std::min(Batch, tries - from);
					batch.assign(count, Try{});
					for (uint32_t n = 0; n < count; ++n)
					{
						const uint64_t hash = Mix(rollSeed ^ Mix(uint64_t(from + n)));
						batch[n].Point = low + glm::vec2(Unit(hash), Unit(Mix(hash))) * span;
						batch[n].Yaw = Unit(Mix(hash ^ 0x5851F42D4C957F2Dull)) * 2.0f * std::numbers::pi_v<float>;
					}
					auto check = [&](size_t n) { Check(terrain, *plan, rule, batch[n]); };
					if (jobs != nullptr)
					{
						jobs->ParallelFor(batch.size(), check, EJobPriority::Normal);
					}
					else
					{
						for (size_t n = 0; n < batch.size(); ++n)
						{
							check(n);
						}
					}
					// Kept in the order tried, so the same places whoever checked them
					for (uint32_t n = 0; n < count && result.Found < rule.Count; ++n)
					{
						++result.Tried;
						const Try& place = batch[n];
						if (!place.bFits)
						{
							++result.Misses[size_t(place.Miss)];
							continue;
						}
						if (!keeps(place.Point))
						{
							++result.Misses[size_t(ETerrainPlaceMiss::Distance)];
							continue;
						}
						placement.Places.push_back({ ruleIndex, glm::vec3(place.Point.x, place.Ground, place.Point.y), place.Yaw, false });
						++result.Found;
					}
				}
			}

			// The template's layout for the rest, on the ground there (or the plan's, where there is none)
			for (size_t index = 0; index < rule.Fallback.size() && result.Found + result.Fallbacks < rule.Count; ++index)
			{
				const glm::vec2 point = rule.Fallback[index];
				const Column column = ColumnAt(*plan, point, 0.0f);
				const float ground = FindTerrainGround(terrain, point, column.Top, column.Bottom).value_or(plan->At(plan->Height, point.x, point.y));
				const float yaw = Unit(Mix(ruleSeed ^ Mix(uint64_t(index) + 0x1000))) * 2.0f * std::numbers::pi_v<float>;
				placement.Places.push_back({ ruleIndex, glm::vec3(point.x, ground, point.y), yaw, true });
				++result.Fallbacks;
			}
		}
		placement.Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		return placement;
	}

	bool IsTerrainPlacementMet(const SdfTerrainComponent& terrain, const TerrainPlacement& placement)
	{
		for (size_t index = 0; index < terrain.PlaceRules.size(); ++index)
		{
			const SdfTerrainPlaceRule& rule = terrain.PlaceRules[index];
			if (rule.bEnabled && (index >= placement.Rules.size() || placement.Rules[index].Found < rule.Count))
			{
				return false;
			}
		}
		return true;
	}

	std::optional<uint32_t> FindTerrainPlaceRule(const SdfTerrainComponent& terrain, std::string_view name)
	{
		for (uint32_t index = 0; index < uint32_t(terrain.PlaceRules.size()); ++index)
		{
			if (terrain.PlaceRules[index].bEnabled && terrain.PlaceRules[index].Name == name)
			{
				return index;
			}
		}
		return std::nullopt;
	}

	uint64_t HashTerrainPlaceRules(const std::vector<SdfTerrainPlaceRule>& rules)
	{
		uint64_t hash = 0xCBF29CE484222325ull;
		AddToHash(hash, rules.size());
		for (const SdfTerrainPlaceRule& rule : rules)
		{
			AddToHash(hash, rule.bEnabled);
			AddToHash(hash, HashText(rule.Name));
			AddToHash(hash, rule.Count);
			AddToHash(hash, rule.Radius);
			AddToHash(hash, rule.MaxSlope);
			AddToHash(hash, rule.Clearance);
			AddToHash(hash, rule.HeightRange);
			AddToHash(hash, rule.bPlains);
			AddToHash(hash, rule.bRoughland);
			AddToHash(hash, rule.bTunnels);
			AddToHash(hash, rule.bRifts);
			AddToHash(hash, rule.bSea);
			AddToHash(hash, rule.bDry);
			AddToHash(hash, rule.Spacing);
			AddToHash(hash, rule.Apart);
			AddToHash(hash, HashText(rule.Near));
			AddToHash(hash, rule.NearDistance);
			AddToHash(hash, rule.Fallback.size());
			for (const glm::vec2& point : rule.Fallback)
			{
				AddToHash(hash, point);
			}
		}
		return hash;
	}

	namespace Detail
	{
		// A finding on a worker: what it was found from, and what it found, taken when it is over
		struct TerrainPlacesWork
		{
			std::shared_ptr<const SdfTerrainComponent> From;
			uint64_t Key = 0;
			JobHandle Job;
			std::shared_ptr<const TerrainPlacement> Placement;
			bool bTaken = false;
		};
	}

	void UpdateTerrainPlaces(Registry& world, JobSystem& jobs)
	{
		NYX_PROFILE_FUNCTION();
		std::vector<Entity> terrains;
		world.Each<SdfTerrainComponent>([&](Entity entity, SdfTerrainComponent&) { terrains.push_back(entity); });
		std::vector<Entity> gone;
		world.Each<TerrainPlacesComponent>([&](Entity entity, TerrainPlacesComponent&)
			{
				if (!world.Has<SdfTerrainComponent>(entity) || world.Get<SdfTerrainComponent>(entity).PlaceRules.empty())
				{
					gone.push_back(entity);
				}
			});
		for (const Entity entity : gone)
		{
			world.Remove<TerrainPlacesComponent>(entity);
		}

		for (const Entity entity : terrains)
		{
			const SdfTerrainComponent& terrain = world.Get<SdfTerrainComponent>(entity);
			if (terrain.PlaceRules.empty() || !UsesTerrainWorldPlan(terrain))
			{
				continue;
			}
			if (!world.Has<TerrainPlacesComponent>(entity))
			{
				world.Add<TerrainPlacesComponent>(entity, TerrainPlacesComponent{});
			}
			TerrainPlacesComponent& component = world.Get<TerrainPlacesComponent>(entity);
			std::shared_ptr<Detail::TerrainPlacesWork>& work = component.Work;
			if (work && !work->bTaken && work->Job.IsOver())
			{
				work->bTaken = true;
				component.Placement = work->Placement;
				component.Key = work->Key;
				++component.Made;
			}
			if (work && !work->bTaken)
			{
				continue;
			}

			// The layers, plan and seed (in the shape) and the rules; never the edits
			uint64_t key = HashSdfTerrainShape(terrain);
			AddToHash(key, HashTerrainPlaceRules(terrain.PlaceRules));
			if (work && work->Key == key)
			{
				continue;
			}
			auto next = std::make_shared<Detail::TerrainPlacesWork>();
			auto from = std::make_shared<SdfTerrainComponent>(terrain);
			from->Edits.clear();
			next->From = std::move(from);
			next->Key = key;
			JobSystem* workers = &jobs;
			next->Job = jobs.Schedule(
				[finding = next, workers] { finding->Placement = std::make_shared<TerrainPlacement>(PlaceOnTerrain(*finding->From, workers)); },
				EJobPriority::Normal);
			work = next;
		}
		UpdateTerrainPlaceCopies(world);
	}

	std::vector<InspectedValue> DescribeTerrainPlaces(const TerrainPlacesComponent& component)
	{
		if (!component.Placement)
		{
			return { { "Places", "being found" } };
		}
		const TerrainPlacement& placement = *component.Placement;
		std::vector<InspectedValue> values;
		for (size_t index = 0; index < placement.Rules.size(); ++index)
		{
			const TerrainPlacement::RuleResult& result = placement.Rules[index];
			if (result.Rolls == 0 && result.Fallbacks == 0)
			{
				continue;
			}
			size_t most = 0;
			for (size_t miss = 1; miss < result.Misses.size(); ++miss)
			{
				most = result.Misses[miss] > result.Misses[most] ? miss : most;
			}
			std::string text = std::format("{} found, {} fallback, {} seed{}, {} tried", result.Found, result.Fallbacks, result.Rolls, result.Rolls == 1 ? "" : "s", result.Tried);
			if (result.Tried > result.Found && !result.Misses.empty())
			{
				text += std::format(", most missed by {}", GetTerrainPlaceMissName(ETerrainPlaceMiss(most)));
			}
			values.push_back({ std::format("Rule {}", index), text });
		}
		values.push_back({ "Found", std::format("{} places, {} times, the last in {:.1f} ms", placement.Places.size(), component.Made, placement.Milliseconds) });
		return values;
	}

	namespace
	{
		glm::mat4 GetTransform(const Registry& world, Entity entity)
		{
			return world.Has<TransformComponent>(entity) ? world.Get<TransformComponent>(entity).ToMatrix() : glm::mat4(1.0f);
		}
	}

	void UpdateTerrainPlaceCopies(Registry& world)
	{
		NYX_PROFILE_FUNCTION();
		world.Each<TerrainPlaceCopiesComponent>(
			[&world](Entity entity, TerrainPlaceCopiesComponent& copies)
			{
				const std::optional<Entity> terrainEntity = copies.Terrain.IsValid() ? FindEntityByGuid(world, copies.Terrain) : std::nullopt;
				const TerrainPlacesComponent* places = terrainEntity && world.Has<TerrainPlacesComponent>(*terrainEntity) ? &world.Get<TerrainPlacesComponent>(*terrainEntity) : nullptr;
				const std::optional<uint32_t> rule = places && world.Has<SdfTerrainComponent>(*terrainEntity) ? FindTerrainPlaceRule(world.Get<SdfTerrainComponent>(*terrainEntity), copies.Rule)
																												 : std::nullopt;
				const glm::mat4 terrainToCopies = terrainEntity ? glm::inverse(GetTransform(world, entity)) * GetTransform(world, *terrainEntity) : glm::mat4(1.0f);

				uint64_t madeFrom = 0xCBF29CE484222325ull;
				AddToHash(madeFrom, places && places->Placement ? places->Key : uint64_t(0));
				AddToHash(madeFrom, rule.value_or(UINT32_MAX));
				AddToHash(madeFrom, copies.Scale);
				AddToHash(madeFrom, terrainToCopies);
				if (copies.MadeFrom == madeFrom && world.Has<MeshInstancesComponent>(entity))
				{
					return;
				}
				auto instances = std::make_shared<MeshInstances>();
				if (rule && places->Placement)
				{
					for (const TerrainPlace& place : places->Placement->Places)
					{
						if (place.Rule != *rule)
						{
							continue;
						}
						glm::mat4 transform = glm::translate(terrainToCopies, place.Position);
						transform = glm::rotate(transform, place.Yaw, glm::vec3(0.0f, 1.0f, 0.0f));
						instances->Transforms.push_back(glm::scale(transform, glm::vec3(copies.Scale)));
					}
				}
				MeshInstancesComponent& component = world.Has<MeshInstancesComponent>(entity) ? world.Get<MeshInstancesComponent>(entity) : world.Add<MeshInstancesComponent>(entity);
				SetMeshInstances(component, std::move(instances));
				copies.MadeFrom = madeFrom;
			});
	}

	void RemoveTerrainPlaceCopies(Registry& world, Entity entity)
	{
		world.Remove<MeshInstancesComponent>(entity);
	}
}
