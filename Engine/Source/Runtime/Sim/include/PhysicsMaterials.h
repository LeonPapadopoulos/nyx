#pragma once

#include "AssetReference.h"
#include "Entity.h"
#include "FileWatcher.h"
#include "PhysicsMaterialAsset.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Nyx
{
	// The physics materials (.nyxphysmat) a game world's colliders name, each read once and kept, by
	// its path in the assets folders ("PhysicsMaterials/Wood.nyxphysmat"). A material whose values
	// change, live from the editor (SetText()) or its file read again (Reload(), ReloadChanged()),
	// changes every body made of it from the next step on (PhysicsBodies). The game world owns one.
	class PhysicsMaterials
	{
	public:
		// The folders a path is looked for in, in order; Paths::GetAssetSearchDirs() until set
		void SetSearchDirs(std::vector<std::filesystem::path> dirs);

		// The material at `path`, read on first use; the engine's (PhysicsMaterialData's defaults) for
		// an empty path, or a file that isn't there or can't be read, which is logged once
		const PhysicsMaterialData& Get(const std::string& path);
		// The engine's material
		static const PhysicsMaterialData& GetDefault();

		// A material's text as the editor has it, saved or not: kept over the file's until the file is
		// read again. False, logged, for text that can't be read, which changes nothing.
		bool SetText(const std::string& path, std::string_view text);
		// Reads the file again, e.g. once saved; false, logged, when it can't be read, which keeps
		// what the material had
		bool Reload(const std::string& path);

		// Watches the asset folders for files that change; ReloadChanged(), once a frame, reads the
		// materials kept whose files changed again
		void Watch();
		void ReloadChanged();

		// Goes up with every change of any material kept, so a user can tell when to look again
		uint64_t GetRevision() const { return Revision; }

	private:
		std::filesystem::path FindFile(const std::string& path) const;

		std::vector<std::filesystem::path> SearchDirs;
		bool bSearchDirsSet = false;
		// By path; references stay valid as more are added
		std::unordered_map<std::string, PhysicsMaterialData> Loaded;
		uint64_t Revision = 0;
		std::unique_ptr<FileWatcher> Watcher;
	};

	// The physics material asset of the collider that makes the entity's body (a box over a sphere
	// over a capsule over a mesh); null for an entity without a collider
	const AssetReference* FindColliderMaterial(const Registry& world, Entity entity);

	// The physics material of the entity's collider: its asset's, or the engine's for one without, or
	// an entity without a collider. A game reads its own numbers from it (GetExtra()), e.g. how
	// readily it burns.
	const PhysicsMaterialData& GetPhysicsMaterial(const Registry& world, PhysicsMaterials& materials, Entity entity);
}
