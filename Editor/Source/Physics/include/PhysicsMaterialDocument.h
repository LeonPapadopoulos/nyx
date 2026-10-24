#pragma once

#include "PhysicsMaterialAsset.h"
#include "TransactionDomain.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Nyx::Editor
{
	class TransactionSystem;

	// The physics material open for editing (.nyxphysmat), as one object of the editor's undo
	// (EObjectDomain::PhysicsMaterial): the Physics Material window edits its values through the
	// details table and records the edits like any others, so Ctrl+Z undoes them in turn with the
	// scene's. Every edit, undo, redo and value being dragged writes the values into the file's lines,
	// keeping its comments and the lines it doesn't know, and counts a revision, so the running games
	// get the text (PhysicsMaterialValuesMessage) as it changes. Each material opened is another
	// object: undo steps of one closed since do nothing.
	class PhysicsMaterialDocument final : public ITransactionDomain, public ITransactionSubscriber
	{
	public:
		ObjectRef GetRef() const
		{
			return ObjectRef{ EObjectDomain::PhysicsMaterial, InspectorTargetId{ OpenCount } };
		}

		// Edits, undo and redo reach it from then on
		void Attach(TransactionSystem& transactions);

		// `path`: relative to the asset folders, as colliders name it ("PhysicsMaterials/Wood.nyxphysmat"),
		// looked for in `assetsFolders` in order. False when the file can't be read, with why in
		// `outError`; what was open stays open then.
		bool Open(const std::vector<std::filesystem::path>& assetsFolders, std::string path, std::string& outError);
		void Close();
		bool IsOpen() const { return !Path.empty(); }

		bool Save(std::string& outError);

		const std::string& GetPath() const { return Path; }
		const std::filesystem::path& GetFile() const { return File; }
		Nyx::PhysicsMaterialData& GetData() { return Data; }
		const Nyx::PhysicsMaterialData& GetData() const { return Data; }
		// The file's text with the values as they are now
		std::string GetText() const { return Asset.ToText(); }

		// Changed since it was opened or saved
		bool IsDirty() const { return Revision != SavedRevision; }
		// Counts changes, so whoever sends or draws it can tell when to look again
		uint64_t GetRevision() const { return Revision; }

		// ITransactionDomain: one root object, the material's values, with itself as its only subobject
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
		bool IsMine(const ObjectRef& root) const { return IsOpen() && root == GetRef(); }

		std::string Path;
		std::filesystem::path File;
		Nyx::PhysicsMaterialAsset Asset;
		Nyx::PhysicsMaterialData Data;
		// Each material opened is another object of the undo; 0 is none
		uint64_t OpenCount = 0;

		uint64_t Revision = 0;
		uint64_t SavedRevision = 0;
	};
}
