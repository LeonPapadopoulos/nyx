#pragma once

#include "Entity.h"
#include "EntityGuid.h"
#include "SceneOutline.h"
#include "TransactionTypes.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Nyx::Editor
{
	class SceneDocument;
	class SceneEditor;

	// The scene as the outliner shows it, and the outliner's edits of its folders and order. The
	// outline is the scene's (SceneDocument::GetOutline(), SceneOutline.h); every edit is one undo
	// step that replaces its arrays (SceneOutlineTransactionDomain). Edits take lists of entities
	// and folders, so a selection of several can be moved as one.

	// One row of the outliner: a folder, with what it holds, or an entity
	struct OutlineNode
	{
		// A folder's id; 0 for an entity
		uint64_t Folder = 0;
		Nyx::Entity Entity{};

		// A folder's subfolders, then its entities
		std::vector<OutlineNode> Children;

		bool IsFolder() const
		{
			return Folder != 0;
		}
	};

	// The rows at the top of the outliner: the top's folders, then its entities. Every entity with
	// a name shows once: where the outline puts it, or, if it doesn't list it, at the top after
	// the listed ones, in the order the registry has their names. A folder whose parent is gone, or
	// whose parents make a loop, shows at the top.
	std::vector<OutlineNode> BuildOutlineTree(const SceneDocument& scene);

	// Whether an entity's name has `filter` in it, whatever the case; every name has an empty one
	bool NameMatchesFilter(std::string_view name, std::string_view filter);

	// The tree with only the entities whose name matches the filter, and the folders that hold
	// one of them, at any depth
	std::vector<OutlineNode> FilterOutlineTree(const std::vector<OutlineNode>& tree, const SceneDocument& scene, std::string_view filter);

	// Where something dragged in the outliner goes
	enum class EOutlinePlace : uint8_t
	{
		// Before or after the row's entity, in its folder; or before or after the row's folder,
		// among its folder's subfolders
		Before,
		After,
		// At the end of the row's folder
		Into,
		// At the end of the top
		End,
	};

	struct OutlineDrop
	{
		EOutlinePlace Place = EOutlinePlace::End;

		// The row dropped on: a folder (its id), or else an entity
		uint64_t Folder = 0;
		Nyx::EntityGuid Entity;
	};

	// What an outliner edit moves, e.g. what is dragged or selected
	struct OutlineItems
	{
		std::vector<Nyx::EntityGuid> Entities;
		std::vector<uint64_t> Folders;
	};

	// Whether the items can go there. Folders come before entities in a folder, so entities go
	// before or after entities and folders before or after folders; a folder never goes into
	// itself or a folder inside it.
	bool CanDropInOutline(const SceneDocument& scene, const OutlineItems& items, const OutlineDrop& drop);

	// Moves the items there, keeping their order, as one undo step ("Move in Outliner"). False,
	// and no step, if they can't go there or are there already.
	bool MoveInOutline(SceneEditor& editing, const OutlineItems& items, const OutlineDrop& drop);

	// A new folder, "New Folder" or "New Folder (2)" and so on so that its siblings' names differ,
	// in `parent` (0 for the top) after its other subfolders, as one undo step. Returns its id.
	uint64_t CreateOutlineFolder(SceneEditor& editing, uint64_t parent);

	// As one undo step; false for an empty name, or the one it has
	bool RenameOutlineFolder(SceneEditor& editing, uint64_t folder, const std::string& name);

	// Deletes the folder, not what is in it: its subfolders and entities go to the folder it was
	// in, where it was, before the entities already there. One undo step.
	bool DeleteOutlineFolder(SceneEditor& editing, uint64_t folder);

	// The folder with this id, if the outline has it (0 never)
	const Nyx::SceneOutlineFolder* FindOutlineFolder(const Nyx::SceneOutline& outline, uint64_t folder);

	// The folder the entity shows in; 0 for the top
	uint64_t GetOutlineFolderOf(const SceneDocument& scene, Nyx::EntityGuid entity);

	// For an entity just made (Add Entity, paste, duplicate): puts it right after `after`, in its
	// folder, if `after` is listed; else at the end of `folder`, 0 for the top. Returns the changes,
	// for the undo step that makes the entity, after the change that adds it.
	std::vector<Change> PlaceNewEntity(SceneDocument& scene, Nyx::EntityGuid entity, Nyx::EntityGuid after, uint64_t folder);

	// The outline as a scene file saves it: without the entities that are gone
	Nyx::SceneOutline MakeSavedOutline(const SceneDocument& scene);
}
