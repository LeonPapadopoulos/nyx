#pragma once

#include "Entity.h"

#include <cstddef>
#include <string>
#include <vector>

// Run-time components as the editor shows them: those of a component type registered as Runtime
// (ComponentTypeRegistry::SetRuntime(), RegisterRuntime()), which a world makes while it runs and
// never saves. The editor's Details panel shows them greyed in its Runtime section: the editor's own
// world's, or, while Play runs, a running game's, sent over the editor link (InspectEntity).
namespace Nyx
{
	// One of a component's values as text, e.g. { "Revision", "3" }
	struct InspectedValue
	{
		std::string Name;
		std::string Value;

		friend bool operator==(const InspectedValue&, const InspectedValue&) = default;
	};

	// One run-time component of an entity
	struct InspectedComponent
	{
		// The type's name as registered, e.g. "Nyx::RuntimeMeshComponent", and the name it is shown by
		std::string Type;
		std::string DisplayName;

		// A reflected type's Serialize properties as one property block with its size in front, as
		// ReflectedArchiveSerializer::SerializeObject() writes it; empty for a type without
		// reflected properties
		std::vector<std::byte> Properties;

		// What the type's describe step says of it (ComponentTypeRegistry::SetDescribe()), if it has one
		std::vector<InspectedValue> Values;
	};

	// The entity's run-time components, in the registry's order of types
	std::vector<InspectedComponent> InspectRuntimeComponents(const Registry& world, Entity entity);
}
