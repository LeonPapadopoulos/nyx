#include "PrefabTransactions.h"

#include "BinaryArchive.h"
#include "ComponentTypeRegistry.h"
#include "IAssetResolver.h"
#include "Log.h"
#include "Paths.h"
#include "Prefab.h"
#include "ReflectedPropertyAccess.h"
#include <algorithm>
#include "SceneEditor.h"
#include "TransactionObjectRefHelpers.h"

#include <variant>

namespace Nyx::Editor
{
	ObjectRef PrefabTransactionDomain::GetRef(const std::string& prefabPath, Nyx::EntityGuid local)
	{
		const auto key = std::make_pair(prefabPath, local.Value);
		auto found = Ids.find(key);
		if (found == Ids.end())
		{
			const uint64_t id = Ids.size() + 1;
			found = Ids.emplace(key, id).first;
			Refs.emplace(id, std::make_pair(prefabPath, local));
		}
		return ObjectRef{ EObjectDomain::Prefab, InspectorTargetId{ found->second } };
	}

	const std::pair<std::string, Nyx::EntityGuid>* PrefabTransactionDomain::Find(const ObjectRef& ref) const
	{
		if (ref.Domain != EObjectDomain::Prefab)
		{
			return nullptr;
		}
		const auto found = Refs.find(ref.Id.Value);
		return found != Refs.end() ? &found->second : nullptr;
	}

	bool PrefabTransactionDomain::FindEntity(const ObjectRef& root, Nyx::PrefabAsset*& outPrefab, Nyx::Entity& outEntity, bool bChanging)
	{
		const std::pair<std::string, Nyx::EntityGuid>* named = Find(root);
		Nyx::PrefabAsset* prefab = named && Assets && *Assets ? (*Assets)->ResolvePrefab(named->first) : nullptr;
		const std::optional<Nyx::Entity> entity = prefab ? prefab->FindLocal(named->second) : std::nullopt;
		if (!entity)
		{
			return false;
		}
		if (bChanging && BeforeChange)
		{
			BeforeChange(named->first);
		}
		outPrefab = prefab;
		outEntity = *entity;
		return true;
	}

	void* PrefabTransactionDomain::ResolveMutable(EditorTransactionContext& /*context*/, const ObjectRef& root, const Nyx::Reflection::TypeMetadata& typeMetadata)
	{
		Nyx::PrefabAsset* prefab = nullptr;
		Nyx::Entity entity{};
		const Nyx::ComponentTypeOps* ops = Nyx::ComponentTypeRegistry::Get().FindByTypeMetadata(typeMetadata);
		return ops && FindEntity(root, prefab, entity, true) ? ops->Get(prefab->GetEditableTemplate(), entity) : nullptr;
	}

	// A prefab's entities are made and deleted with the prefab, not by undo steps
	bool PrefabTransactionDomain::CreateRootObject(EditorTransactionContext& /*context*/, const ObjectRef& /*root*/)
	{
		return false;
	}

	bool PrefabTransactionDomain::DeleteRootObject(EditorTransactionContext& /*context*/, const ObjectRef& /*root*/)
	{
		return false;
	}

	void PrefabTransactionDomain::EnumerateSubobjects(EditorTransactionContext& /*context*/, const ObjectRef& root, std::vector<ReflectedObjectView>& outSubobjects)
	{
		outSubobjects.clear();
		Nyx::PrefabAsset* prefab = nullptr;
		Nyx::Entity entity{};
		if (!FindEntity(root, prefab, entity, false))
		{
			return;
		}
		Nyx::Registry& world = prefab->GetEditableTemplate();
		for (const Nyx::ComponentTypeOps& ops : Nyx::ComponentTypeRegistry::Get().GetAll())
		{
			if (!ops.IsRuntime() && ops.Has(world, entity))
			{
				outSubobjects.push_back({ ops.TypeMetadata, ops.Get(world, entity) });
			}
		}
	}

	bool PrefabTransactionDomain::EnsureSubobject(EditorTransactionContext& /*context*/, const ObjectRef& root, const Nyx::Reflection::TypeMetadata& typeMetadata)
	{
		Nyx::PrefabAsset* prefab = nullptr;
		Nyx::Entity entity{};
		const Nyx::ComponentTypeOps* ops = Nyx::ComponentTypeRegistry::Get().FindByTypeMetadata(typeMetadata);
		if (!ops || !FindEntity(root, prefab, entity, true))
		{
			return false;
		}
		ops->Add(prefab->GetEditableTemplate(), entity);
		return true;
	}

	bool PrefabTransactionDomain::RemoveSubobject(EditorTransactionContext& /*context*/, const ObjectRef& root, const Nyx::Reflection::TypeMetadata& typeMetadata)
	{
		Nyx::PrefabAsset* prefab = nullptr;
		Nyx::Entity entity{};
		const Nyx::ComponentTypeOps* ops = Nyx::ComponentTypeRegistry::Get().FindByTypeMetadata(typeMetadata);
		if (!ops || !FindEntity(root, prefab, entity, true) || !ops->Has(prefab->GetEditableTemplate(), entity))
		{
			return false;
		}
		ops->Remove(prefab->GetEditableTemplate(), entity);
		return true;
	}

	PrefabSubscriber::PrefabSubscriber(SceneEditor& editing)
		: Editing(editing)
	{
		FindFile = [](const std::string& prefabPath)
		{
			return Nyx::Paths::FindAsset(prefabPath);
		};
	}

	void PrefabSubscriber::Remember(const std::string& prefabPath)
	{
		if (Saved.contains(prefabPath))
		{
			return;
		}
		if (const Nyx::PrefabAsset* prefab = Editing.Assets ? Editing.Assets->ResolvePrefab(prefabPath) : nullptr)
		{
			Saved[prefabPath] = prefab->SaveToMemory();
		}
	}

	void PrefabSubscriber::FileChanged(const std::string& prefabPath)
	{
		if (!Editing.Assets)
		{
			return;
		}

		// Its own saves come back as changes too: nothing to do for those
		Nyx::BinaryReader reader;
		std::vector<std::byte> bytes;
		if (reader.LoadFromFile(FindFile(prefabPath)))
		{
			bytes.resize(reader.GetRemainingSize());
			reader.ReadBytes(bytes.data(), bytes.size());
		}
		const auto saved = Saved.find(prefabPath);
		if (saved != Saved.end() && saved->second == bytes)
		{
			return;
		}

		// As it was: as last saved or read, or as the editor has it now
		Nyx::PrefabAsset before;
		std::string error;
		const Nyx::PrefabAsset* current = Editing.Assets->ResolvePrefab(prefabPath);
		const std::vector<std::byte> beforeBytes = saved != Saved.end() ? saved->second : current ? current->SaveToMemory() : std::vector<std::byte>{};
		if (beforeBytes.empty() || !before.LoadFromMemory(beforeBytes, error))
		{
			Editing.Assets->ForgetPrefab(prefabPath);
			return;
		}

		Editing.Assets->ForgetPrefab(prefabPath);
		const Nyx::PrefabAsset* changed = Editing.Assets->ResolvePrefab(prefabPath);
		if (!changed)
		{
			return;
		}
		Saved[prefabPath] = changed->SaveToMemory();
		ReapplyInstances(prefabPath, before);
		Editing.Hierarchy.Update();
		LOG_INFO("Prefab '{0}' changed: its instances follow it", prefabPath);
	}

	void PrefabSubscriber::OnTransactionCommitted(const Transaction& transaction)
	{
		AfterChange(transaction, false);
	}

	void PrefabSubscriber::OnTransactionApplied(const Transaction& transaction, bool bWasUndo)
	{
		AfterChange(transaction, bWasUndo);
	}

	void PrefabSubscriber::AfterChange(const Transaction& transaction, bool bWasUndo)
	{
		// The prefabs the step changed, each once
		std::vector<std::string> changed;
		for (const Change& change : transaction.Changes)
		{
			const ObjectRef target = std::visit([](const auto& payload) { return payload.Target; }, change.Payload);
			if (const std::pair<std::string, Nyx::EntityGuid>* named = Editing.PrefabDomain.Find(target))
			{
				if (std::find(changed.begin(), changed.end(), named->first) == changed.end())
				{
					changed.push_back(named->first);
				}
			}
		}
		if (changed.empty() || !Editing.Assets)
		{
			return;
		}

		for (const std::string& prefabPath : changed)
		{
			Nyx::PrefabAsset* prefab = Editing.Assets->ResolvePrefab(prefabPath);
			const auto saved = Saved.find(prefabPath);
			Nyx::PrefabAsset before;
			std::string error;
			if (!prefab || saved == Saved.end() || !before.LoadFromMemory(saved->second, error))
			{
				continue;
			}

			prefab->Refresh();
			if (!prefab->SaveToFile(FindFile(prefabPath)))
			{
				LOG_ERROR("Couldn't save prefab '{0}'", prefabPath);
			}
			saved->second = prefab->SaveToMemory();
			ReapplyInstances(prefabPath, before);
		}

		// The scene's entities with the values the step left them with: made again, they would
		// otherwise follow the prefab where the step kept a value, or a component, of their own
		Nyx::Registry& world = Editing.Scene.GetRegistry();
		for (const Change& change : transaction.Changes)
		{
			const SetValueChange* setValue = std::get_if<SetValueChange>(&change.Payload);
			const ReplaceArrayChange* replaceArray = std::get_if<ReplaceArrayChange>(&change.Payload);
			const ObjectRef target = setValue ? setValue->Target : replaceArray ? replaceArray->Target : ObjectRef{};
			const std::optional<Nyx::Entity> entity = target.Domain == EObjectDomain::SceneEntity ? Editing.Scene.FindEntity(GetSceneEntityGuid(target)) : std::nullopt;
			if (!entity)
			{
				continue;
			}
			const SubobjectPath& location = setValue ? setValue->Location : replaceArray->Location;
			const Nyx::Reflection::TypeMetadata* owner = setValue ? setValue->TypeMetadata : replaceArray->TypeMetadata;
			const Nyx::Reflection::TypeMetadata* componentType = location.SubobjectType ? location.SubobjectType : owner;
			const Nyx::ComponentTypeOps* ops = componentType ? Nyx::ComponentTypeRegistry::Get().FindByTypeMetadata(*componentType) : nullptr;
			// A component the step kept that making the instance again took away comes back
			void* component = ops ? ops->Get(world, *entity) : nullptr;
			if (ops && !component && !ops->IsRuntime())
			{
				component = ops->Add(world, *entity);
			}
			const ResolvedObject object = component && location.SubobjectType ? ResolvePropertyPath(component, *componentType, location.PropertyIndices)
																			  : ResolvedObject{ component, componentType };
			if (!object.Object)
			{
				continue;
			}
			if (setValue && setValue->PropertyIndex < owner->PropertyCount)
			{
				WriteReflectedPropertyValue(object.Object, owner->Properties[setValue->PropertyIndex], bWasUndo ? setValue->Before : setValue->After);
			}
			else if (replaceArray)
			{
				LoadReflectedProperty(object.Object, *owner, bWasUndo ? replaceArray->Before : replaceArray->After);
			}
		}
		Editing.Hierarchy.Update();
	}

	void PrefabSubscriber::ReapplyInstances(const std::string& prefabPath, const Nyx::PrefabAsset& before)
	{
		// Parts are made again in new entities: what points at components lets go first, and the
		// selection is found again by guid
		Editing.DetailsContext.CancelPendingEdits();
		Editing.Gizmo.CancelInteraction();
		std::optional<Nyx::Entity>& selection = Editing.Scene.GetSelection();
		const Nyx::EntityGuid selected = selection ? Editing.Scene.GetGuid(*selection) : Nyx::EntityGuid{};

		Nyx::Registry& world = Editing.Scene.GetRegistry();
		std::vector<Nyx::Entity> roots;
		world.Each<Nyx::PrefabInstanceComponent>(
			[&](Nyx::Entity entity, const Nyx::PrefabInstanceComponent& instance)
			{
				if (instance.Prefab.Path == prefabPath)
				{
					roots.push_back(entity);
				}
			});
		Nyx::ScenePostLoadContext context{ .AssetResolver = Editing.Assets };
		for (const Nyx::Entity root : roots)
		{
			if (!Nyx::ReapplyPrefab(world, root, before, context))
			{
				LOG_ERROR("An instance of '{0}' couldn't be made again from it", prefabPath);
			}
		}

		if (selected.IsValid())
		{
			selection = Editing.Scene.FindEntity(selected);
		}
	}
}
