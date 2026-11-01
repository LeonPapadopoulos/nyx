#include "TerrainPlanEdits.h"

#include "ReflectedPropertyAccess.h"
#include "SceneDocument.h"
#include "SceneEditor.h"
#include "TerrainWorldPlan.h"
#include "TransactionObjectRefHelpers.h"
#include "TransactionSystem.h"
#include "TransactionTypes.h"

#include <algorithm>
#include <string_view>
#include <utility>

namespace Nyx::Editor
{
	namespace
	{
		size_t PropertyIndex(std::string_view name)
		{
			const Nyx::Reflection::TypeMetadata& type = Nyx::Reflection::GetTypeMetadata<Nyx::SdfTerrainComponent>();
			for (size_t index = 0; index < type.PropertyCount; ++index)
			{
				if (std::string_view(type.Properties[index].Name) == name)
				{
					return index;
				}
			}
			return type.PropertyCount;
		}

		// Changes one of the terrain's properties with `change`, recorded whole before and after as
		// one undo step
		template <typename Change>
		bool ChangeProperty(SceneEditor& editing, Nyx::Entity terrain, std::string_view name, const char* label, Change&& change)
		{
			Nyx::Registry& world = editing.Scene.GetRegistry();
			if (!world.IsAlive(terrain) || !world.Has<Nyx::SdfTerrainComponent>(terrain))
			{
				return false;
			}
			const Nyx::Reflection::TypeMetadata& type = Nyx::Reflection::GetTypeMetadata<Nyx::SdfTerrainComponent>();
			const size_t index = PropertyIndex(name);
			Nyx::SdfTerrainComponent& values = world.Get<Nyx::SdfTerrainComponent>(terrain);
			std::vector<std::byte> before = SaveReflectedProperty(&values, type, index);
			change(values);
			std::vector<std::byte> after = SaveReflectedProperty(&values, type, index);
			if (after == before)
			{
				return false;
			}
			Transaction transaction;
			transaction.Label = label;
			transaction.Changes.push_back({ EChangeKind::ReplaceArray,
				ReplaceArrayChange{ .Target = MakeSceneEntityRef(editing.Scene, terrain),
					.TypeMetadata = &type,
					.PropertyIndex = index,
					.Before = std::move(before),
					.After = std::move(after) } });
			editing.Transactions.Push(std::move(transaction));
			return true;
		}
	}

	std::optional<Nyx::Entity> FindPlannedTerrain(SceneEditor& editing)
	{
		Nyx::Registry& world = editing.Scene.GetRegistry();
		const std::optional<Nyx::Entity>& selected = editing.Scene.GetSelection();
		if (selected && world.IsAlive(*selected) && world.Has<Nyx::SdfTerrainComponent>(*selected) &&
			Nyx::UsesTerrainWorldPlan(world.Get<Nyx::SdfTerrainComponent>(*selected)))
		{
			return selected;
		}
		std::optional<Nyx::Entity> first;
		world.Each<Nyx::SdfTerrainComponent>(
			[&](Nyx::Entity entity, Nyx::SdfTerrainComponent& terrain)
			{
				if (!first && Nyx::UsesTerrainWorldPlan(terrain))
				{
					first = entity;
				}
			});
		return first;
	}

	bool SetTerrainPlanPins(SceneEditor& editing, Nyx::Entity terrain, const std::vector<Nyx::SdfTerrainPlanPin>& pins, const char* label)
	{
		return ChangeProperty(editing, terrain, "Plan", label,
			[&](Nyx::SdfTerrainComponent& values)
			{
				// One gone: the first that differs, or the last
				const std::vector<Nyx::SdfTerrainPlanPin>& before = values.Plan.Pins;
				if (pins.size() + 1 == before.size())
				{
					size_t gone = 0;
					while (gone < pins.size() && pins[gone].Position == before[gone].Position && pins[gone].Landform == before[gone].Landform
						&& pins[gone].Radius == before[gone].Radius)
					{
						++gone;
					}
					for (Nyx::SdfTerrainPlanOverride& override : values.Plan.Overrides)
					{
						if (override.Pin == int32_t(gone))
						{
							override.Points = Nyx::GetTerrainPlanOverridePoints(values.Plan, override);
							override.Pin = -1;
						}
						else if (override.Pin > int32_t(gone))
						{
							--override.Pin;
						}
					}
				}
				values.Plan.Pins = pins;
			});
	}

	bool SetTerrainPlanOverrides(SceneEditor& editing, Nyx::Entity terrain, const std::vector<Nyx::SdfTerrainPlanOverride>& overrides, const char* label)
	{
		return ChangeProperty(editing, terrain, "Plan", label, [&](Nyx::SdfTerrainComponent& values) { values.Plan.Overrides = overrides; });
	}

	Nyx::SdfTerrainPlanOverride AnchorTerrainPlanOverride(const std::vector<Nyx::SdfTerrainPlanPin>& pins, Nyx::SdfTerrainPlanOverride override)
	{
		override.Pin = -1;
		if (override.Points.empty())
		{
			return override;
		}
		for (size_t index = pins.size(); index-- > 0;)
		{
			if (glm::length(override.Points.front() - pins[index].Position) <= pins[index].Radius)
			{
				override.Pin = int32_t(index);
				for (glm::vec2& point : override.Points)
				{
					point -= pins[index].Position;
				}
				break;
			}
		}
		return override;
	}

	std::optional<size_t> FindTerrainPlanOverrideAt(const Nyx::SdfTerrainWorldPlan& plan, const glm::vec2& point, float tolerance)
	{
		for (size_t index = plan.Overrides.size(); index-- > 0;)
		{
			const std::vector<glm::vec2> points = Nyx::GetTerrainPlanOverridePoints(plan, plan.Overrides[index]);
			for (size_t n = 0; n < points.size(); ++n)
			{
				const glm::vec2 a = points[n];
				const glm::vec2 b = n + 1 < points.size() ? points[n + 1] : a;
				const glm::vec2 along = b - a;
				const float length = glm::dot(along, along);
				const float t = length > 0.0f ? std::clamp(glm::dot(point - a, along) / length, 0.0f, 1.0f) : 0.0f;
				if (glm::length(point - (a + along * t)) <= tolerance)
				{
					return index;
				}
			}
		}
		return std::nullopt;
	}

	bool SetTerrainSeed(SceneEditor& editing, Nyx::Entity terrain, uint32_t seed)
	{
		return ChangeProperty(editing, terrain, "Seed", "Terrain Seed", [&](Nyx::SdfTerrainComponent& values) { values.Seed = seed; });
	}

	std::optional<TerrainPinHit> FindTerrainPinAt(const std::vector<Nyx::SdfTerrainPlanPin>& pins, const glm::vec2& point, float tolerance)
	{
		for (size_t index = pins.size(); index-- > 0;)
		{
			for (size_t place = pins[index].Path.size(); place-- > 0;)
			{
				if (glm::length(point - pins[index].Path[place]) <= tolerance)
				{
					return TerrainPinHit{ index, false, int(place) };
				}
			}
		}
		for (size_t index = pins.size(); index-- > 0;)
		{
			if (glm::length(point - pins[index].Position) <= tolerance)
			{
				return TerrainPinHit{ index, false };
			}
		}
		for (size_t index = pins.size(); index-- > 0;)
		{
			if (std::abs(glm::length(point - pins[index].Position) - pins[index].Radius) <= tolerance)
			{
				return TerrainPinHit{ index, true };
			}
		}
		return std::nullopt;
	}
}
