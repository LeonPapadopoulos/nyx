#pragma once

#include "ReflectionTypes.h"

#include <cstddef>
#include <vector>

namespace Nyx::Editor
{
	// One subobject (component) of a root object, whole: its Serialize properties as one property
	// block of the tagged format scene files use (ReflectedArchiveSerializer). That keeps struct
	// properties too, such as a MeshRenderer's mesh and material (AssetReference).
	struct SubobjectSnapshot
	{
		const Nyx::Reflection::TypeMetadata* TypeMetadata = nullptr;
		std::vector<std::byte> Properties;
	};

	// A whole root object (entity), to bring it back after a delete, or again after an add
	struct RootObjectSnapshot
	{
		bool bAlive = false;
		std::vector<SubobjectSnapshot> Subobjects;
	};
}