#pragma once

#include "ReflectedAssetTypes.h"
#include "TransactionObjectRef.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Nyx::Editor
{
	// One asset of a reflected asset type (Nyx::ReflectedAssetTypes) open for editing: its file's lines
	// and its values, an object of the type's struct. Each change of the values writes them into the
	// lines, keeping the file's comments and the lines it doesn't know, checks them and counts a
	// revision. ReflectedAssetEditors owns the open ones; each is one object of the editor's undo.
	class ReflectedAssetDocument
	{
	public:
		// Reads the file at `file`; null, with why in `outError`, when it can't be read. `path`: where
		// it is in the asset folders, as references name it ("PhysicsMaterials/Wood.nyxphysmat").
		// `id`: its own among every asset opened, for its ObjectRef.
		static std::unique_ptr<ReflectedAssetDocument> Load(
			const Nyx::ReflectedAssetType& type, std::string path, std::filesystem::path file, uint64_t id, std::string& outError);

		ObjectRef GetRef() const
		{
			return ObjectRef{ EObjectDomain::ReflectedAsset, InspectorTargetId{ Id } };
		}

		const Nyx::ReflectedAssetType& GetType() const { return *Type; }
		const std::string& GetPath() const { return Path; }
		const std::filesystem::path& GetFile() const { return File; }

		// An object of the type's struct
		void* GetData() { return Data.get(); }
		const void* GetData() const { return Data.get(); }

		// The file's text with the values as they are now
		std::string GetText() const { return Text.ToText(); }

		// What is wrong: the lines whose values can't be read (written by hand, until their values
		// change), then what is wrong with the values (Nyx::ValidateReflectedAsset())
		const std::vector<Nyx::PropertyProblem>& GetProblems() const { return Problems; }

		// The values changed (an edit, undo, redo, a value being dragged): written into the lines,
		// checked and counted
		void OnValuesChanged();

		// Writes the text to the file
		bool Save(std::string& outError);

		// Changed since it was opened or saved
		bool IsDirty() const { return Revision != SavedRevision; }
		// Counts changes, so whoever sends or draws it can tell when to look again
		uint64_t GetRevision() const { return Revision; }

		// Its revision the running games last got
		uint64_t GetSentRevision() const { return SentRevision; }
		void SetSentRevision(uint64_t revision) { SentRevision = revision; }

		// Its file changed on disk while it had changes not saved, so it wasn't read again
		bool IsChangedOnDisk() const { return bChangedOnDisk; }
		void SetChangedOnDisk(bool bChanged) { bChangedOnDisk = bChanged; }

	private:
		ReflectedAssetDocument(const Nyx::ReflectedAssetType& type, std::string path, std::filesystem::path file, uint64_t id);

		void Check();

		const Nyx::ReflectedAssetType* Type = nullptr;
		std::string Path;
		std::filesystem::path File;
		uint64_t Id = 0;

		Nyx::ReflectedAssetText Text;
		Nyx::ReflectedObjectPtr Data;
		std::vector<Nyx::PropertyProblem> Problems;

		uint64_t Revision = 0;
		uint64_t SavedRevision = 0;
		uint64_t SentRevision = 0;
		bool bChangedOnDisk = false;
	};
}
