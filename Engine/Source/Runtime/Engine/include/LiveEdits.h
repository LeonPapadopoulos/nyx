#pragma once

#include "EditorLinkMessages.h"
#include "Entity.h"
#include "NetConnection.h"
#include "SceneSerializationTypes.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// Live edits: the editor sends what it changes in its world to the running game, as editor link
// messages (SetProperties, CreateEntity, DeleteEntity), and the game applies them to its world.
// The messages name entities by guid and components by type name, so the same functions work in
// any program and in either direction.
namespace Nyx::Engine
{
	struct ComponentTypeOps;

	// The sending side: messages that make another program's copy of an entity match this world.

	// The current values of some properties of one component. propertyIndices index the component
	// type's properties. Properties without Serialize aren't sent, as they aren't saved either.
	// Nothing if none are left, or the entity has no guid or no such component.
	std::optional<SetPropertiesMessage> MakeSetPropertiesMessage(
		const Registry& world,
		Entity entity,
		const ComponentTypeOps& componentType,
		const std::vector<size_t>& propertyIndices);

	// The whole entity with all its components. Nothing if it has no guid, or a component can't
	// be written.
	std::optional<CreateEntityMessage> MakeCreateEntityMessage(const Registry& world, Entity entity);

	// The receiving side

	enum class ELiveEditResult : uint8_t
	{
		Applied,
		NotALiveEdit,  // Another message type; nothing happened
		Ignored,       // E.g. for an entity the world doesn't have; logged as a warning
		Unreadable,    // The message is cut off or damaged; logged as an error
	};

	// Applies a SetProperties, CreateEntity or DeleteEntity message to the world:
	// - SetProperties reads the values into the entity's component and runs the component's
	//   post-load step, e.g. to load a changed mesh. Properties the component type doesn't have
	//   (in this build) are skipped with a warning. If the entity doesn't have the component, the
	//   message is ignored: it only holds the values that changed.
	// - CreateEntity adds the entity as it would be loaded from a scene file, and replaces the
	//   entity with the same guid, if there is one.
	// - DeleteEntity destroys the entity with the guid.
	// Messages for entities the world doesn't have are ignored. The context's asset resolver
	// loads the assets that components refer to.
	ELiveEditResult ApplyLiveEdit(const Net::Message& message, Registry& world, ScenePostLoadContext postLoadContext);
}
