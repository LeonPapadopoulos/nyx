#pragma once

#include "EditorLinkMessages.h"
#include "ReflectedAssetDocument.h"
#include "TransactionDomain.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Nyx
{
	class FileWatcher;
}

namespace Nyx::Editor
{
	class TransactionSystem;

	// The assets of reflected asset types open for editing, any number at once, each in its window
	// (ReflectedAssetWindows), and each one object of the editor's undo (EObjectDomain::ReflectedAsset):
	// the windows edit their values through the details table and record the edits like any others, so
	// Ctrl+Z undoes them in turn with the scene's. Every edit, undo, redo and value being dragged reaches
	// the document's lines at once and the running games once a frame (TakeValuesToSend()). An asset
	// opened again, or read again from its file, is another object: undo steps from before do nothing.
	//
	// Files changed on disk (Update(), ApplyFileChanges()) are read again into an asset open without
	// changes; one with changes keeps them and says its file changed.
	class ReflectedAssetEditors final : public ITransactionDomain, public ITransactionSubscriber
	{
	public:
		ReflectedAssetEditors();
		~ReflectedAssetEditors() override;

		// Edits, undo and redo reach the assets from then on
		void Attach(TransactionSystem& transactions);

		// Where paths are looked for, in order (Paths::GetAssetSearchDirs()); new assets go into the first
		void SetAssetFolders(std::vector<std::filesystem::path> folders);
		// Watches the asset folders for files that change, which Update() reads again
		void Watch();
		// Once a frame
		void Update();

		// Opens the asset at `path` ("PhysicsMaterials/Wood.nyxphysmat"), of the type its extension
		// names, or gives the one open. Null, with why in `outError`, for a type that isn't registered
		// or a file that can't be read.
		ReflectedAssetDocument* Open(const std::string& path, std::string& outError);
		// Closes it, dropping changes not saved
		void Close(const std::string& path);
		ReflectedAssetDocument* Find(const std::string& path);
		const std::vector<std::unique_ptr<ReflectedAssetDocument>>& GetDocuments() const { return Documents; }

		// New <DisplayName>: a file of the type with its values' defaults, every one written, in
		// `folder` (relative to the first asset folder), named New<TypeId>, New<TypeId>2, ... Its path,
		// or empty with why in `outError`.
		std::string Create(const Nyx::ReflectedAssetType& type, const std::filesystem::path& folder, std::string& outError);

		// Files that changed on disk, as the watcher names them: their open assets read again, those
		// without changes; a folder stands for everything in it
		void ApplyFileChanges(const std::vector<std::filesystem::path>& changes);
		// Reads the asset's file again, dropping its changes; false, with why, when it can't be read
		bool Reload(const std::string& path, std::string& outError);

		// The assets whose values changed since the games last got them, as messages; counted as sent
		std::vector<Nyx::AssetValuesMessage> TakeValuesToSend();
		// The assets with changes not saved, for a game that starts
		std::vector<Nyx::AssetValuesMessage> GetUnsavedValues() const;

		// ITransactionDomain: one root object per asset, its values, with itself as its only subobject
		void* ResolveMutable(EditorTransactionContext& context, const ObjectRef& root, const Nyx::Reflection::TypeMetadata& typeMetadata) override;
		bool CreateRootObject(EditorTransactionContext& context, const ObjectRef& root) override;
		bool DeleteRootObject(EditorTransactionContext& context, const ObjectRef& root) override;
		void EnumerateSubobjects(EditorTransactionContext& context, const ObjectRef& root, std::vector<ReflectedObjectView>& outSubobjects) override;
		bool EnsureSubobject(EditorTransactionContext& context, const ObjectRef& root, const Nyx::Reflection::TypeMetadata& typeMetadata) override;
		bool RemoveSubobject(EditorTransactionContext& context, const ObjectRef& root, const Nyx::Reflection::TypeMetadata& typeMetadata) override;

		// ITransactionSubscriber
		void OnTransactionCommitted(const Transaction& transaction) override;
		void OnTransactionApplied(const Transaction& transaction, bool bWasUndo) override;
		void OnTransactionPreviewed(const Transaction& transaction) override;

	private:
		void OnChanged(const Transaction& transaction);
		ReflectedAssetDocument* FindByRef(const ObjectRef& root);
		std::filesystem::path FindFile(const std::string& path) const;

		std::vector<std::filesystem::path> AssetFolders;
		std::vector<std::unique_ptr<ReflectedAssetDocument>> Documents;
		// Each asset opened is another object of the undo; 0 is none
		uint64_t OpenCount = 0;
		std::unique_ptr<Nyx::FileWatcher> Watcher;
	};
}
