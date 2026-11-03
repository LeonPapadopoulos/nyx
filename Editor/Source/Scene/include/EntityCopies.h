#pragma once

#include "Entity.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Nyx::Editor
{
	class SceneDocument;
	class TransactionSystem;
	struct ITransactionDomain;
	struct EditorTransactionContext;
	struct Transaction;

	// The entity and everything below it (its children, at any depth) as scene files store them
	// (SceneSerializer::WriteEntity), after their count, for copy and paste: it stays readable after
	// they are deleted or another scene is opened. Empty if a component can't be written.
	std::vector<std::byte> CopyEntity(const SceneDocument& scene, Nyx::Entity entity);

	// Makes new entities from what CopyEntity() returned, records them for undo like Add Entity, as
	// one step, and returns the first. Every copy gets a new guid, since a guid names exactly one
	// entity (undo, files and the game link find entities by it); the copied children name their
	// copied parents, and the first keeps the parent the original has. The first gets a name of its
	// own (MakeCopyName). Returns nothing, and changes nothing, if the copied data can't be read. alsoDo, if given, adds to the
	// paste's undo step what else it changes, e.g. the copy's place in the outliner.
	std::optional<Nyx::Entity> PasteEntity(SceneDocument& scene, TransactionSystem& transactions, ITransactionDomain& domain,
		EditorTransactionContext& context, const std::vector<std::byte>& copied, const char* label,
		const std::function<void(Nyx::Entity pasted, Transaction& step)>& alsoDo = {});

	// The same for several copies, as one undo step: returns the first of each. alsoDo is called for
	// each of them. What can't be read is left out.
	std::vector<Nyx::Entity> PasteEntities(SceneDocument& scene, TransactionSystem& transactions, ITransactionDomain& domain,
		EditorTransactionContext& context, const std::vector<std::vector<std::byte>>& copies, const char* label,
		const std::function<void(Nyx::Entity pasted, Transaction& step)>& alsoDo = {});

	// A name for a copy that no entity of the scene has: "Cube" and "Cube (2)" become "Cube (2)",
	// or "Cube (3)" if that is taken, and so on
	std::string MakeCopyName(const SceneDocument& scene, const std::string& name);
}
