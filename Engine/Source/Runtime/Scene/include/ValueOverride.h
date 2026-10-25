#pragma once

#include "ReflectionMacros.h"

#include <cfloat>
#include <string>
#include <string_view>
#include <vector>

// Values a component inherits from somewhere else, such as a data file or a parent, and overrides one
// by one, as a material instance does its parent's parameters. The component keeps only what it
// overrides, in a property std::vector<ValueOverride> with the metadata UI = Overrides, saved, undone
// and sent to a running game like any array; everything else follows its source. Its type's provider
// (ComponentTypeRegistry::SetOverridableValues()) lists what an instance can override, and the
// editor's Details panel shows each of those: an inherited value greyed, with where it comes from,
// an overridden one with a reset arrow.
namespace Nyx
{
	// One overridden value
	NYX_REFLECT()
	struct ValueOverride
	{
		// The value's name, as the provider lists it (OverridableValue::Name)
		NYX_PROPERTY(Edit, Undo, Serialize)
		std::string Name;

		NYX_PROPERTY(Edit, Undo, Serialize)
		float Value = 0.0f;
	};

	// A value an instance can override, as its type's provider lists it
	struct OverridableValue
	{
		// What a ValueOverride saves; one value of an instance has it
		std::string Name;

		// What the Details panel shows; Name when empty
		std::string DisplayName;

		// Its value without an override
		float Inherited = 0.0f;

		// The range an override is kept in when edited; -FLT_MAX and FLT_MAX for none
		float Min = -FLT_MAX;
		float Max = FLT_MAX;

		std::string Tooltip;

		// Where Inherited comes from, e.g. a file's name, for its tooltip; may be empty
		std::string Source;

		bool HasRange() const
		{
			return Min > -FLT_MAX || Max < FLT_MAX;
		}

		// value in Min..Max
		float Clamp(float value) const;
	};

	// The first override of this name, or null. A later one of the same name is never used.
	const ValueOverride* FindValueOverride(const std::vector<ValueOverride>& overrides, std::string_view name);

	// What an instance uses for the value: its override, or else what it inherits
	float GetOverriddenValue(const std::vector<ValueOverride>& overrides, std::string_view name, float inherited);
}

#include "Generated/Runtime/ValueOverride.reflect.h"
