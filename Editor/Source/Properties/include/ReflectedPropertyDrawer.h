#pragma once

#include "InspectorDrawContext.h"
#include "ReflectionTypes.h"

#include <string_view>

namespace Nyx::Editor
{
	bool DrawReflectedTypeTable(
		void* object,
		const Nyx::Reflection::TypeMetadata& typeMetadata,
		Nyx::Editor::InspectorDrawContext& drawContext);

	// Whether a property of the type has `filter` in its name or its category's, at any depth, so that
	// drawing with it as InspectorDrawContext::PropertyFilter shows something
	bool ReflectedTypeHasMatch(const Nyx::Reflection::TypeMetadata& typeMetadata, std::string_view filter);

	// One property's row (or rows: a struct's, an array's), as the table draws it
	bool DrawReflectedProperty(
		void* object,
		const Nyx::Reflection::TypeMetadata& typeMetadata,
		size_t propertyIndex,
		Nyx::Editor::InspectorDrawContext& drawContext);
}