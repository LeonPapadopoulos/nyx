#pragma once

#include "ComponentTypeRegistry.h"
#include "InspectorDrawContext.h"
#include "ReflectionTypes.h"

// A component's overrides (ValueOverride.h) in the details table: a property with UI = Overrides
// that holds ValueOverride elements, of a component type with a provider of overridable values
// (ComponentTypeRegistry::SetOverridableValues()), is drawn as one row per value the provider lists
// instead of as its array. A value without an override shows what it inherits, greyed, with where it
// comes from in its tooltip; editing it makes its override. An overridden value has a reset arrow,
// which takes its override away. An override the provider doesn't list (renamed, or of a value this
// instance doesn't have) is shown as not listed, with a Remove button. Every edit is one undo step on
// the array, as the array's own rows make them.
namespace Nyx::Editor
{
	// The component type whose provider lists the property's values, or null when the property is
	// drawn as any array: it hasn't UI = Overrides, isn't an array of ValueOverride, or its owner
	// isn't a component type with a provider
	const Nyx::ComponentTypeOps* FindOverridableValuesProvider(
		const Nyx::Reflection::PropertyMetadata& property,
		const Nyx::Reflection::TypeMetadata& ownerType);

	// The property's rows, under a tree row of its own. True if anything changed.
	bool DrawOverrideRows(
		void* component,
		const Nyx::Reflection::PropertyMetadata& property,
		const Nyx::Reflection::TypeMetadata& ownerType,
		const Nyx::ComponentTypeOps& provider,
		InspectorDrawContext& drawContext);
}
