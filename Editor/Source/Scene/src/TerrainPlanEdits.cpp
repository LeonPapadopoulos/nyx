#include "TerrainPlanEdits.h"

#include "ReflectedPropertyAccess.h"
#include "SceneDocument.h"
#include "SceneEditor.h"
#include "TransactionObjectRefHelpers.h"
#include "TransactionSystem.h"
#include "TransactionTypes.h"

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

	bool SetTerrainPlanPins(SceneEditor& editing, Nyx::Entity terrain, const std::vector<Nyx::SdfTerrainPlanPin>& pins, const char* label)
	{
		return ChangeProperty(editing, terrain, "Plan", label, [&](Nyx::SdfTerrainComponent& values) { values.Plan.Pins = pins; });
	}

	bool SetTerrainSeed(SceneEditor& editing, Nyx::Entity terrain, uint32_t seed)
	{
		return ChangeProperty(editing, terrain, "Seed", "Terrain Seed", [&](Nyx::SdfTerrainComponent& values) { values.Seed = seed; });
	}

	std::optional<TerrainPinHit> FindTerrainPinAt(const std::vector<Nyx::SdfTerrainPlanPin>& pins, const glm::vec2& point, float tolerance)
	{
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
