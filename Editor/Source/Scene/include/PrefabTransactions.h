#pragma once

#include "Entity.h"
#include "EntityGuid.h"
#include "TransactionDomain.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Nyx
{
	class IAssetResolver;
	class PrefabAsset;
}

namespace Nyx::Editor
{
	class SceneEditor;

	// The prefabs' own entities in undo steps (EObjectDomain::Prefab): each entity of a prefab's
	// template is a root object, by the prefab's path and the entity's id in it, its components its
	// subobjects. Apply to Prefab changes them as any edit changes a scene's entities, so undo and redo
	// work as for those; PrefabSubscriber then saves the prefab and makes its instances again.
	class PrefabTransactionDomain final : public ITransactionDomain
	{
	public:
		// Where the prefabs come from: the resolver `assets` points at (SceneEditor::Assets), which
		// may be set later
		void SetAssets(Nyx::IAssetResolver* const* assets)
		{
			Assets = assets;
		}

		// Called before a prefab's template is changed through this domain, with its path
		std::function<void(const std::string& prefabPath)> BeforeChange;

		// The prefab's entity `local` as an undo step names it
		ObjectRef GetRef(const std::string& prefabPath, Nyx::EntityGuid local);

		// Which prefab and entity a reference names, if this domain made it
		const std::pair<std::string, Nyx::EntityGuid>* Find(const ObjectRef& ref) const;

		void* ResolveMutable(EditorTransactionContext& context, const ObjectRef& root, const Nyx::Reflection::TypeMetadata& typeMetadata) override;
		bool CreateRootObject(EditorTransactionContext& context, const ObjectRef& root) override;
		bool DeleteRootObject(EditorTransactionContext& context, const ObjectRef& root) override;
		void EnumerateSubobjects(EditorTransactionContext& context, const ObjectRef& root, std::vector<ReflectedObjectView>& outSubobjects) override;
		bool EnsureSubobject(EditorTransactionContext& context, const ObjectRef& root, const Nyx::Reflection::TypeMetadata& typeMetadata) override;
		bool RemoveSubobject(EditorTransactionContext& context, const ObjectRef& root, const Nyx::Reflection::TypeMetadata& typeMetadata) override;

	private:
		// The template's entity a reference names, and its world; false if there is none
		bool FindEntity(const ObjectRef& root, Nyx::PrefabAsset*& outPrefab, Nyx::Entity& outEntity, bool bChanging);

		Nyx::IAssetResolver* const* Assets = nullptr;
		std::map<std::pair<std::string, uint64_t>, uint64_t> Ids;
		std::unordered_map<uint64_t, std::pair<std::string, Nyx::EntityGuid>> Refs;
	};

	// After an edit, undo or redo of a prefab's entities (PrefabTransactionDomain): saves the prefab
	// and makes every instance of it in the scene again (ReapplyPrefab()), keeping what each changes;
	// then gives the scene's entities the step changed the values the step left them with, so an
	// instance that applied a value to its prefab keeps it when that is undone. A prefab file changed
	// by something else makes its instances again the same way (FileChanged()).
	class PrefabSubscriber final : public ITransactionSubscriber
	{
	public:
		explicit PrefabSubscriber(SceneEditor& editing);

		// Where a prefab's file is, by its path in the asset folders; Paths::FindAsset() until set
		std::function<std::filesystem::path(const std::string& prefabPath)> FindFile;

		// Keeps the prefab as it is now, unless kept already, so its instances can be compared with it
		// once it changed. Called before it changes.
		void Remember(const std::string& prefabPath);

		// The prefab's file changed: read again, its instances made again
		void FileChanged(const std::string& prefabPath);

		void OnTransactionCommitted(const Transaction& transaction) override;
		void OnTransactionApplied(const Transaction& transaction, bool bWasUndo) override;

	private:
		void AfterChange(const Transaction& transaction, bool bWasUndo);

		// Makes the instances of the prefab again against `before`; the selection kept by guid
		void ReapplyInstances(const std::string& prefabPath, const Nyx::PrefabAsset& before);

		SceneEditor& Editing;

		// Each prefab as its file was when it was last saved or read, by path
		std::unordered_map<std::string, std::vector<std::byte>> Saved;
	};
}
