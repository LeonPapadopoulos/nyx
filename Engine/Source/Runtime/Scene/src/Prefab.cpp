#include "NyxPCH.h"
#include "Prefab.h"

#include "BinaryArchive.h"
#include "ComponentTypeRegistry.h"
#include "GuidComponent.h"
#include "Log.h"
#include "ParentComponent.h"
#include "Paths.h"
#include "ReflectedArchiveSerializer.h"
#include "SceneSerializer.h"
#include "TransformComponent.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace Nyx
{
	namespace
	{
		const ComponentTypeOps* FindOps(const Reflection::TypeMetadata& type)
		{
			return ComponentTypeRegistry::Get().FindByTypeMetadata(type);
		}

		template <typename T>
		bool IsType(const ComponentTypeOps& ops)
		{
			return ops.TypeMetadata == &Reflection::GetTypeMetadata<T>();
		}

		// A component's property, alone, as a property block: what two components are compared by
		std::vector<std::byte> SaveProperty(const void* component, const Reflection::TypeMetadata& type, size_t index)
		{
			BinaryWriter writer;
			ReflectedArchiveSerializer::SerializeProperties(writer, component, type, { index });
			return writer.GetBytes();
		}

		// The Serialize properties of `component` whose values differ from `prefab`'s, but `skipped`
		std::vector<size_t> FindDifferentProperties(const void* component, const void* prefab, const Reflection::TypeMetadata& type, std::string_view skipped = {})
		{
			std::vector<size_t> different;
			for (size_t index = 0; index < type.PropertyCount; ++index)
			{
				const Reflection::PropertyMetadata& property = type.Properties[index];
				if (!Reflection::HasFlag(property.Flags, Reflection::EPropertyFlags::Serialize) || (!skipped.empty() && std::string_view(property.Name) == skipped))
				{
					continue;
				}
				if (SaveProperty(component, type, index) != SaveProperty(prefab, type, index))
				{
					different.push_back(index);
				}
			}
			return different;
		}

		// A component type every entity has for itself, not from its prefab: its guid and the marks of
		// instances and parts
		bool IsOwnType(const ComponentTypeOps& ops)
		{
			return IsType<GuidComponent>(ops) || IsType<PrefabInstanceComponent>(ops) || IsType<PrefabPartComponent>(ops);
		}

		// The prefab entity's component types the entity doesn't have
		std::vector<std::string> FindRemovedComponents(const Registry& world, Entity entity, const Registry& prefabWorld, Entity prefabEntity, bool bRoot)
		{
			std::vector<std::string> removed;
			for (const ComponentTypeOps& ops : ComponentTypeRegistry::Get().GetAll())
			{
				const bool bOwn = IsOwnType(ops) || (bRoot && (IsType<TransformComponent>(ops) || IsType<ParentComponent>(ops)));
				if (!ops.IsRuntime() && !bOwn && ops.Has(prefabWorld, prefabEntity) && !ops.Has(world, entity))
				{
					removed.push_back(ops.TypeMetadata->Name);
				}
			}
			return removed;
		}

		// One component of an entity record: whole, or some of its properties
		struct WrittenComponent
		{
			const ComponentTypeOps* Ops = nullptr;
			const void* Component = nullptr;
			bool bWhole = true;
			std::vector<size_t> Properties;
		};

		bool WriteComponents(BinaryWriter& writer, const std::vector<WrittenComponent>& components)
		{
			writer.WriteUInt32(static_cast<uint32_t>(components.size()));
			for (const WrittenComponent& written : components)
			{
				writer.WriteString(written.Ops->TypeMetadata->Name);
				const bool bWritten = written.bWhole
					? ReflectedArchiveSerializer::SerializeObject(writer, written.Component, *written.Ops->TypeMetadata)
					: ReflectedArchiveSerializer::SerializeProperties(writer, written.Component, *written.Ops->TypeMetadata, written.Properties);
				if (!bWritten)
				{
					return false;
				}
			}
			return true;
		}

		// splitmix64's finaliser: every bit of the input reaches every bit of the output
		uint64_t Mix(uint64_t value)
		{
			value += 0x9E3779B97F4A7C15ull;
			value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
			value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
			return value ^ (value >> 31);
		}
	}

	EntityGuid GetPrefabPartGuid(EntityGuid instance, EntityGuid local)
	{
		const uint64_t value = Mix(Mix(instance.Value) ^ local.Value);
		return EntityGuid{ value != 0 ? value : 1 };
	}

	bool PrefabAsset::SaveToFile(const Registry& world, Entity root, const std::filesystem::path& path)
	{
		const std::vector<Entity> entities = GetTransformSubtree(world, root);
		if (entities.empty())
		{
			return false;
		}

		BinaryWriter writer;
		writer.WriteUInt32(PrefabFileMagic);
		writer.WriteUInt32(SceneFileVersion);
		writer.WriteUInt32(static_cast<uint32_t>(entities.size()));
		for (const Entity entity : entities)
		{
			if (world.Has<PrefabInstanceComponent>(entity) || world.Has<PrefabPartComponent>(entity))
			{
				LOG_WARNING("Prefab: '{0}' can't hold an instance of a prefab yet", path.string());
				return false;
			}

			// The root without its parent: an instance has its own
			std::vector<WrittenComponent> components;
			for (const ComponentTypeOps& ops : ComponentTypeRegistry::Get().GetAll())
			{
				if (!ops.IsRuntime() && ops.Has(world, entity) && !(entity == root && IsType<ParentComponent>(ops)))
				{
					components.push_back(WrittenComponent{ .Ops = &ops, .Component = ops.GetConst(world, entity) });
				}
			}
			if (!WriteComponents(writer, components))
			{
				LOG_WARNING("Prefab: '{0}' can't be saved, since a component can't be written", path.string());
				return false;
			}
		}
		return writer.SaveToFile(path);
	}

	bool PrefabAsset::LoadFromFile(const std::filesystem::path& path, std::string& outError)
	{
		BinaryReader reader;
		if (!reader.LoadFromFile(path))
		{
			outError = "can't be read";
			return false;
		}
		std::vector<std::byte> bytes(reader.GetRemainingSize());
		if (!reader.ReadBytes(bytes.data(), bytes.size()))
		{
			outError = "can't be read";
			return false;
		}
		return LoadFromMemory(bytes, outError);
	}

	bool PrefabAsset::LoadFromMemory(const std::vector<std::byte>& bytes, std::string& outError)
	{
		BinaryReader reader;
		reader.LoadFromMemory(bytes);
		uint32_t magic = 0;
		uint32_t version = 0;
		uint32_t count = 0;
		if (!reader.ReadUInt32(magic) || magic != PrefabFileMagic)
		{
			outError = "is not a prefab file";
			return false;
		}
		if (!reader.ReadUInt32(version) || version != SceneFileVersion)
		{
			outError = "is a prefab of another version than this build reads";
			return false;
		}
		if (!reader.ReadUInt32(count) || count == 0)
		{
			outError = "has no entities";
			return false;
		}

		auto loaded = std::make_unique<Registry>();
		std::vector<Entity> entities;
		std::vector<std::vector<std::byte>> entityBytes;
		std::unordered_map<EntityGuid, Entity> byLocal;
		ScenePostLoadContext noAssets{};
		ReadWarnings warnings;
		for (uint32_t index = 0; index < count; ++index)
		{
			const Entity entity = loaded->CreateEntity();
			if (!SceneSerializer::ReadEntity(reader, *loaded, entity, noAssets, warnings) || !reader.IsValid())
			{
				outError = "ends early or is damaged";
				return false;
			}
			if (!loaded->Has<GuidComponent>(entity) || !byLocal.emplace(loaded->Get<GuidComponent>(entity).Guid, entity).second)
			{
				outError = "has an entity without an id of its own";
				return false;
			}

			// As instances read it: what the file has, without what this build doesn't know
			BinaryWriter writer;
			SceneSerializer::WriteEntity(*loaded, entity, writer);
			entities.push_back(entity);
			entityBytes.push_back(writer.GetBytes());
		}
		warnings.Log("Prefab");

		Template = std::move(loaded);
		Entities = std::move(entities);
		EntityBytes = std::move(entityBytes);
		ByLocal = std::move(byLocal);
		return true;
	}

	std::optional<Entity> PrefabAsset::FindLocal(EntityGuid local) const
	{
		const auto found = ByLocal.find(local);
		return found != ByLocal.end() ? std::optional<Entity>(found->second) : std::nullopt;
	}

	void ApplyPrefab(Registry& world, Entity root, const PrefabAsset& prefab, ScenePostLoadContext& context, std::vector<Entity>* outParts)
	{
		if (outParts)
		{
			outParts->clear();
		}
		if (prefab.IsEmpty() || !world.Has<GuidComponent>(root))
		{
			return;
		}

		const Registry& prefabWorld = prefab.GetTemplate();
		const Entity prefabRoot = prefab.GetRoot();
		const EntityGuid rootGuid = world.Get<GuidComponent>(root).Guid;
		const EntityGuid prefabRootGuid = prefabWorld.Get<GuidComponent>(prefabRoot).Guid;
		const std::vector<std::string> removed =
			world.Has<PrefabInstanceComponent>(root) ? world.Get<PrefabInstanceComponent>(root).RemovedComponents : std::vector<std::string>{};

		// The root: the prefab root's components it hasn't got, as the prefab has them
		ReadWarnings warnings;
		for (const ComponentTypeOps& ops : ComponentTypeRegistry::Get().GetAll())
		{
			if (ops.IsRuntime() || !ops.Has(prefabWorld, prefabRoot) || ops.Has(world, root) ||
				std::find(removed.begin(), removed.end(), ops.TypeMetadata->Name) != removed.end())
			{
				continue;
			}
			BinaryWriter writer;
			ReflectedArchiveSerializer::SerializeObject(writer, ops.GetConst(prefabWorld, prefabRoot), *ops.TypeMetadata);
			BinaryReader reader;
			reader.LoadFromMemory(writer.GetBytes());
			void* component = ops.Add(world, root);
			ReflectedArchiveSerializer::DeserializeObject(reader, component, *ops.TypeMetadata, warnings);
			if (ops.PostLoad)
			{
				context.World = &world;
				context.Entity = root;
				ops.PostLoad(component, context);
			}
		}

		// The parts, each parent before its children, as the prefab has them
		const auto instanceGuidOf = [&](EntityGuid local)
		{
			return local == prefabRootGuid ? rootGuid : GetPrefabPartGuid(rootGuid, local);
		};
		std::unordered_map<EntityGuid, Entity> made{ { rootGuid, root } };
		const std::vector<Entity>& entities = prefab.GetEntities();
		for (size_t index = 1; index < entities.size(); ++index)
		{
			const Entity part = world.CreateEntity();
			BinaryReader reader;
			reader.LoadFromMemory(prefab.GetEntityBytes(index));
			SceneSerializer::ReadEntity(reader, world, part, context, warnings);

			const EntityGuid local = world.Get<GuidComponent>(part).Guid;
			world.Get<GuidComponent>(part).Guid = instanceGuidOf(local);
			world.Add<PrefabPartComponent>(part, PrefabPartComponent{ .Instance = rootGuid, .Local = local });
			if (world.Has<ParentComponent>(part))
			{
				ParentComponent& link = world.Get<ParentComponent>(part);
				link.Parent = instanceGuidOf(link.Parent);
				const auto parent = made.find(link.Parent);
				if (parent != made.end() && world.Has<TransformComponent>(parent->second) && world.Has<TransformComponent>(part))
				{
					TransformComponent& transform = world.Get<TransformComponent>(part);
					const TransformComponent placed = GetChildWorldTransform(world.Get<TransformComponent>(parent->second), link);
					transform.Position = placed.Position;
					transform.Rotation = placed.Rotation;
					transform.Scale = placed.Scale;
				}
			}
			made.emplace(world.Get<GuidComponent>(part).Guid, part);
			if (outParts)
			{
				outParts->push_back(part);
			}
		}
		warnings.Log("Prefab instance " + GuidToText(rootGuid));
	}

	bool WritePrefabInstanceEntity(const Registry& world, Entity entity, const PrefabAsset& prefab, BinaryWriter& writer, bool* outbNothing)
	{
		const bool bRoot = world.Has<PrefabInstanceComponent>(entity);
		const Registry& prefabWorld = prefab.GetTemplate();
		const std::optional<Entity> prefabEntity =
			bRoot ? std::optional<Entity>(prefab.GetRoot()) : prefab.FindLocal(world.Get<PrefabPartComponent>(entity).Local);
		if (outbNothing)
		{
			*outbNothing = false;
		}
		if (!prefabEntity)
		{
			// A part of an entity the prefab no longer has: loading wouldn't make it
			if (outbNothing)
			{
				*outbNothing = true;
			}
			return true;
		}

		// The marks, with what this one doesn't have of the prefab's
		PrefabInstanceComponent instance = bRoot ? world.Get<PrefabInstanceComponent>(entity) : PrefabInstanceComponent{};
		PrefabPartComponent part = bRoot ? PrefabPartComponent{} : world.Get<PrefabPartComponent>(entity);
		std::vector<std::string>& removed = bRoot ? instance.RemovedComponents : part.RemovedComponents;
		removed = FindRemovedComponents(world, entity, prefabWorld, *prefabEntity, bRoot);

		// The root's guid comes first, so its parts' guids are known when its prefab is made
		std::vector<WrittenComponent> components;
		if (bRoot)
		{
			if (const ComponentTypeOps* guidOps = FindOps(Reflection::GetTypeMetadata<GuidComponent>()); guidOps && world.Has<GuidComponent>(entity))
			{
				components.push_back(WrittenComponent{ .Ops = guidOps, .Component = &world.Get<GuidComponent>(entity) });
			}
			components.push_back(WrittenComponent{ .Ops = FindOps(Reflection::GetTypeMetadata<PrefabInstanceComponent>()), .Component = &instance });
		}
		else
		{
			components.push_back(WrittenComponent{ .Ops = FindOps(Reflection::GetTypeMetadata<PrefabPartComponent>()), .Component = &part });
		}
		const size_t marks = components.size();

		for (const ComponentTypeOps& ops : ComponentTypeRegistry::Get().GetAll())
		{
			if (ops.IsRuntime() || IsOwnType(ops) || !ops.Has(world, entity))
			{
				continue;
			}
			const void* component = ops.GetConst(world, entity);

			// The root's place is its own; a part's is made from its prefab's and the root's
			if (IsType<TransformComponent>(ops))
			{
				if (bRoot)
				{
					components.push_back(WrittenComponent{ .Ops = &ops, .Component = component });
				}
				continue;
			}
			if (bRoot && IsType<ParentComponent>(ops))
			{
				components.push_back(WrittenComponent{ .Ops = &ops, .Component = component });
				continue;
			}

			// Added: whole. From the prefab: what differs, a part's parent's guid aside
			if (!ops.Has(prefabWorld, *prefabEntity))
			{
				components.push_back(WrittenComponent{ .Ops = &ops, .Component = component });
				continue;
			}
			std::vector<size_t> different = FindDifferentProperties(component, ops.GetConst(prefabWorld, *prefabEntity), *ops.TypeMetadata,
				!bRoot && IsType<ParentComponent>(ops) ? std::string_view("Parent") : std::string_view());
			if (!different.empty())
			{
				components.push_back(WrittenComponent{ .Ops = &ops, .Component = component, .bWhole = false, .Properties = std::move(different) });
			}
		}

		if (outbNothing && !bRoot)
		{
			*outbNothing = components.size() == marks && removed.empty();
			if (*outbNothing)
			{
				return true;
			}
		}
		return WriteComponents(writer, components);
	}

	const PrefabAsset* PrefabLibrary::Find(const std::string& path)
	{
		if (const auto found = Prefabs.find(path); found != Prefabs.end())
		{
			return found->second.get();
		}

		// Kept also when it can't be read, so that is logged once
		std::unique_ptr<PrefabAsset> prefab = std::make_unique<PrefabAsset>();
		const std::filesystem::path file = Paths::FindAsset(path);
		std::string error;
		if (!prefab->LoadFromFile(file, error))
		{
			LOG_WARNING("Prefab '{0}' {1}; its instances have only what the scene has of them", path, error);
			prefab.reset();
		}
		return Prefabs.emplace(path, std::move(prefab)).first->second.get();
	}

	void PrefabLibrary::Forget(const std::string& path)
	{
		Prefabs.erase(path);
	}

	void PrefabLibrary::Set(const std::string& path, std::unique_ptr<PrefabAsset> prefab)
	{
		Prefabs[path] = std::move(prefab);
	}
}
