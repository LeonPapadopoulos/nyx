#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>

namespace Nyx::Engine
{
	// Names one entity the same way in its scene file and in every program that loads it, unlike
	// an Entity handle: that is a slot in one Registry, so it changes with every load and differs
	// between the editor and the game. Only entities that are saved or sent to another program
	// need a guid. It is unique within a scene; loading makes sure of that.
	//
	// A random 64-bit number; 0 means "no guid".
	struct EntityGuid
	{
		uint64_t Value = 0;

		// A new random guid, never 0
		static EntityGuid Generate();

		bool IsValid() const
		{
			return Value != 0;
		}

		friend bool operator==(EntityGuid left, EntityGuid right)
		{
			return left.Value == right.Value;
		}

		friend bool operator!=(EntityGuid left, EntityGuid right)
		{
			return left.Value != right.Value;
		}
	};

	// Reflection saves and edits an EntityGuid property as its Value (kind UInt64), so the struct
	// must stay exactly one uint64_t.
	static_assert(std::is_same_v<decltype(EntityGuid::Value), uint64_t> && sizeof(EntityGuid) == sizeof(uint64_t) &&
			std::is_standard_layout_v<EntityGuid>,
		"EntityGuid must stay a single uint64_t");
}

namespace std
{
	// Lets EntityGuid be a key of std::unordered_map and std::unordered_set
	template<>
	struct hash<Nyx::Engine::EntityGuid>
	{
		size_t operator()(Nyx::Engine::EntityGuid guid) const noexcept
		{
			return hash<uint64_t>{}(guid.Value);
		}
	};
}
