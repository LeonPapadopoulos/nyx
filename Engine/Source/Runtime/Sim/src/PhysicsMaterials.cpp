#include "NyxPCH.h"
#include "PhysicsMaterials.h"

#include "Log.h"
#include "Paths.h"
#include "PhysicsComponents.h"

#include <algorithm>
#include <cctype>

namespace Nyx
{
	namespace
	{
		// Windows' file names don't tell case apart
		std::string ComparablePath(const std::filesystem::path& path)
		{
			std::string text = path.lexically_normal().generic_string();
			std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c)
				{
					return static_cast<char>(std::tolower(c));
				});
			while (!text.empty() && text.back() == '/')
			{
				text.pop_back();
			}
			return text;
		}

		void LogProblems(const std::string& path, const std::vector<std::string>& problems)
		{
			for (const std::string& problem : problems)
			{
				LOG_WARNING("Physics material '{0}': {1}", path, problem);
			}
		}
	}

	void PhysicsMaterials::SetSearchDirs(std::vector<std::filesystem::path> dirs)
	{
		SearchDirs = std::move(dirs);
		bSearchDirsSet = true;
	}

	const PhysicsMaterialData& PhysicsMaterials::GetDefault()
	{
		static const PhysicsMaterialData Default;
		return Default;
	}

	const PhysicsMaterialData& PhysicsMaterials::Get(const std::string& path)
	{
		if (path.empty())
		{
			return GetDefault();
		}
		if (const auto found = Loaded.find(path); found != Loaded.end())
		{
			return found->second;
		}

		// Kept as the default when it can't be read, so it is tried once and logged once
		PhysicsMaterialData& material = Loaded[path];
		PhysicsMaterialAsset asset;
		std::string error;
		if (!asset.Load(FindFile(path), error))
		{
			LOG_WARNING("Physics material '{0}' can't be read, so its colliders have the engine's: {1}", path, error);
			return material;
		}
		std::vector<std::string> problems;
		material = asset.Read(&problems);
		LogProblems(path, problems);
		return material;
	}

	bool PhysicsMaterials::SetText(const std::string& path, std::string_view text)
	{
		PhysicsMaterialAsset asset;
		std::string error;
		if (path.empty() || !asset.Parse(text, error))
		{
			LOG_WARNING("Physics material '{0}' from the editor can't be read: {1}", path, error);
			return false;
		}
		std::vector<std::string> problems;
		Loaded[path] = asset.Read(&problems);
		LogProblems(path, problems);
		++Revision;
		return true;
	}

	bool PhysicsMaterials::Reload(const std::string& path)
	{
		PhysicsMaterialAsset asset;
		std::string error;
		if (path.empty() || !asset.Load(FindFile(path), error))
		{
			LOG_WARNING("Physics material '{0}' can't be read again: {1}", path, error);
			return false;
		}
		std::vector<std::string> problems;
		Loaded[path] = asset.Read(&problems);
		LogProblems(path, problems);
		++Revision;
		return true;
	}

	void PhysicsMaterials::Watch()
	{
		Watcher = std::make_unique<FileWatcher>();
		for (const std::filesystem::path& dir : bSearchDirsSet ? SearchDirs : Paths::GetAssetSearchDirs())
		{
			if (!Watcher->Watch(dir))
			{
				LOG_WARNING("Physics materials: can't watch '{0}': {1}", dir.string(), Watcher->GetProblem());
			}
		}
	}

	void PhysicsMaterials::ReloadChanged()
	{
		if (!Watcher)
		{
			return;
		}
		const std::vector<std::filesystem::path> changes = Watcher->TakeChanges();
		if (changes.empty())
		{
			return;
		}

		// Only materials in use, whose file is among the changes (or whose folder overflowed)
		std::vector<std::string> due;
		for (const auto& [path, material] : Loaded)
		{
			const std::string file = ComparablePath(FindFile(path));
			for (const std::filesystem::path& change : changes)
			{
				// The file itself, or a folder it is in: the watcher names its folder when it lost count
				const std::string changed = ComparablePath(change);
				if (file.starts_with(changed) && (file.size() == changed.size() || file[changed.size()] == '/'))
				{
					due.push_back(path);
					break;
				}
			}
		}
		for (const std::string& path : due)
		{
			if (Reload(path))
			{
				LOG_INFO("Physics material '{0}' read again", path);
			}
		}
	}

	std::filesystem::path PhysicsMaterials::FindFile(const std::string& path) const
	{
		if (!bSearchDirsSet)
		{
			return Paths::FindAsset(path);
		}
		return Paths::FindAssetDirIn(SearchDirs, path) / path;
	}

	const AssetReference* FindColliderMaterial(const Registry& world, Entity entity)
	{
		if (world.Has<BoxColliderComponent>(entity))
		{
			return &world.Get<BoxColliderComponent>(entity).Material;
		}
		if (world.Has<SphereColliderComponent>(entity))
		{
			return &world.Get<SphereColliderComponent>(entity).Material;
		}
		if (world.Has<CapsuleColliderComponent>(entity))
		{
			return &world.Get<CapsuleColliderComponent>(entity).Material;
		}
		if (world.Has<MeshColliderComponent>(entity))
		{
			return &world.Get<MeshColliderComponent>(entity).Material;
		}
		return nullptr;
	}

	const PhysicsMaterialData& GetPhysicsMaterial(const Registry& world, PhysicsMaterials& materials, Entity entity)
	{
		const AssetReference* material = FindColliderMaterial(world, entity);
		return material ? materials.Get(material->Path) : PhysicsMaterials::GetDefault();
	}
}
