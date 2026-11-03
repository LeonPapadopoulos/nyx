#include "PrefabEdits.h"

#include "AssetReference.h"
#include "BinaryArchive.h"
#include "ReflectedArchiveSerializer.h"
#include "ComponentTypeRegistry.h"
#include "ParentComponent.h"
#include "ReflectedPropertyAccess.h"
#include "ReflectionUtils.h"
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

		// Copies the property of `source` into `destination` (an object of `ownerType` at `location` in
		// the component), recording each value that changed, or with bKeep each value whether or not,
		// as undo steps hold them: a struct field by field, an array whole
		void AppendCopy(std::vector<Change>& changes, const ObjectRef& target, void* destination, const void* source,
			const Nyx::Reflection::TypeMetadata& ownerType, size_t index, const SubobjectPath& location, bool bKeep)
		{
			const Nyx::Reflection::PropertyMetadata& property = ownerType.Properties[index];
			if (!Nyx::Reflection::HasFlag(property.Flags, Nyx::Reflection::EPropertyFlags::Serialize))
			{
				return;
			}
			if (Nyx::Reflection::IsStructProperty(property))
			{
				const Nyx::Reflection::TypeMetadata* nested = Nyx::Reflection::TryGetNestedType(property);
				if (!nested)
				{
					return;
				}
				SubobjectPath inside = location;
				if (!inside.SubobjectType)
				{
					inside.SubobjectType = &ownerType;
				}
				inside.PropertyIndices.push_back(index);
				void* destinationStruct = Nyx::Reflection::GetPropertyAddress(destination, property);
				const void* sourceStruct = Nyx::Reflection::GetPropertyAddress(const_cast<void*>(source), property);
				for (size_t field = 0; field < nested->PropertyCount; ++field)
				{
					AppendCopy(changes, target, destinationStruct, sourceStruct, *nested, field, inside, bKeep);
				}
				return;
			}
			if (Nyx::Reflection::TryGetArray(property))
			{
				std::vector<std::byte> before = SaveReflectedProperty(destination, ownerType, index);
				std::vector<std::byte> after = SaveReflectedProperty(source, ownerType, index);
				if (bKeep || before != after)
				{
					LoadReflectedProperty(destination, ownerType, after);
					changes.push_back(Change{ EChangeKind::ReplaceArray,
						ReplaceArrayChange{ .Target = target, .TypeMetadata = &ownerType, .PropertyIndex = index, .Location = location, .Before = std::move(before), .After = std::move(after) } });
				}
				return;
			}
			Nyx::Reflection::PropertyValue before = ReadReflectedPropertyValue(destination, property);
			Nyx::Reflection::PropertyValue after = ReadReflectedPropertyValue(source, property);
			if (bKeep || !Nyx::Reflection::ArePropertyValuesEqual(before, after))
			{
				WriteReflectedPropertyValue(destination, property, after);
				changes.push_back(Change{ EChangeKind::SetValue,
					SetValueChange{ .Target = target, .TypeMetadata = &ownerType, .PropertyIndex = index, .Before = std::move(before), .After = std::move(after), .Location = location } });
			}
		}
	}

	std::optional<PrefabSource> FindPrefabSource(SceneEditor& editing, Nyx::Entity entity)
	{
		const Nyx::Registry& world = editing.Scene.GetRegistry();
		const std::optional<Nyx::Entity> root = FindPrefabInstanceRoot(world, entity);
		if (!root || !editing.Assets)
		{
			return std::nullopt;
		}
		const std::string& path = world.Get<Nyx::PrefabInstanceComponent>(*root).Prefab.Path;
		Nyx::PrefabAsset* prefab = editing.Assets->ResolvePrefab(path);
		const std::optional<Nyx::Entity> prefabEntity = prefab ? Nyx::FindPrefabEntityOf(world, entity, *prefab) : std::nullopt;
		if (!prefabEntity)
		{
			return std::nullopt;
		}
		return PrefabSource{ .Prefab = prefab, .Path = path, .Entity = *prefabEntity, .bPart = !world.Has<Nyx::PrefabInstanceComponent>(entity) };
	}

	bool FollowsPrefab(const Nyx::Registry& world, Nyx::Entity entity, const Nyx::ComponentTypeOps& componentType)
	{
		const Nyx::Reflection::TypeMetadata* type = componentType.TypeMetadata;
		if (componentType.IsRuntime() || type == &Nyx::Reflection::GetTypeMetadata<Nyx::GuidComponent>() ||
			type == &Nyx::Reflection::GetTypeMetadata<Nyx::PrefabInstanceComponent>() || type == &Nyx::Reflection::GetTypeMetadata<Nyx::PrefabPartComponent>())
		{
			return false;
		}
		const bool bRoot = world.Has<Nyx::PrefabInstanceComponent>(entity);
		return !(bRoot && (type == &Nyx::Reflection::GetTypeMetadata<Nyx::TransformComponent>() || type == &Nyx::Reflection::GetTypeMetadata<Nyx::ParentComponent>())) &&
			!(!bRoot && type == &Nyx::Reflection::GetTypeMetadata<Nyx::TransformComponent>());
	}

	std::vector<size_t> FindOverriddenProperties(SceneEditor& editing, Nyx::Entity entity, const Nyx::ComponentTypeOps& componentType)
	{
		const Nyx::Registry& world = editing.Scene.GetRegistry();
		const std::optional<PrefabSource> source = FindPrefabSource(editing, entity);
		if (!source || !FollowsPrefab(world, entity, componentType) || !componentType.Has(world, entity) ||
			!componentType.Has(source->Prefab->GetTemplate(), source->Entity))
		{
			return {};
		}
		return Nyx::FindPrefabOverrides(componentType.GetConst(world, entity), componentType.GetConst(source->Prefab->GetTemplate(), source->Entity),
			*componentType.TypeMetadata, source->bPart);
	}

	bool IsAddedToInstance(SceneEditor& editing, Nyx::Entity entity, const Nyx::ComponentTypeOps& componentType)
	{
		const Nyx::Registry& world = editing.Scene.GetRegistry();
		const std::optional<PrefabSource> source = FindPrefabSource(editing, entity);
		return source && FollowsPrefab(world, entity, componentType) && componentType.Has(world, entity) &&
			!componentType.Has(source->Prefab->GetTemplate(), source->Entity);
	}

	bool RevertToPrefab(SceneEditor& editing, Nyx::Entity entity, const Nyx::ComponentTypeOps& componentType, std::optional<size_t> propertyIndex)
	{
		const std::vector<size_t> overridden = FindOverriddenProperties(editing, entity, componentType);
		const std::optional<PrefabSource> source = FindPrefabSource(editing, entity);
		if (overridden.empty() || !source)
		{
			return false;
		}
		editing.CancelPendingEdits();
		Nyx::Registry& world = editing.Scene.GetRegistry();
		void* component = componentType.Get(world, entity);
		const void* prefabComponent = componentType.GetConst(source->Prefab->GetTemplate(), source->Entity);

		Transaction transaction;
		transaction.Label = "Revert to Prefab";
		for (const size_t index : overridden)
		{
			if (!propertyIndex || *propertyIndex == index)
			{
				AppendCopy(transaction.Changes, MakeSceneEntityRef(editing.Scene, entity), component, prefabComponent, *componentType.TypeMetadata, index, {}, false);
			}
		}
		if (transaction.IsEmpty())
		{
			return false;
		}
		editing.Transactions.Push(std::move(transaction));
		return true;
	}

	bool ApplyToPrefab(SceneEditor& editing, Nyx::Entity entity, const Nyx::ComponentTypeOps& componentType, std::optional<size_t> propertyIndex)
	{
		const std::optional<PrefabSource> source = FindPrefabSource(editing, entity);
		Nyx::Registry& world = editing.Scene.GetRegistry();
		if (!source || !FollowsPrefab(world, entity, componentType) || !componentType.Has(world, entity))
		{
			return false;
		}
		editing.CancelPendingEdits();
		editing.Prefabs.Remember(source->Path);

		Nyx::Registry& prefabWorld = source->Prefab->GetEditableTemplate();
		const Nyx::EntityGuid local = prefabWorld.Get<Nyx::GuidComponent>(source->Entity).Guid;
		const ObjectRef prefabRef = editing.PrefabDomain.GetRef(source->Path, local);
		const void* component = componentType.GetConst(world, entity);
		Transaction transaction;
		transaction.Label = "Apply to Prefab";

		// A component the instance added: the prefab gets it whole
		if (!componentType.Has(prefabWorld, source->Entity))
		{
			if (propertyIndex)
			{
				return false;
			}
			void* added = componentType.Add(prefabWorld, source->Entity);
			Nyx::BinaryWriter writer;
			Nyx::ReflectedArchiveSerializer::SerializeObject(writer, component, *componentType.TypeMetadata);
			LoadReflectedProperty(added, *componentType.TypeMetadata, writer.GetBytes());
			transaction.Changes.push_back(Change{ EChangeKind::AddSubobject,
				AddSubobjectChange{ .Target = prefabRef, .AfterAdd = CaptureSubobjectSnapshot(added, *componentType.TypeMetadata) } });

			// The instance's own, kept in the step: undone, the prefab loses it and the instance keeps it
			void* own = componentType.Get(world, entity);
			for (size_t index = 0; index < componentType.TypeMetadata->PropertyCount; ++index)
			{
				AppendCopy(transaction.Changes, MakeSceneEntityRef(editing.Scene, entity), own, component, *componentType.TypeMetadata, index, {}, true);
			}
			editing.Transactions.Push(std::move(transaction));
			return true;
		}

		const std::vector<size_t> overridden = FindOverriddenProperties(editing, entity, componentType);
		void* prefabComponent = componentType.Get(prefabWorld, source->Entity);
		void* instanceComponent = componentType.Get(world, entity);
		for (const size_t index : overridden)
		{
			if (!propertyIndex || *propertyIndex == index)
			{
				AppendCopy(transaction.Changes, prefabRef, prefabComponent, component, *componentType.TypeMetadata, index, {}, false);

				// The instance's own value, kept in the step: undoing it gives the prefab its value back,
				// and the instance keeps this one
				AppendCopy(transaction.Changes, MakeSceneEntityRef(editing.Scene, entity), instanceComponent, component, *componentType.TypeMetadata, index, {}, true);
			}
		}
		if (transaction.IsEmpty())
		{
			return false;
		}
		editing.Transactions.Push(std::move(transaction));
		return true;
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
		if (editing.PrefabRoot.IsValid())
		{
			LOG_WARNING("Place Prefab: a prefab can't hold an instance of a prefab yet");
			return std::nullopt;
		}
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
		editing.Scene.Select(root);
		return root;
	}

	bool CreatePrefab(SceneEditor& editing, Nyx::Entity entity, const std::filesystem::path& file, const std::string& prefabPath)
	{
		Nyx::Registry& world = editing.Scene.GetRegistry();
		const std::vector<Nyx::Entity> entities = GetSelfAndBelow(world, entity);
		if (entities.empty() || !world.Has<Nyx::GuidComponent>(entity) || editing.PrefabRoot.IsValid())
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
