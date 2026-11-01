#pragma once

#include "EntityGuid.h"
#include "TransactionDomain.h"

#include <cstdint>
#include <memory>
#include <string>

namespace Nyx::Editor
{
	class SceneEditor;

	// Prefab mode: a prefab open on its own, its entities in a SceneEditor of their own, which the
	// panels and scene views show and edit in place of the scene's, with an undo of its own. Its
	// entities are the prefab's, with the guids the prefab gives them. A new entity becomes a child
	// of the root, as a prefab has one. Save() writes the prefab file, and the scene's instances
	// follow it, keeping what they override (PrefabSubscriber::FileChanged()). The scene, its undo
	// and its selection stay as they were meanwhile.
	class PrefabMode
	{
	public:
		// `scene`: the scene's editing, whose asset resolver and prefab files this uses; it must
		// outlive this
		explicit PrefabMode(SceneEditor& scene);
		~PrefabMode();

		PrefabMode(const PrefabMode&) = delete;
		PrefabMode& operator=(const PrefabMode&) = delete;

		// Opens the prefab at the path in the asset folders ("Prefabs/Lamp.nyxprefab"), in place of
		// one open. False, with why in `outError`, if its file can't be read; what was open stays.
		bool Open(const std::string& prefabPath, std::string& outError);

		// Drops the prefab open, and what of it isn't saved
		void Close();

		bool IsOpen() const
		{
			return Editing != nullptr;
		}

		// The prefab's editing; only while one is open
		SceneEditor& GetEditing()
		{
			return *Editing;
		}

		const std::string& GetPath() const
		{
			return Path;
		}

		// Changed since it was opened or saved
		bool IsDirty() const
		{
			return IsOpen() && Revisions.Revision != SavedRevision;
		}

		// Writes the prefab's file from its root and what is below it, and the scene's instances
		// follow. False, with why in `outError`, and the file as it was, if the root is gone, if an
		// entity isn't below the root, or if the file can't be written.
		bool Save(std::string& outError);

	private:
		// Counts the prefab's edits, undos and redos
		struct RevisionCounter final : ITransactionSubscriber
		{
			void OnTransactionCommitted(const Transaction& /*transaction*/) override
			{
				++Revision;
			}
			void OnTransactionApplied(const Transaction& /*transaction*/, bool /*bWasUndo*/) override
			{
				++Revision;
			}

			uint64_t Revision = 0;
		};

		SceneEditor& Scene;
		std::unique_ptr<SceneEditor> Editing;
		std::string Path;
		RevisionCounter Revisions;
		uint64_t SavedRevision = 0;
	};
}
