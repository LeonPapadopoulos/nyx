#pragma once

#include "AssetReference.h"
#include "ReflectionMacros.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

// An asset type of the test game module, registered the way a game registers its own (RegisterModule(),
// ReflectedAssetTypes): a creature's stats, in a .nyxcreature file, with every shape an asset's text
// holds
namespace NyxTestModule
{
	NYX_ENUM()
	enum class ETestDiet : uint8_t
	{
		Plants,
		Meat,
		Anything,
	};

	// A struct in the asset: "Senses.Sight = 30"
	NYX_REFLECT()
	struct TestCreatureSenses
	{
		NYX_PROPERTY(Edit, Undo, Serialize)
		float Sight = 10.0f;

		NYX_PROPERTY(Edit, Undo, Serialize)
		float Smell = 0.5f;
	};

	// Named entries of several values: "Move.Bite.Damage = 12"
	NYX_REFLECT()
	struct TestCreatureMove
	{
		NYX_PROPERTY(Edit, Undo, Serialize)
		std::string Name;

		NYX_PROPERTY(Edit, Undo, Serialize)
		float Damage = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize)
		float Reach = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize)
		std::vector<std::string> Tags;
	};

	// Named entries of one value, a number by name: "Resist.Fire = 0.5"
	NYX_REFLECT()
	struct TestCreatureResistance
	{
		NYX_PROPERTY(Edit, Undo, Serialize)
		std::string Name;

		NYX_PROPERTY(Edit, Undo, Serialize)
		float Amount = 0.0f;
	};

	// Named entries on one line, default, min and max: "Range.Hearing = 20 5 40"
	NYX_REFLECT(meta = (Inline))
	struct TestCreatureRange
	{
		NYX_PROPERTY(Edit, Undo, Serialize)
		std::string Name;

		NYX_PROPERTY(Edit, Undo, Serialize)
		float Default = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize)
		float Min = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize)
		float Max = 1.0f;
	};

	// Elements by index: "Waypoints[1].Wait = 2"
	NYX_REFLECT()
	struct TestCreatureWaypoint
	{
		NYX_PROPERTY(Edit, Undo, Serialize)
		glm::vec3 Position{ 0.0f };

		NYX_PROPERTY(Edit, Undo, Serialize)
		float Wait = 0.0f;
	};

	NYX_REFLECT()
	struct TestCreatureData
	{
		NYX_PROPERTY(Edit, Undo, Serialize)
		std::string DisplayName = "Creature";

		NYX_PROPERTY(Edit, Undo, Serialize)
		int32_t Legs = 4;

		NYX_PROPERTY(Edit, Undo, Serialize)
		uint32_t Litter = 2;

		NYX_PROPERTY(Edit, Undo, Serialize)
		bool bNocturnal = false;

		NYX_PROPERTY(Edit, Undo, Serialize)
		float Speed = 3.0f;

		NYX_PROPERTY(Edit, Undo, Serialize)
		ETestDiet Diet = ETestDiet::Plants;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (UI = Color))
		glm::vec3 Tint{ 1.0f };

		NYX_PROPERTY(Edit, Undo, Serialize)
		glm::vec2 Size{ 1.0f };

		NYX_PROPERTY(Edit, Undo, Serialize)
		glm::vec4 Markings{ 0.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (AssetType = Material))
		Nyx::AssetReference Portrait;

		NYX_PROPERTY(Edit, Undo, Serialize)
		std::vector<std::string> Habitats;

		NYX_PROPERTY(Edit, Undo, Serialize)
		std::vector<ETestDiet> Prey;

		NYX_PROPERTY(Edit, Undo, Serialize)
		TestCreatureSenses Senses;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (TextName = Move))
		std::vector<TestCreatureMove> Moves;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (TextName = Resist))
		std::vector<TestCreatureResistance> Resistances;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (TextName = Range))
		std::vector<TestCreatureRange> Ranges;

		NYX_PROPERTY(Edit, Undo, Serialize)
		std::vector<TestCreatureWaypoint> Waypoints;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (AssetType = Material))
		std::vector<Nyx::AssetReference> Skins;

		// Not in the file
		float Hunger = 0.0f;
	};

	inline constexpr const char* TestCreatureExtension = ".nyxcreature";
}

#include "Generated/NyxTestModule/TestCreatureAsset.reflect.h"
