#pragma once

#include "InspectorDrawContext.h"
#include "ReflectionTypes.h"

namespace Nyx::Editor
{
	bool DrawReflectedTypeTable(
		void* object,
		const Nyx::Reflection::TypeMetadata& typeMetadata,
		Nyx::Editor::InspectorDrawContext& drawContext);

	// One property's row (or rows: a struct's, an array's), as the table draws it
	bool DrawReflectedProperty(
		void* object,
		const Nyx::Reflection::TypeMetadata& typeMetadata,
		size_t propertyIndex,
		Nyx::Editor::InspectorDrawContext& drawContext);
}