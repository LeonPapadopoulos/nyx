#include "SceneOutlineEdits.h"

#include "GuidComponent.h"
#include "NameComponent.h"
#include "ParentComponent.h"
#include "ReflectedPropertyAccess.h"
#include "ReflectionUtils.h"
#include "SceneDocument.h"
#include "SceneEditor.h"
#include "SceneOutlineTransactionDomain.h"
#include "TransformComponent.h"
#include "TransformParents.h"

#include <algorithm>
#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace
{
	using namespace Nyx;
	using namespace Nyx::Editor;

	const Reflection::TypeMetadata& OutlineType()
	{
		return Reflection::GetTypeMetadata<SceneOutline>();
	}

	size_t FoldersProperty()
	{
		static const size_t index = Reflection::FindPropertyIndexByName(OutlineType(), "Folders").value();
		return index;
	}

	size_t EntitiesProperty()
	{
		static const size_t index = Reflection::FindPropertyIndexByName(OutlineType(), "Entities").value();
		return index;
	}

	// The entities the outliner shows, those with a name: in the order the registry has their
	// names, and by guid for those that have one
	struct NamedEntities
	{
		std::vector<Entity> InOrder;
		std::unordered_map<EntityGuid, Entity> ByGuid;
	};

	NamedEntities FindNamedEntities(const SceneDocument& scene)
	{
		NamedEntities named;
		scene.GetRegistry().Each<NameComponent>(
			[&](Entity entity, const NameComponent&)
			{
				named.InOrder.push_back(entity);
				const EntityGuid guid = scene.GetGuid(entity);
				if (guid.IsValid())
				{
					named.ByGuid.emplace(guid, entity);
				}
			});
		return named;
	}

	// Leaves out what can't be shown: folders without an id or with one taken already, entities
	// without a guid or listed already. A folder whose parent is gone, or whose parents lead back
	// to it, goes to the top, as does an entity whose folder is gone.
	void Tidy(SceneOutline& outline)
	{
		std::unordered_set<uint64_t> folderIds;
		std::erase_if(outline.Folders,
			[&](const SceneOutlineFolder& folder)
			{
				return folder.Id == 0 || !folderIds.insert(folder.Id).second;
			});

		std::unordered_map<uint64_t, uint64_t> parents;
		for (SceneOutlineFolder& folder : outline.Folders)
		{
			if (!folderIds.contains(folder.Parent))
			{
				folder.Parent = 0;
			}
			parents[folder.Id] = folder.Parent;
		}

		// A loop is broken at the first of its folders met; each walk up is at most as long as there
		// are folders
		for (SceneOutlineFolder& folder : outline.Folders)
		{
			uint64_t parent = parents[folder.Id];
			for (size_t step = 0; parent != 0 && step <= outline.Folders.size(); ++step)
			{
				if (parent == folder.Id)
				{
					folder.Parent = 0;
					parents[folder.Id] = 0;
					break;
				}
				parent = parents[parent];
			}
		}

		std::unordered_set<EntityGuid> listed;
		std::erase_if(outline.Entities,
			[&](const SceneOutlineEntity& entity)
			{
				return !entity.Entity.IsValid() || !listed.insert(entity.Entity).second;
			});
		for (SceneOutlineEntity& entity : outline.Entities)
		{
			if (!folderIds.contains(entity.Folder))
			{
				entity.Folder = 0;
			}
		}
	}

	// The scene's outline, tidied, with every entity the outliner shows listed: those it didn't
	// list at the end, at the top, where the outliner showed them. Edits start from it, so that
	// what they do is what they show.
	SceneOutline MakeCompleteOutline(const SceneDocument& scene, const NamedEntities& named)
	{
		SceneOutline outline = scene.GetOutline();
		Tidy(outline);

		std::unordered_set<EntityGuid> listed;
		for (const SceneOutlineEntity& entity : outline.Entities)
		{
			listed.insert(entity.Entity);
		}
		for (Entity entity : named.InOrder)
		{
			const EntityGuid guid = scene.GetGuid(entity);
			if (guid.IsValid() && listed.insert(guid).second)
			{
				outline.Entities.push_back(SceneOutlineEntity{ .Entity = guid, .Folder = 0 });
			}
		}
		return outline;
	}

	SceneOutline MakeCompleteOutline(const SceneDocument& scene)
	{
		return MakeCompleteOutline(scene, FindNamedEntities(scene));
	}

	// Whether `folder` is `ancestor` or inside it, at any depth, in a tidied outline
	bool IsInside(const SceneOutline& outline, uint64_t folder, uint64_t ancestor)
	{
		for (size_t step = 0; folder != 0 && step <= outline.Folders.size(); ++step)
		{
			if (folder == ancestor)
			{
				return true;
			}
			const SceneOutlineFolder* found = FindOutlineFolder(outline, folder);
			folder = found ? found->Parent : 0;
		}
		return false;
	}

	const SceneOutlineEntity* FindListed(const SceneOutline& outline, EntityGuid entity)
	{
		const auto found = std::find_if(outline.Entities.begin(), outline.Entities.end(),
			[&](const SceneOutlineEntity& listed)
			{
				return listed.Entity == entity;
			});
		return found != outline.Entities.end() ? &*found : nullptr;
	}

	// The folder an entity shows in: its own, or for a child its topmost parent's, whose row it is under
	uint64_t FindShownFolder(const SceneDocument& scene, const SceneOutline& outline, EntityGuid entity)
	{
		const Registry& world = scene.GetRegistry();
		if (const std::optional<Entity> found = scene.FindEntity(entity))
		{
			Entity top = *found;
			for (size_t step = 0; step <= outline.Entities.size(); ++step)
			{
				const std::optional<Entity> parent = FindParent(world, top);
				if (!parent || *parent == *found)
				{
					break;
				}
				top = *parent;
			}
			entity = scene.GetGuid(top);
		}
		const SceneOutlineEntity* listed = FindListed(outline, entity);
		return listed ? listed->Folder : 0;
	}

	// The entities named by the items that the scene has
	std::vector<Entity> FindItemEntities(const SceneDocument& scene, const OutlineItems& items)
	{
		std::vector<Entity> entities;
		for (const EntityGuid guid : items.Entities)
		{
			if (const std::optional<Entity> entity = scene.FindEntity(guid))
			{
				entities.push_back(*entity);
			}
		}
		return entities;
	}

	// The parent a drop gives the entities: the row's entity for Onto, its parent beside it, none in a
	// folder, beside one or at the end of the top
	std::optional<Entity> FindDropParent(const SceneDocument& scene, const OutlineDrop& drop)
	{
		const std::optional<Entity> target = drop.Folder == 0 && drop.Entity.IsValid() ? scene.FindEntity(drop.Entity) : std::nullopt;
		if (!target)
		{
			return std::nullopt;
		}
		if (drop.Place == EOutlinePlace::Onto)
		{
			return target;
		}
		if (drop.Place == EOutlinePlace::Before || drop.Place == EOutlinePlace::After)
		{
			return FindParent(scene.GetRegistry(), *target);
		}
		return std::nullopt;
	}

	bool IsSameOutline(const SceneOutline& a, const SceneOutline& b)
	{
		return SaveReflectedProperty(&a, OutlineType(), FoldersProperty()) == SaveReflectedProperty(&b, OutlineType(), FoldersProperty()) &&
			SaveReflectedProperty(&a, OutlineType(), EntitiesProperty()) == SaveReflectedProperty(&b, OutlineType(), EntitiesProperty());
	}

	// Makes `after` the scene's outline. Returns the changes of its arrays that differ, for undo.
	std::vector<Change> ReplaceOutline(SceneDocument& scene, SceneOutline after)
	{
		SceneOutline& outline = scene.GetOutline();
		std::vector<Change> changes;
		for (const size_t property : { FoldersProperty(), EntitiesProperty() })
		{
			std::vector<std::byte> before = SaveReflectedProperty(&outline, OutlineType(), property);
			std::vector<std::byte> changed = SaveReflectedProperty(&after, OutlineType(), property);
			if (before != changed)
			{
				changes.push_back(Change{ EChangeKind::ReplaceArray,
					ReplaceArrayChange{ .Target = SceneOutlineTransactionDomain::GetRef(),
						.TypeMetadata = &OutlineType(),
						.PropertyIndex = property,
						.Before = std::move(before),
						.After = std::move(changed) } });
			}
		}
		outline = std::move(after);
		return changes;
	}

	// Makes `after` the scene's outline as one undo step, after `changes` already made, e.g. new
	// parents; nothing if there are none and the outliner would show the same
	bool CommitOutline(SceneEditor& editing, const char* label, SceneOutline after, std::vector<Change> changes = {})
	{
		if (changes.empty() && IsSameOutline(MakeCompleteOutline(editing.Scene), after))
		{
			return false;
		}

		Transaction transaction;
		transaction.Label = label;
		transaction.Changes = std::move(changes);
		for (Change& change : ReplaceOutline(editing.Scene, std::move(after)))
		{
			transaction.Changes.push_back(std::move(change));
		}
		if (transaction.IsEmpty())
		{
			return false;
		}
		editing.Transactions.Push(std::move(transaction));
		return true;
	}

	// Takes the elements `bMoved` picks out of `elements`, in their order, and puts them back at
	// `position`, an index among the others, after `change` was done to each
	template<typename T, typename Picks, typename Edit>
	void MoveElements(std::vector<T>& elements, Picks bMoved, Edit change, const std::function<size_t(const std::vector<T>&)>& position)
	{
		std::vector<T> moved;
		std::vector<T> others;
		for (T& element : elements)
		{
			(bMoved(element) ? moved : others).push_back(std::move(element));
		}
		for (T& element : moved)
		{
			change(element);
		}
		const size_t at = std::min(position(others), others.size());
		others.insert(others.begin() + static_cast<ptrdiff_t>(at), std::make_move_iterator(moved.begin()), std::make_move_iterator(moved.end()));
		elements = std::move(others);
	}

	// An entity's row with its children's below it, at any depth
	OutlineNode BuildEntityNode(Entity entity, const std::unordered_map<uint32_t, std::vector<Entity>>& children)
	{
		OutlineNode node{ .Entity = entity };
		if (const auto found = children.find(entity.Index()); found != children.end())
		{
			for (const Entity child : found->second)
			{
				node.Children.push_back(BuildEntityNode(child, children));
			}
		}
		return node;
	}

	std::vector<OutlineNode> BuildChildren(uint64_t folder,
		const std::unordered_map<uint64_t, std::vector<uint64_t>>& subfolders,
		const std::unordered_map<uint64_t, std::vector<Entity>>& entities,
		const std::unordered_map<uint32_t, std::vector<Entity>>& children)
	{
		std::vector<OutlineNode> nodes;
		if (const auto found = subfolders.find(folder); found != subfolders.end())
		{
			for (const uint64_t subfolder : found->second)
			{
				nodes.push_back(OutlineNode{ .Folder = subfolder, .Children = BuildChildren(subfolder, subfolders, entities, children) });
			}
		}
		if (const auto found = entities.find(folder); found != entities.end())
		{
			for (const Entity entity : found->second)
			{
				nodes.push_back(BuildEntityNode(entity, children));
			}
		}
		return nodes;
	}

	// The parent the outliner shows an entity under: its parent, if that shows too and the chain
	// above it doesn't go round in a circle; else none, and it shows in its folder
	std::optional<Entity> FindShownParent(const SceneDocument& scene, const NamedEntities& named, Entity entity)
	{
		const Registry& world = scene.GetRegistry();
		const auto shownParent = [&](Entity of) -> std::optional<Entity>
		{
			if (!world.Has<ParentComponent>(of))
			{
				return std::nullopt;
			}
			const auto found = named.ByGuid.find(world.Get<ParentComponent>(of).Parent);
			return found != named.ByGuid.end() && found->second != of ? std::optional<Entity>(found->second) : std::nullopt;
		};

		const std::optional<Entity> parent = shownParent(entity);
		std::optional<Entity> above = parent;
		for (size_t step = 0; above; ++step)
		{
			if (*above == entity || step > named.InOrder.size())
			{
				return std::nullopt;
			}
			above = shownParent(*above);
		}
		return parent;
	}

	// Takes the folder out, not what is in it: that goes where the folder was
	bool RemoveFolder(SceneOutline& outline, uint64_t folder)
	{
		const auto found = std::find_if(outline.Folders.begin(), outline.Folders.end(),
			[&](const SceneOutlineFolder& candidate)
			{
				return candidate.Id == folder;
			});
		if (found == outline.Folders.end())
		{
			return false;
		}
		const uint64_t parent = found->Parent;

		// Its subfolders where it was, among the others of its parent: after as many of the folders
		// that stay as were before it
		size_t staying = 0;
		for (auto before = outline.Folders.begin(); before != found; ++before)
		{
			staying += before->Parent != folder ? 1 : 0;
		}
		outline.Folders.erase(found);
		MoveElements<SceneOutlineFolder>(
			outline.Folders,
			[&](const SceneOutlineFolder& candidate)
			{
				return candidate.Parent == folder;
			},
			[&](SceneOutlineFolder& subfolder)
			{
				subfolder.Parent = parent;
			},
			[&](const std::vector<SceneOutlineFolder>&)
			{
				return staying;
			});

		// Its entities first among those of its parent, as it showed above them
		MoveElements<SceneOutlineEntity>(
			outline.Entities,
			[&](const SceneOutlineEntity& entity)
			{
				return entity.Folder == folder;
			},
			[&](SceneOutlineEntity& entity)
			{
				entity.Folder = parent;
			},
			[&](const std::vector<SceneOutlineEntity>& others)
			{
				const auto first = std::find_if(others.begin(), others.end(),
					[&](const SceneOutlineEntity& entity)
					{
						return entity.Folder == parent;
					});
				return static_cast<size_t>(first - others.begin());
			});

		return true;
	}
}

namespace Nyx::Editor
{
	std::vector<OutlineNode> BuildOutlineTree(const SceneDocument& scene)
	{
		const NamedEntities named = FindNamedEntities(scene);
		SceneOutline outline = scene.GetOutline();
		Tidy(outline);

		std::unordered_map<uint64_t, std::vector<uint64_t>> subfolders;
		for (const SceneOutlineFolder& folder : outline.Folders)
		{
			subfolders[folder.Parent].push_back(folder.Id);
		}

		// In the outline's order, then those it doesn't list; children under their parents, in that
		// order among their siblings, whatever folder the outline gives them
		std::vector<std::pair<Entity, uint64_t>> ordered;
		std::unordered_set<EntityGuid> shown;
		for (const SceneOutlineEntity& listed : outline.Entities)
		{
			if (const auto found = named.ByGuid.find(listed.Entity); found != named.ByGuid.end())
			{
				ordered.emplace_back(found->second, listed.Folder);
				shown.insert(listed.Entity);
			}
		}
		for (const Entity entity : named.InOrder)
		{
			const EntityGuid guid = scene.GetGuid(entity);
			if (!guid.IsValid() || !shown.contains(guid))
			{
				ordered.emplace_back(entity, 0);
			}
		}

		std::unordered_map<uint64_t, std::vector<Entity>> entities;
		std::unordered_map<uint32_t, std::vector<Entity>> children;
		for (const auto& [entity, folder] : ordered)
		{
			if (const std::optional<Entity> parent = FindShownParent(scene, named, entity))
			{
				children[parent->Index()].push_back(entity);
			}
			else
			{
				entities[folder].push_back(entity);
			}
		}

		return BuildChildren(0, subfolders, entities, children);
	}

	std::vector<OutlineNode> FilterOutlineTree(const std::vector<OutlineNode>& tree, const SceneDocument& scene, std::string_view filter)
	{
		if (filter.empty())
		{
			return tree;
		}

		std::vector<OutlineNode> kept;
		for (const OutlineNode& node : tree)
		{
			if (node.IsFolder())
			{
				OutlineNode folder{ .Folder = node.Folder, .Children = FilterOutlineTree(node.Children, scene, filter) };
				if (!folder.Children.empty())
				{
					kept.push_back(std::move(folder));
				}
			}
			else
			{
				// An entity whose name matches, or one with a child below it that does
				OutlineNode entity{ .Entity = node.Entity, .Children = FilterOutlineTree(node.Children, scene, filter) };
				if (!entity.Children.empty() || NameMatchesFilter(scene.GetRegistry().Get<NameComponent>(node.Entity).Name, filter))
				{
					kept.push_back(std::move(entity));
				}
			}
		}
		return kept;
	}

	OutlineItems LeaveOutWhatGoesAlong(const SceneDocument& scene, const OutlineItems& items)
	{
		const Registry& world = scene.GetRegistry();
		const SceneOutline outline = MakeCompleteOutline(scene);
		const auto isInListedFolder = [&](uint64_t folder)
		{
			return std::any_of(items.Folders.begin(), items.Folders.end(),
				[&](uint64_t listed)
				{
					return IsInside(outline, folder, listed);
				});
		};

		OutlineItems left;
		for (const uint64_t folder : items.Folders)
		{
			const SceneOutlineFolder* found = FindOutlineFolder(outline, folder);
			if (!found || !isInListedFolder(found->Parent))
			{
				left.Folders.push_back(folder);
			}
		}
		const std::vector<Entity> entities = FindItemEntities(scene, items);
		for (const Entity entity : entities)
		{
			const bool bBelowAnother = std::any_of(entities.begin(), entities.end(),
				[&](Entity other)
				{
					return other != entity && IsSelfOrBelow(world, entity, other);
				});
			const EntityGuid guid = scene.GetGuid(entity);
			if (!bBelowAnother && !isInListedFolder(GetOutlineFolderOf(scene, guid)))
			{
				left.Entities.push_back(guid);
			}
		}
		return left;
	}

	bool CanDropInOutline(const SceneDocument& scene, const OutlineItems& items, const OutlineDrop& drop)
	{
		if (items.Entities.empty() && items.Folders.empty())
		{
			return false;
		}

		SceneOutline outline = MakeCompleteOutline(scene);

		// An entity given a parent needs a transform, and never goes below itself: not onto or beside
		// an entity below it
		const Registry& world = scene.GetRegistry();
		const std::vector<Entity> entities = FindItemEntities(scene, items);
		const std::optional<Entity> target = drop.Folder == 0 && drop.Entity.IsValid() ? scene.FindEntity(drop.Entity) : std::nullopt;
		const std::optional<Entity> parent = FindDropParent(scene, drop);
		for (const Entity entity : entities)
		{
			if ((target && IsSelfOrBelow(world, *target, entity)) || (parent && !world.Has<TransformComponent>(entity)))
			{
				return false;
			}
		}
		if (drop.Place == EOutlinePlace::Onto)
		{
			return target && items.Folders.empty() && world.Has<TransformComponent>(*target);
		}

		const auto movesFolderInto = [&](uint64_t folder)
		{
			return std::any_of(items.Folders.begin(), items.Folders.end(),
				[&](uint64_t moved)
				{
					return IsInside(outline, folder, moved);
				});
		};

		switch (drop.Place)
		{
		case EOutlinePlace::Before:
		case EOutlinePlace::After:
			if (drop.Folder != 0)
			{
				const SceneOutlineFolder* folder = FindOutlineFolder(outline, drop.Folder);
				return folder && items.Entities.empty() && !movesFolderInto(drop.Folder) && !movesFolderInto(folder->Parent);
			}
			return items.Folders.empty() && FindListed(outline, drop.Entity) &&
				std::find(items.Entities.begin(), items.Entities.end(), drop.Entity) == items.Entities.end();

		case EOutlinePlace::Into:
			return FindOutlineFolder(outline, drop.Folder) && !movesFolderInto(drop.Folder);

		case EOutlinePlace::End:
			return true;
		}
		return false;
	}

	bool MoveInOutline(SceneEditor& editing, const OutlineItems& items, const OutlineDrop& drop)
	{
		if (!CanDropInOutline(editing.Scene, items, drop))
		{
			return false;
		}

		SceneOutline outline = MakeCompleteOutline(editing.Scene);
		const bool bBeside = drop.Place == EOutlinePlace::Before || drop.Place == EOutlinePlace::After;
		const size_t after = drop.Place == EOutlinePlace::After ? 1 : 0;

		// The folder they go into
		uint64_t target = 0;
		if (drop.Place == EOutlinePlace::Into)
		{
			target = drop.Folder;
		}
		else if (bBeside && drop.Folder != 0)
		{
			target = FindOutlineFolder(outline, drop.Folder)->Parent;
		}
		else if (bBeside || drop.Place == EOutlinePlace::Onto)
		{
			// Beside a child, or onto an entity: where its topmost parent shows, so the entities
			// show there if they lose their parent later
			target = FindShownFolder(editing.Scene, outline, drop.Entity);
		}

		const std::unordered_set<EntityGuid> entities(items.Entities.begin(), items.Entities.end());
		MoveElements<SceneOutlineEntity>(
			outline.Entities,
			[&](const SceneOutlineEntity& entity)
			{
				return entities.contains(entity.Entity);
			},
			[&](SceneOutlineEntity& entity)
			{
				entity.Folder = target;
			},
			[&](const std::vector<SceneOutlineEntity>& others)
			{
				const auto beside = std::find_if(others.begin(), others.end(),
					[&](const SceneOutlineEntity& entity)
					{
						return bBeside && drop.Folder == 0 && entity.Entity == drop.Entity;
					});
				return beside != others.end() ? static_cast<size_t>(beside - others.begin()) + after : others.size();
			});

		const std::unordered_set<uint64_t> folders(items.Folders.begin(), items.Folders.end());
		MoveElements<SceneOutlineFolder>(
			outline.Folders,
			[&](const SceneOutlineFolder& folder)
			{
				return folders.contains(folder.Id);
			},
			[&](SceneOutlineFolder& folder)
			{
				folder.Parent = target;
			},
			[&](const std::vector<SceneOutlineFolder>& others)
			{
				const auto beside = std::find_if(others.begin(), others.end(),
					[&](const SceneOutlineFolder& folder)
					{
						return bBeside && folder.Id == drop.Folder;
					});
				return beside != others.end() ? static_cast<size_t>(beside - others.begin()) + after : others.size();
			});

		// The parent the place gives them, keeping each where it is in the world
		std::vector<Change> parentChanges;
		const std::optional<Entity> parent = FindDropParent(editing.Scene, drop);
		for (const Entity entity : FindItemEntities(editing.Scene, items))
		{
			if (FindParent(editing.Scene.GetRegistry(), entity) != parent)
			{
				ChangeParent(editing.Scene, entity, parent, parentChanges);
			}
		}

		return CommitOutline(editing, parent ? "Set Parent" : "Move in Outliner", std::move(outline), std::move(parentChanges));
	}

	uint64_t CreateOutlineFolder(SceneEditor& editing, uint64_t parent)
	{
		SceneOutline outline = MakeCompleteOutline(editing.Scene);
		if (!FindOutlineFolder(outline, parent))
		{
			parent = 0;
		}

		uint64_t id = 0;
		while (id == 0 || FindOutlineFolder(outline, id))
		{
			id = Nyx::EntityGuid::Generate().Value;
		}

		std::string name = "New Folder";
		for (int number = 2; std::any_of(outline.Folders.begin(), outline.Folders.end(),
								 [&](const SceneOutlineFolder& sibling)
								 {
									 return sibling.Parent == parent && sibling.Name == name;
								 });
			 ++number)
		{
			name = "New Folder (" + std::to_string(number) + ")";
		}

		outline.Folders.push_back(SceneOutlineFolder{ .Id = id, .Name = std::move(name), .Parent = parent });
		return CommitOutline(editing, "New Folder", std::move(outline)) ? id : 0;
	}

	bool RenameOutlineFolder(SceneEditor& editing, uint64_t folder, const std::string& name)
	{
		SceneOutline outline = MakeCompleteOutline(editing.Scene);
		const auto found = std::find_if(outline.Folders.begin(), outline.Folders.end(),
			[&](const SceneOutlineFolder& candidate)
			{
				return candidate.Id == folder;
			});
		if (found == outline.Folders.end() || name.empty() || found->Name == name)
		{
			return false;
		}

		found->Name = name;
		return CommitOutline(editing, "Rename Folder", std::move(outline));
	}

	bool DeleteOutlineFolder(SceneEditor& editing, uint64_t folder)
	{
		return DeleteOutlineFolders(editing, { folder });
	}

	bool DeleteOutlineFolders(SceneEditor& editing, const std::vector<uint64_t>& folders)
	{
		SceneOutline outline = MakeCompleteOutline(editing.Scene);
		bool bAny = false;
		for (const uint64_t folder : folders)
		{
			bAny |= RemoveFolder(outline, folder);
		}
		return bAny && CommitOutline(editing, folders.size() > 1 ? "Delete Folders" : "Delete Folder", std::move(outline));
	}

	const Nyx::SceneOutlineFolder* FindOutlineFolder(const Nyx::SceneOutline& outline, uint64_t folder)
	{
		if (folder == 0)
		{
			return nullptr;
		}
		const auto found = std::find_if(outline.Folders.begin(), outline.Folders.end(),
			[&](const SceneOutlineFolder& candidate)
			{
				return candidate.Id == folder;
			});
		return found != outline.Folders.end() ? &*found : nullptr;
	}

	uint64_t GetOutlineFolderOf(const SceneDocument& scene, Nyx::EntityGuid entity)
	{
		SceneOutline outline = scene.GetOutline();
		Tidy(outline);
		return FindShownFolder(scene, outline, entity);
	}

	std::vector<Change> PlaceNewEntity(SceneDocument& scene, Nyx::EntityGuid entity, Nyx::EntityGuid after, uint64_t folder)
	{
		// Lists the new entity at the end
		SceneOutline outline = MakeCompleteOutline(scene);
		const auto placed = std::find_if(outline.Entities.begin(), outline.Entities.end(),
			[&](const SceneOutlineEntity& listed)
			{
				return listed.Entity == entity;
			});
		if (!entity.IsValid() || placed == outline.Entities.end())
		{
			return {};
		}
		SceneOutlineEntity place = *placed;
		outline.Entities.erase(placed);

		const auto beside = std::find_if(outline.Entities.begin(), outline.Entities.end(),
			[&](const SceneOutlineEntity& listed)
			{
				return after.IsValid() && listed.Entity == after;
			});
		if (beside != outline.Entities.end())
		{
			place.Folder = beside->Folder;
			outline.Entities.insert(beside + 1, place);
		}
		else
		{
			place.Folder = FindOutlineFolder(outline, folder) ? folder : 0;
			outline.Entities.push_back(place);
		}
		return ReplaceOutline(scene, std::move(outline));
	}

	Nyx::SceneOutline MakeSavedOutline(const SceneDocument& scene)
	{
		const NamedEntities named = FindNamedEntities(scene);
		SceneOutline outline = MakeCompleteOutline(scene, named);
		std::erase_if(outline.Entities,
			[&](const SceneOutlineEntity& listed)
			{
				return !named.ByGuid.contains(listed.Entity);
			});
		return outline;
	}
}
