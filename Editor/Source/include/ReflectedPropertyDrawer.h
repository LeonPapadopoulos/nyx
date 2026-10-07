#pragma once

#include "InspectorDrawContext.h"
#include "ReflectionTypes.h"

namespace Nyx::Editor
{
	bool DrawReflectedTypeTable(
		void* object,
		const Nyx::Reflection::TypeMetadata& typeMetadata,
		Nyx::Editor::InspectorDrawContext& drawContext);
}