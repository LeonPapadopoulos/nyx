#include "PrefabMode.h"

#include "BinaryArchive.h"
#include "GuidComponent.h"
#include "JobSystem.h"
#include "Log.h"
#include "ParentComponent.h"
#include "Prefab.h"
#include "ReflectedArchiveSerializer.h"
#include "SceneEditor.h"
#include "SceneSerializer.h"

#include <cstddef>

namespace Nyx::Editor
{
	PrefabMode::PrefabMode(SceneEditor& scene)
		: Scene(scene)
	{
	}

	PrefabMode::~PrefabMode() = default;

	bool PrefabMode::Open(const std::string& prefabPath, std::string& outError)
	{
		Nyx::PrefabAsset prefab;
		if (!prefab.LoadFromFile(Scene.Prefabs.FindFile(prefabPath), outError))
		{
			return false;
		}

		auto editing = std::make_unique<SceneEditor>();
		Nyx::Registry& world = editing->Scene.GetRegistry();
		Nyx::ScenePostLoadContext context{ .AssetResolver = Scene.Assets };
		Nyx::ReadWarnings warnings;
		for (size_t index = 0; index < prefab.GetEntities().size(); ++index)
		{
			Nyx::BinaryReader reader;
			reader.LoadFromMemory(prefab.GetEntityBytes(index));
			const Nyx::Entity entity = world.CreateEntity();
			if (!Nyx::SceneSerializer::ReadEntity(reader, world, entity, context, warnings))
			{
				outError = "has an entity that can't be read";
				return false;
			}
		}
		warnings.Log("Prefab");

		// Its own undo, with the assets of what it changes loaded and children placed, as the scene's
		editing->Assets = Scene.Assets;
		editing->AssetTypes = Scene.AssetTypes;
		editing->AssetFileWritten = Scene.AssetFileWritten;
		editing->PrefabRoot = world.Get<Nyx::GuidComponent>(prefab.GetEntities().front()).Guid;
		editing->AssetLoader.SetPostLoadContext(Nyx::ScenePostLoadContext{ .AssetResolver = Scene.Assets, .Background = &Nyx::JobSystem::Get() });
		editing->Transactions.Subscribe(&editing->AssetLoader);
		editing->Transactions.Subscribe(&editing->Hierarchy);
		editing->Transactions.Subscribe(&Revisions);
		editing->Hierarchy.Update();

		Editing = std::move(editing);
		Path = prefabPath;
		SavedRevision = Revisions.Revision;
		return true;
	}

	void PrefabMode::Close()
	{
		Editing.reset();
		Path.clear();
	}

	bool PrefabMode::Save(std::string& outError)
	{
		if (!Editing)
		{
			outError = "no prefab is open";
			return false;
		}

		Nyx::Registry& world = Editing->Scene.GetRegistry();
		const std::optional<Nyx::Entity> root = Editing->Scene.FindEntity(Editing->PrefabRoot);
		if (!root)
		{
			outError = "its root was deleted (undo brings it back)";
			return false;
		}
		size_t count = 0;
		world.Each<Nyx::GuidComponent>([&](Nyx::Entity, const Nyx::GuidComponent&) { ++count; });
		const size_t below = Nyx::GetTransformSubtree(world, *root).size();
		if (below != count)
		{
			outError = std::to_string(count - below) + (count - below == 1 ? " entity isn't" : " entities aren't") +
				" below its root: drag them onto it in the outliner, or delete them";
			return false;
		}
		if (!Nyx::PrefabAsset::SaveToFile(world, *root, Scene.Prefabs.FindFile(Path)))
		{
			outError = "its file can't be written (the log says why)";
			return false;
		}

		SavedRevision = Revisions.Revision;
		// The scene's instances follow; edits in progress there point at their components
		Scene.CancelPendingEdits();
		Scene.Prefabs.FileChanged(Path);
		LOG_INFO("Saved prefab '{0}'", Path);
		return true;
	}
}
