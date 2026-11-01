#include "PrefabEdits.h"

#include "AssetReference.h"
#include "GuidComponent.h"
#include "IAssetResolver.h"
#include "Log.h"
#include "NameComponent.h"
#include "Paths.h"
#include "Prefab.h"
#include "RootObjectSnapshotUtils.h"
#include "SceneEditor.h"
#include "SceneOutlineEdits.h"
#include "TerrainBrush.h"
#include "TransactionObjectRefHelpers.h"
#include "TransformComponent.h"
#include "TransformParents.h"

#include <cmath>
#include <utility>
#include <vector>

namespace Nyx::Editor
{
	namespace
	{
		Change MakeAddEntityChange(SceneEditor& editing, Nyx::Entity entity)
		{
			const ObjectRef target = MakeSceneEntityRef(editing.Scene, entity);
			return Change{ EChangeKind::AddObject,
				AddObjectChange{ .Target = target, .AfterCreate = CaptureRootObjectSnapshot(editing.EntityDomain, editing.TransactionContext, target) } };
		}

		Nyx::AssetReference MakePrefabReference(const std::string& prefabPath)
		{
			return Nyx::AssetReference{ .Type = std::string(Nyx::PrefabAssetType), .Path = prefabPath };
		}
	}

	glm::vec3 FindPrefabDropPoint(const Nyx::Registry& world, const Nyx::Ray& ray)
	{
		if (const std::optional<Nyx::TerrainBrushHit> hit = Nyx::CastTerrainBrush(world, ray, 2000.0f))
		{
			return hit->Point;
		}
		if (ray.Direction.y < -1e-4f && ray.Origin.y > 0.0f)
		{
			return ray.Origin + ray.Direction * (-ray.Origin.y / ray.Direction.y);
		}
		return ray.Origin + ray.Direction * 10.0f;
	}

	std::optional<Nyx::Entity> PlacePrefab(SceneEditor& editing, const std::string& prefabPath, const glm::vec3& position)
	{
		const Nyx::PrefabAsset* prefab = editing.Assets ? editing.Assets->ResolvePrefab(prefabPath) : nullptr;
		if (!prefab)
		{
			LOG_WARNING("Place Prefab: '{0}' can't be read", prefabPath);
			return std::nullopt;
		}

		// New entities can move components in memory; edits in progress point at components
		editing.CancelPendingEdits();
		Nyx::Registry& world = editing.Scene.GetRegistry();
		const Nyx::Registry& prefabWorld = prefab->GetTemplate();
		const Nyx::Entity prefabRoot = prefab->GetRoot();
		const std::string name = prefabWorld.Has<Nyx::NameComponent>(prefabRoot) ? prefabWorld.Get<Nyx::NameComponent>(prefabRoot).Name : "Prefab";

		const std::optional<Nyx::Entity> selected = editing.Scene.GetSelection();
		const Nyx::EntityGuid after = selected ? editing.Scene.GetGuid(*selected) : Nyx::EntityGuid{};
		const Nyx::Entity root = editing.Scene.CreateEntity(name);
		Nyx::TransformComponent transform =
			prefabWorld.Has<Nyx::TransformComponent>(prefabRoot) ? prefabWorld.Get<Nyx::TransformComponent>(prefabRoot) : Nyx::TransformComponent{};
		transform.Position = position;
		world.Add<Nyx::TransformComponent>(root, transform);
		world.Add<Nyx::PrefabInstanceComponent>(root, Nyx::PrefabInstanceComponent{ .Prefab = MakePrefabReference(prefabPath) });

		Nyx::ScenePostLoadContext context{ .AssetResolver = editing.Assets };
		std::vector<Nyx::Entity> parts;
		Nyx::ApplyPrefab(world, root, *prefab, context, &parts);

		Transaction transaction;
		transaction.Label = "Place Prefab";
		transaction.Changes.push_back(MakeAddEntityChange(editing, root));
		for (const Nyx::Entity part : parts)
		{
			transaction.Changes.push_back(MakeAddEntityChange(editing, part));
		}
		for (Change& placed : PlaceNewEntity(editing.Scene, editing.Scene.GetGuid(root), after, 0))
		{
			transaction.Changes.push_back(std::move(placed));
		}
		editing.Transactions.Push(std::move(transaction));
		editing.Scene.GetSelection() = root;
		return root;
	}

	bool CreatePrefab(SceneEditor& editing, Nyx::Entity entity, const std::filesystem::path& file, const std::string& prefabPath)
	{
		Nyx::Registry& world = editing.Scene.GetRegistry();
		const std::vector<Nyx::Entity> entities = GetSelfAndBelow(world, entity);
		if (entities.empty() || !world.Has<Nyx::GuidComponent>(entity))
		{
			return false;
		}
		for (const Nyx::Entity below : entities)
		{
			if (world.Has<Nyx::PrefabInstanceComponent>(below) || world.Has<Nyx::PrefabPartComponent>(below))
			{
				LOG_WARNING("Create Prefab: '{0}' holds an instance of a prefab, which a prefab can't hold yet", file.string());
				return false;
			}
		}

		// Saved, and read back as instances will read it
		if (!Nyx::PrefabAsset::SaveToFile(world, entity, file))
		{
			LOG_ERROR("Create Prefab: couldn't save '{0}'", file.string());
			return false;
		}
		if (editing.Assets)
		{
			editing.Assets->ForgetPrefab(prefabPath);
		}
		if (editing.AssetFileWritten)
		{
			editing.AssetFileWritten();
		}
		const Nyx::PrefabAsset* prefab = editing.Assets ? editing.Assets->ResolvePrefab(prefabPath) : nullptr;
		if (!prefab)
		{
			LOG_ERROR("Create Prefab: saved '{0}', but can't read it back as '{1}'", file.string(), prefabPath);
			return false;
		}

		editing.CancelPendingEdits();
		Transaction transaction;
		transaction.Label = "Create Prefab";

		// The entities below go, children before their parents, and come back as the instance's parts
		for (auto below = entities.rbegin(); below != entities.rend() - 1; ++below)
		{
			const ObjectRef target = MakeSceneEntityRef(editing.Scene, *below);
			RootObjectSnapshot snapshot = CaptureRootObjectSnapshot(editing.EntityDomain, editing.TransactionContext, target);
			if (editing.Scene.DestroyEntity(*below))
			{
				transaction.Changes.push_back(Change{ EChangeKind::DeleteObject, DeleteObjectChange{ .Target = target, .BeforeDelete = std::move(snapshot) } });
			}
		}

		const Nyx::Reflection::TypeMetadata& instanceType = Nyx::Reflection::GetTypeMetadata<Nyx::PrefabInstanceComponent>();
		Nyx::PrefabInstanceComponent& instance =
			world.Add<Nyx::PrefabInstanceComponent>(entity, Nyx::PrefabInstanceComponent{ .Prefab = MakePrefabReference(prefabPath) });
		transaction.Changes.push_back(Change{ EChangeKind::AddSubobject,
			AddSubobjectChange{ .Target = MakeSceneEntityRef(editing.Scene, entity), .AfterAdd = CaptureSubobjectSnapshot(&instance, instanceType) } });

		Nyx::ScenePostLoadContext context{ .AssetResolver = editing.Assets };
		std::vector<Nyx::Entity> parts;
		Nyx::ApplyPrefab(world, entity, *prefab, context, &parts);
		for (const Nyx::Entity part : parts)
		{
			transaction.Changes.push_back(MakeAddEntityChange(editing, part));
		}
		editing.Transactions.Push(std::move(transaction));
		return true;
	}

	std::optional<Nyx::Entity> FindPrefabInstanceRoot(const Nyx::Registry& world, Nyx::Entity entity)
	{
		if (!world.IsAlive(entity))
		{
			return std::nullopt;
		}
		if (world.Has<Nyx::PrefabInstanceComponent>(entity))
		{
			return entity;
		}
		if (world.Has<Nyx::PrefabPartComponent>(entity))
		{
			const std::optional<Nyx::Entity> root = Nyx::FindEntityByGuid(world, world.Get<Nyx::PrefabPartComponent>(entity).Instance);
			return root && world.Has<Nyx::PrefabInstanceComponent>(*root) ? root : std::nullopt;
		}
		return std::nullopt;
	}

	bool UnpackPrefab(SceneEditor& editing, Nyx::Entity entity)
	{
		Nyx::Registry& world = editing.Scene.GetRegistry();
		const std::optional<Nyx::Entity> root = FindPrefabInstanceRoot(world, entity);
		if (!root)
		{
			return false;
		}
		editing.CancelPendingEdits();

		const Nyx::EntityGuid rootGuid = editing.Scene.GetGuid(*root);
		Transaction transaction;
		transaction.Label = "Unpack Prefab";
		const auto removeMark = [&](Nyx::Entity marked, const auto* component, const Nyx::Reflection::TypeMetadata& type, auto remove)
		{
			SubobjectSnapshot beforeRemove = CaptureSubobjectSnapshot(component, type);
			remove();
			transaction.Changes.push_back(Change{ EChangeKind::RemoveSubobject,
				RemoveSubobjectChange{ .Target = MakeSceneEntityRef(editing.Scene, marked), .BeforeRemove = std::move(beforeRemove) } });
		};
		for (const Nyx::Entity below : GetSelfAndBelow(world, *root))
		{
			if (world.Has<Nyx::PrefabPartComponent>(below) && world.Get<Nyx::PrefabPartComponent>(below).Instance == rootGuid)
			{
				removeMark(below, &world.Get<Nyx::PrefabPartComponent>(below), Nyx::Reflection::GetTypeMetadata<Nyx::PrefabPartComponent>(),
					[&]() { world.Remove<Nyx::PrefabPartComponent>(below); });
			}
		}
		removeMark(*root, &world.Get<Nyx::PrefabInstanceComponent>(*root), Nyx::Reflection::GetTypeMetadata<Nyx::PrefabInstanceComponent>(),
			[&]() { world.Remove<Nyx::PrefabInstanceComponent>(*root); });
		editing.Transactions.Push(std::move(transaction));
		return true;
	}

	std::optional<std::string> GetAssetPathOf(const std::filesystem::path& file)
	{
		std::error_code error;
		const std::filesystem::path absolute = std::filesystem::weakly_canonical(file, error);
		for (const std::filesystem::path& folder : Nyx::Paths::GetAssetSearchDirs())
		{
			const std::filesystem::path relative = std::filesystem::weakly_canonical(absolute, error).lexically_relative(std::filesystem::weakly_canonical(folder, error));
			if (!relative.empty() && *relative.begin() != "..")
			{
				return relative.generic_string();
			}
		}
		return std::nullopt;
	}
}
