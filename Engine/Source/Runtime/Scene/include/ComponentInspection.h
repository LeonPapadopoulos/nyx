#pragma once

#include "Entity.h"
#include "EntityGuid.h"

#include <cstddef>
#include <optional>
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

	// On an entity the editor is asking about (InspectEntity), while it asks: the game's end of the
	// link marks it, and forgets the mark once the editor stops asking. A game's system that keeps a
	// view of its own for the inspector, such as the terms of what it computed, fills it only for
	// entities with this, so it costs nothing while nobody looks. Not a registered type: never saved,
	// never shown.
	struct InspectionRequestComponent
	{
		// When it was last asked about, in the seconds of the clock MarkInspectionRequest() was given
		double LastAsked = 0.0;
	};

	// How long a mark lasts without being asked again: a few of the editor's intervals (250 ms)
	inline constexpr double InspectionRequestTimeout = 1.0;

	// Marks the entity of that guid as asked about at `now`, if the world has it; returns whether it does
	bool MarkInspectionRequest(Registry& world, EntityGuid entity, double now);
	// Takes the mark off every entity not asked about for `timeout` seconds before `now`; with no
	// `now`, off all
	void ForgetInspectionRequests(Registry& world, std::optional<double> now = std::nullopt, double timeout = InspectionRequestTimeout);
}
