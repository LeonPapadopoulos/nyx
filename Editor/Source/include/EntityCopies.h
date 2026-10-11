#pragma once

#include "Entity.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace Nyx
{
	class SceneDocument;
}

namespace Nyx::Editor
{
	class TransactionSystem;
	struct ITransactionDomain;
	struct EditorTransactionContext;

	// The entity as scene files store it (SceneSerializer::WriteEntity), for copy and paste: it
	// stays readable after the entity is deleted or another scene is opened. Empty if a component
	// can't be written.
	std::vector<std::byte> CopyEntity(const Nyx::SceneDocument& scene, Nyx::Engine::Entity entity);

	// Makes a new entity from what CopyEntity() returned, records it for undo like Add Entity, and
	// returns it. The copy always gets a new guid, since a guid names exactly one entity (undo,
	// files and the game link find entities by it), and a name of its own (MakeCopyName). Returns
	// nothing, and changes nothing, if the copied data can't be read.
	std::optional<Nyx::Engine::Entity> PasteEntity(Nyx::SceneDocument& scene, TransactionSystem& transactions, ITransactionDomain& domain,
		EditorTransactionContext& context, const std::vector<std::byte>& copied, const char* label);

	// A name for a copy that no entity of the scene has: "Cube" and "Cube (2)" become "Cube (2)",
	// or "Cube (3)" if that is taken, and so on
	std::string MakeCopyName(const Nyx::SceneDocument& scene, const std::string& name);
}
