#include "NyxPCH.h"
#include "WorldSave.h"

#include "ComponentTypeRegistry.h"
#include "EntityGuidLookup.h"
#include "GuidComponent.h"
#include "IAssetResolver.h"
#include "ParentComponent.h"
#include "Prefab.h"
#include "ReflectedArchiveSerializer.h"
#include "SceneSerializer.h"
#include "SdfTerrain.h"
#include "TerrainSave.h"
#include "TransformComponent.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <fstream>
#include <map>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace
{
	using namespace Nyx;

	constexpr std::string_view HeaderSection = "Header";
	constexpr std::string_view DestroyedSection = "Destroyed";
	constexpr std::string_view ChangedSection = "Changed";
	constexpr std::string_view MadeSection = "Made";
	constexpr std::string_view InstancesSection = "Instances";
	constexpr std::string_view MotionsSection = "Motions";
	constexpr std::string_view TerrainsSection = "Terrains";

	// A terrain's properties but its edits, which a save holds on their own
	const std::vector<size_t>& GetTerrainPropertiesWithoutEdits()
	{
		static const std::vector<size_t> Properties = []
		{
			const Nyx::Reflection::TypeMetadata& type = Nyx::Reflection::GetTypeMetadata<SdfTerrainComponent>();
			std::vector<size_t> properties;
			for (size_t index = 0; index < type.PropertyCount; ++index)
			{
				if (std::string_view(type.Properties[index].Name) != "Edits")
				{
					properties.push_back(index);
				}
			}
			return properties;
		}();
		return Properties;
	}

	// FNV-1a over the file's bytes; 0 if it can't be read
	uint64_t HashFile(const std::filesystem::path& path)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file)
		{
			return 0;
		}

		uint64_t hash = 14695981039346656037ull;
		char buffer[65536];
		while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0)
		{
			for (std::streamsize index = 0; index < file.gcount(); ++index)
			{
				hash = (hash ^ static_cast<uint8_t>(buffer[index])) * 1099511628211ull;
			}
		}
		return hash;
	}

	std::span<const std::byte> GetComponentBytes(const WorldSnapshot& snapshot, const WorldSnapshot::Component& component)
	{
		return std::span<const std::byte>(snapshot.Bytes.GetBytes()).subspan(component.Offset, component.Size);
	}

	std::span<const WorldSnapshot::Component> GetComponents(const WorldSnapshot& snapshot, const WorldSnapshot::SavedEntity& entity)
	{
		return std::span<const WorldSnapshot::Component>(snapshot.Components).subspan(entity.FirstComponent, entity.ComponentCount);
	}

	const WorldSnapshot::Component* FindComponent(std::span<const WorldSnapshot::Component> components, const Nyx::Reflection::TypeMetadata& type)
	{
		for (const WorldSnapshot::Component& component : components)
		{
			if (component.Type == &type)
			{
				return &component;
			}
		}
		return nullptr;
	}

	template <typename T>
	const WorldSnapshot::Component* FindComponent(std::span<const WorldSnapshot::Component> components)
	{
		return FindComponent(components, Nyx::Reflection::GetTypeMetadata<T>());
	}

	template <typename T>
	bool IsType(const WorldSnapshot::Component& component)
	{
		return component.Type == &Nyx::Reflection::GetTypeMetadata<T>();
	}

	// The values of a snapshot's component again, for what isn't told from its bytes
	template <typename T>
	bool ReadValues(const WorldSnapshot& snapshot, const WorldSnapshot::Component& component, T& outValues)
	{
		const std::span<const std::byte> bytes = GetComponentBytes(snapshot, component);
		BinaryReader reader;
		reader.LoadFromMemory(std::vector<std::byte>(bytes.begin(), bytes.end()));
		ReadWarnings warnings;
		return ReflectedArchiveSerializer::DeserializeObject(reader, &outValues, Nyx::Reflection::GetTypeMetadata<T>(), warnings);
	}

	void WriteSection(BinaryWriter& out, std::string_view name, const BinaryWriter& section)
	{
		out.WriteString(std::string(name));
		out.WriteBlock(section);
	}

	// An entity of the snapshot, whole, as SceneSerializer::WriteEntity() stores it
	void WriteWholeEntity(const WorldSnapshot& snapshot, const WorldSnapshot::SavedEntity& entity, BinaryWriter& out)
	{
		out.WriteUInt32(entity.ComponentCount);
		for (const WorldSnapshot::Component& component : GetComponents(snapshot, entity))
		{
			const std::span<const std::byte> bytes = GetComponentBytes(snapshot, component);
			out.WriteString(component.Type->Name);
			out.WriteBytes(bytes.data(), bytes.size());
		}
	}

	// What differs between an entity and how it was: the components taken away, those it got whole,
	// and of the others the properties that differ. False if a component's bytes aren't whole.
	bool WriteEntityChanges(const WorldSnapshot& baseSnapshot, const WorldSnapshot::SavedEntity& baseEntity, const WorldSnapshot& snapshot,
		const WorldSnapshot::SavedEntity& entity, BinaryWriter& outRecord, bool& outbChanged)
	{
		const std::span<const WorldSnapshot::Component> components = GetComponents(snapshot, entity);
		const std::span<const WorldSnapshot::Component> baseComponents = GetComponents(baseSnapshot, baseEntity);

		BinaryWriter removed;
		uint32_t removedCount = 0;
		for (const WorldSnapshot::Component& baseComponent : baseComponents)
		{
			if (!FindComponent(components, *baseComponent.Type))
			{
				removed.WriteString(baseComponent.Type->Name);
				++removedCount;
			}
		}

		BinaryWriter changed;
		uint32_t changedCount = 0;
		for (const WorldSnapshot::Component& component : components)
		{
			const std::span<const std::byte> bytes = GetComponentBytes(snapshot, component);
			const WorldSnapshot::Component* baseComponent = FindComponent(baseComponents, *component.Type);
			if (!baseComponent)
			{
				changed.WriteString(component.Type->Name);
				changed.WriteBytes(bytes.data(), bytes.size());
				++changedCount;
				continue;
			}

			// Written to one side first: the name goes in front only if something differs
			BinaryWriter properties;
			bool bDiffers = false;
			if (!ReflectedArchiveSerializer::WriteChangedProperties(properties, bytes, GetComponentBytes(baseSnapshot, *baseComponent), bDiffers))
			{
				return false;
			}
			if (bDiffers)
			{
				changed.WriteString(component.Type->Name);
				changed.WriteBytes(properties.GetBytes().data(), properties.GetBytes().size());
				++changedCount;
			}
		}

		outbChanged = removedCount != 0 || changedCount != 0;
		if (outbChanged)
		{
			outRecord.WriteUInt64(entity.Guid.Value);
			outRecord.WriteUInt32(removedCount);
			outRecord.WriteBytes(removed.GetBytes().data(), removed.GetBytes().size());
			outRecord.WriteUInt32(changedCount);
			outRecord.WriteBytes(changed.GetBytes().data(), changed.GetBytes().size());
		}
		return true;
	}

	// A root or a part of an instance made at run time as what differs from its prefab, as a scene's
	// load reads it. As a scene file has one (WritePrefabInstanceEntity()), but where a part is and which
	// parent it has may be anything in a running world, so each part has its transform, and its parent's
	// guid if that isn't the one its instance makes. The root's transform goes before its
	// PrefabInstanceComponent, so that a part its prefab has gained since is made where it belongs.
	bool WriteInstanceEntity(const WorldSnapshot& snapshot, const WorldSnapshot::SavedEntity& entity, const WorldSnapshot::Prefab& prefab,
		const WorldSnapshot::SavedEntity& prefabEntity, EntityGuid instance, BinaryWriter& out)
	{
		const std::span<const WorldSnapshot::Component> components = GetComponents(snapshot, entity);
		const std::span<const WorldSnapshot::Component> prefabComponents = GetComponents(prefab.Entities, prefabEntity);
		const bool bRoot = entity.Guid == instance;

		const auto isMark = [](const WorldSnapshot::Component& component)
		{
			return IsType<GuidComponent>(component) || IsType<PrefabInstanceComponent>(component) || IsType<PrefabPartComponent>(component);
		};

		std::vector<std::string> removed;
		for (const WorldSnapshot::Component& component : prefabComponents)
		{
			const bool bOwn = isMark(component) || (bRoot && (IsType<TransformComponent>(component) || IsType<ParentComponent>(component)));
			if (!bOwn && !FindComponent(components, *component.Type))
			{
				removed.push_back(component.Type->Name);
			}
		}

		BinaryWriter written;
		uint32_t count = 0;
		const auto writeWhole = [&](const WorldSnapshot::Component* component)
		{
			if (component)
			{
				const std::span<const std::byte> bytes = GetComponentBytes(snapshot, *component);
				written.WriteString(component->Type->Name);
				written.WriteBytes(bytes.data(), bytes.size());
				++count;
			}
		};
		const auto writeMark = [&]<typename T>(T& mark)
		{
			const Nyx::Reflection::TypeMetadata& type = Nyx::Reflection::GetTypeMetadata<T>();
			const WorldSnapshot::Component* component = FindComponent(components, type);
			if (!component || !ReadValues(snapshot, *component, mark))
			{
				return false;
			}
			mark.RemovedComponents = removed;
			written.WriteString(type.Name);
			++count;
			return ReflectedArchiveSerializer::SerializeObject(written, &mark, type);
		};

		const WorldSnapshot::Component* transform = FindComponent<TransformComponent>(components);
		PrefabInstanceComponent instanceMark;
		PrefabPartComponent partMark;
		if (bRoot)
		{
			writeWhole(FindComponent<GuidComponent>(components));
			writeWhole(transform);
			writeWhole(FindComponent<ParentComponent>(components));
			if (!writeMark(instanceMark))
			{
				return false;
			}
		}
		else
		{
			if (!writeMark(partMark))
			{
				return false;
			}
			writeWhole(transform);
		}

		for (const WorldSnapshot::Component& component : components)
		{
			if (isMark(component) || IsType<TransformComponent>(component) || (bRoot && IsType<ParentComponent>(component)))
			{
				continue;
			}
			const WorldSnapshot::Component* prefabComponent = FindComponent(prefabComponents, *component.Type);
			if (!prefabComponent)
			{
				writeWhole(&component);
				continue;
			}

			// A part's parent is compared with the one its instance makes: the prefab's has its own ids
			BinaryWriter madeLink;
			std::span<const std::byte> compared = GetComponentBytes(prefab.Entities, *prefabComponent);
			if (IsType<ParentComponent>(component))
			{
				ParentComponent link;
				if (!ReadValues(prefab.Entities, *prefabComponent, link))
				{
					return false;
				}
				link.Parent = link.Parent == prefab.Root ? instance : GetPrefabPartGuid(instance, link.Parent);
				if (!ReflectedArchiveSerializer::SerializeObject(madeLink, &link, *component.Type))
				{
					return false;
				}
				compared = madeLink.GetBytes();
			}

			BinaryWriter properties;
			bool bDiffers = false;
			if (!ReflectedArchiveSerializer::WriteChangedProperties(properties, GetComponentBytes(snapshot, component), compared, bDiffers))
			{
				return false;
			}
			if (bDiffers)
			{
				written.WriteString(component.Type->Name);
				written.WriteBytes(properties.GetBytes().data(), properties.GetBytes().size());
				++count;
			}
		}

		out.WriteUInt32(count);
		out.WriteBytes(written.GetBytes().data(), written.GetBytes().size());
		return true;
	}

	// An instance made at run time: its root, its parts as they are, and those that are gone, which
	// loading would make again. outWritten gets the guids of what it holds.
	bool WriteInstance(const WorldSnapshot& snapshot, const WorldSnapshot::SavedEntity& root, const WorldSnapshot::Prefab& prefab,
		const std::unordered_map<EntityGuid, const WorldSnapshot::SavedEntity*>& byGuid, BinaryWriter& outRecord, std::unordered_set<EntityGuid>& outWritten)
	{
		const auto prefabRoot = std::find_if(prefab.Entities.Entities.begin(), prefab.Entities.Entities.end(),
			[&prefab](const WorldSnapshot::SavedEntity& entity)
			{
				return entity.Guid == prefab.Root;
			});
		if (prefabRoot == prefab.Entities.Entities.end())
		{
			return false;
		}

		outRecord.WriteUInt64(root.Guid.Value);
		if (!WriteInstanceEntity(snapshot, root, prefab, *prefabRoot, root.Guid, outRecord))
		{
			return false;
		}
		outWritten.insert(root.Guid);

		BinaryWriter parts;
		uint32_t partCount = 0;
		BinaryWriter gone;
		uint32_t goneCount = 0;
		for (const WorldSnapshot::SavedEntity& prefabEntity : prefab.Entities.Entities)
		{
			if (prefabEntity.Guid == prefab.Root)
			{
				continue;
			}
			const EntityGuid guid = GetPrefabPartGuid(root.Guid, prefabEntity.Guid);
			const auto found = byGuid.find(guid);
			if (found == byGuid.end() || !FindComponent<PrefabPartComponent>(GetComponents(snapshot, *found->second)))
			{
				gone.WriteUInt64(guid.Value);
				++goneCount;
				continue;
			}
			if (!WriteInstanceEntity(snapshot, *found->second, prefab, prefabEntity, root.Guid, parts))
			{
				return false;
			}
			outWritten.insert(guid);
			++partCount;
		}

		outRecord.WriteUInt32(partCount);
		outRecord.WriteBytes(parts.GetBytes().data(), parts.GetBytes().size());
		outRecord.WriteUInt32(goneCount);
		outRecord.WriteBytes(gone.GetBytes().data(), gone.GetBytes().size());
		return true;
	}

	bool ReadDestroyed(BinaryReader& section, Registry& world, const EntityGuidLookup& guids)
	{
		uint32_t count = 0;
		if (!section.ReadUInt32(count))
		{
			return false;
		}
		for (uint32_t index = 0; index < count; ++index)
		{
			uint64_t guid = 0;
			if (!section.ReadUInt64(guid))
			{
				return false;
			}
			// One the scene no longer has is gone anyway
			if (const std::optional<Entity> entity = FindEntityByGuid(world, guids, EntityGuid{ guid }))
			{
				world.DestroyEntity(*entity);
			}
		}
		return true;
	}

	bool ReadChanged(BinaryReader& section, Registry& world, const EntityGuidLookup& guids, ScenePostLoadContext& postLoadContext, ReadWarnings& warnings)
	{
		uint32_t count = 0;
		if (!section.ReadUInt32(count))
		{
			return false;
		}
		for (uint32_t index = 0; index < count; ++index)
		{
			BinaryReader record;
			uint64_t guid = 0;
			uint32_t removedCount = 0;
			if (!section.ReadBlock(record) || !record.ReadUInt64(guid) || !record.ReadUInt32(removedCount))
			{
				return false;
			}

			const std::optional<Entity> entity = FindEntityByGuid(world, guids, EntityGuid{ guid });
			if (!entity)
			{
				warnings.Add("dropped what the save has of an entity its scene no longer has");
				continue;
			}

			for (uint32_t removedIndex = 0; removedIndex < removedCount; ++removedIndex)
			{
				std::string name;
				if (!record.ReadString(name))
				{
					return false;
				}
				if (const ComponentTypeOps* ops = ComponentTypeRegistry::Get().FindByName(name))
				{
					ops->Remove(world, *entity);
				}
			}

			if (!SceneSerializer::ReadEntity(record, world, *entity, postLoadContext, warnings))
			{
				return false;
			}
		}
		return true;
	}

	bool ReadMade(BinaryReader& section, Registry& world, const EntityGuidLookup& guids, ScenePostLoadContext& postLoadContext, ReadWarnings& warnings)
	{
		uint32_t count = 0;
		if (!section.ReadUInt32(count))
		{
			return false;
		}
		SceneSerializer::PrefabReadState prefabs;
		for (uint32_t index = 0; index < count; ++index)
		{
			BinaryReader record;
			uint64_t guid = 0;
			if (!section.ReadBlock(record) || !record.ReadUInt64(guid))
			{
				return false;
			}

			// The scene got an entity of this guid after the save was made: the scene's stays
			if (FindEntityByGuid(world, guids, EntityGuid{ guid }))
			{
				warnings.Add("dropped an entity the save made, as its scene now has one of that guid");
				continue;
			}

			if (!SceneSerializer::ReadSceneEntity(record, world, world.CreateEntity(), postLoadContext, warnings, &prefabs))
			{
				return false;
			}
		}
		return true;
	}

	bool ReadInstances(BinaryReader& section, Registry& world, const EntityGuidLookup& guids, ScenePostLoadContext& postLoadContext, ReadWarnings& warnings)
	{
		uint32_t count = 0;
		if (!section.ReadUInt32(count))
		{
			return false;
		}
		for (uint32_t index = 0; index < count; ++index)
		{
			BinaryReader record;
			uint64_t guid = 0;
			if (!section.ReadBlock(record) || !record.ReadUInt64(guid))
			{
				return false;
			}

			if (FindEntityByGuid(world, guids, EntityGuid{ guid }))
			{
				warnings.Add("dropped an instance the save made, as its scene now has an entity of that guid");
				continue;
			}

			SceneSerializer::PrefabReadState prefabs;
			uint32_t partCount = 0;
			if (!SceneSerializer::ReadSceneEntity(record, world, world.CreateEntity(), postLoadContext, warnings, &prefabs) || !record.ReadUInt32(partCount))
			{
				return false;
			}

			// A part's record goes into the part its instance made, and its own entity with it. If that
			// stays, the prefab no longer has the part, and the record is too little to be an entity.
			for (uint32_t part = 0; part < partCount; ++part)
			{
				const Entity read = world.CreateEntity();
				if (!SceneSerializer::ReadSceneEntity(record, world, read, postLoadContext, warnings, &prefabs))
				{
					return false;
				}
				if (world.IsAlive(read))
				{
					warnings.Add("dropped what the save has of a part its prefab no longer has");
					world.DestroyEntity(read);
				}
			}

			uint32_t goneCount = 0;
			if (!record.ReadUInt32(goneCount))
			{
				return false;
			}
			for (uint32_t gone = 0; gone < goneCount; ++gone)
			{
				uint64_t part = 0;
				if (!record.ReadUInt64(part))
				{
					return false;
				}
				if (const auto made = prefabs.Parts.find(EntityGuid{ part }); made != prefabs.Parts.end())
				{
					world.DestroyEntity(made->second);
				}
			}
		}
		return true;
	}

	// The terrains whose edits aren't those of the base. Every edit stays, so all can be undone later.
	void WriteTerrains(const WorldSaveBase& base, const WorldSnapshot& snapshot, BinaryWriter& outRecords, uint32_t& outCount)
	{
		outCount = 0;
		for (const WorldSnapshot::Terrain& terrain : snapshot.Terrains)
		{
			const auto scene = std::find_if(base.Entities.Terrains.begin(), base.Entities.Terrains.end(),
				[&terrain](const WorldSnapshot::Terrain& candidate)
				{
					return candidate.Guid == terrain.Guid;
				});
			const bool bFromScene = scene != base.Entities.Terrains.end();
			const size_t sceneCount = bFromScene ? scene->Values.Edits.size() : 0;
			const size_t kept = bFromScene ? CountSameTerrainEdits(terrain.Values, scene->Values) : 0;
			if (bFromScene && kept == sceneCount && kept == terrain.Values.Edits.size())
			{
				continue;
			}

			std::vector<uint64_t> runs;
			if (bFromScene)
			{
				HashTerrainEditRuns(scene->Values, runs);
			}

			BinaryWriter edits;
			WriteTerrainEdits(terrain.Values, edits, kept > 0 ? terrain.Values.Edits[kept - 1].Revision : 0);

			BinaryWriter record;
			record.WriteUInt64(terrain.Guid.Value);
			record.WriteUInt8(bFromScene ? 1 : 0);
			record.WriteUInt64(kept);
			record.WriteUInt64(sceneCount);
			record.WriteUInt64(bFromScene ? runs[kept] : 0);
			record.WriteUInt64(GetTerrainGeneratorFingerprint());
			record.WriteUInt64(HashTerrainLayers(terrain.Values));
			record.WriteBlock(edits);
			outRecords.WriteBlock(record);
			++outCount;
		}
	}

	bool ReadTerrains(BinaryReader& section, Registry& world, ReadWarnings& warnings)
	{
		uint32_t count = 0;
		if (!section.ReadUInt32(count))
		{
			return false;
		}

		const EntityGuidLookup guids = MakeEntityGuidLookup(world);
		for (uint32_t index = 0; index < count; ++index)
		{
			BinaryReader record;
			BinaryReader block;
			uint64_t guid = 0;
			uint8_t bFromScene = 0;
			uint64_t kept = 0;
			uint64_t sceneCount = 0;
			uint64_t sceneHash = 0;
			uint64_t generator = 0;
			uint64_t layers = 0;
			std::vector<SdfTerrainEdit> saved;
			if (!section.ReadBlock(record) || !record.ReadUInt64(guid) || !record.ReadUInt8(bFromScene) || !record.ReadUInt64(kept) || !record.ReadUInt64(sceneCount) ||
				!record.ReadUInt64(sceneHash) || !record.ReadUInt64(generator) || !record.ReadUInt64(layers) || !record.ReadBlock(block) || !ReadTerrainEdits(block, saved))
			{
				return false;
			}

			const std::optional<Entity> entity = FindEntityByGuid(world, guids, EntityGuid{ guid });
			if (!entity || !world.Has<SdfTerrainComponent>(*entity))
			{
				warnings.Add("dropped the edits of a terrain the world no longer has");
				continue;
			}
			SdfTerrainComponent& terrain = world.Get<SdfTerrainComponent>(*entity);

			// What the save's bakes were made from is still there, or they make their values again
			bool bSameGround = generator == GetTerrainGeneratorFingerprint() && layers == HashTerrainLayers(terrain);
			if (!bSameGround)
			{
				warnings.Add("the generator or the layers of a terrain have changed since it was saved, so the ground under its edits may differ");
			}

			if (bFromScene != 0)
			{
				std::vector<uint64_t> runs;
				HashTerrainEditRuns(terrain, runs);
				const bool bSameEdits = terrain.Edits.size() == sceneCount && kept < runs.size() && runs[static_cast<size_t>(kept)] == sceneHash;
				if (!bSameEdits)
				{
					warnings.Add("the edits a scene gives its terrain have changed since it was saved");
					bSameGround = false;
				}

				// All of the scene's if the world had undone none of them, its new ones too
				const size_t keep = kept == sceneCount ? terrain.Edits.size() : std::min(static_cast<size_t>(kept), terrain.Edits.size());
				terrain.Edits.erase(terrain.Edits.begin() + static_cast<std::ptrdiff_t>(keep), terrain.Edits.end());
			}
			else
			{
				terrain.Edits.clear();
			}

			// Numbered on from the terrain's, the same as they were if nothing has changed
			uint64_t revision = GetTerrainRevision(terrain);
			terrain.Edits.reserve(terrain.Edits.size() + saved.size());
			for (SdfTerrainEdit& edit : saved)
			{
				edit.Revision = ++revision;
				if (!bSameGround)
				{
					edit.Baked.reset();
				}
				terrain.Edits.push_back(std::move(edit));
			}
		}
		return true;
	}

	bool ReadMotions(BinaryReader& section, std::vector<WorldSaveMotion>& outMotions, ReadWarnings& warnings)
	{
		uint32_t count = 0;
		if (!section.ReadUInt32(count))
		{
			return false;
		}
		for (uint32_t index = 0; index < count; ++index)
		{
			WorldSaveMotion motion;
			if (!ReflectedArchiveSerializer::DeserializeObject(section, &motion, Nyx::Reflection::GetTypeMetadata<WorldSaveMotion>(), warnings))
			{
				return false;
			}
			outMotions.push_back(motion);
		}
		return true;
	}
}

namespace Nyx
{
	bool TakeWorldSnapshot(const Registry& world, WorldSnapshot& outSnapshot, IAssetResolver* prefabs)
	{
		outSnapshot = WorldSnapshot{};

		std::vector<const ComponentTypeOps*> saved;
		for (const ComponentTypeOps& ops : ComponentTypeRegistry::Get().GetAll())
		{
			if (!ops.IsRuntime())
			{
				saved.push_back(&ops);
			}
		}

		bool bWritten = true;
		world.ForEachEntity([&](Entity entity)
			{
				if (!bWritten || !world.Has<GuidComponent>(entity) || !world.Get<GuidComponent>(entity).Guid.IsValid())
				{
					return;
				}

				WorldSnapshot::SavedEntity savedEntity;
				savedEntity.Guid = world.Get<GuidComponent>(entity).Guid;
				savedEntity.FirstComponent = static_cast<uint32_t>(outSnapshot.Components.size());
				for (const ComponentTypeOps* ops : saved)
				{
					const void* component = ops->GetConst(world, entity);
					if (!component)
					{
						continue;
					}
					const size_t offset = outSnapshot.Bytes.GetBytes().size();
					const bool bTerrain = ops->TypeMetadata == &Nyx::Reflection::GetTypeMetadata<SdfTerrainComponent>();
					if (bTerrain)
					{
						outSnapshot.Terrains.push_back(WorldSnapshot::Terrain{ savedEntity.Guid, *static_cast<const SdfTerrainComponent*>(component) });
					}
					const bool bValues = bTerrain ? ReflectedArchiveSerializer::SerializeProperties(outSnapshot.Bytes, component, *ops->TypeMetadata, GetTerrainPropertiesWithoutEdits())
												  : ReflectedArchiveSerializer::SerializeObject(outSnapshot.Bytes, component, *ops->TypeMetadata);
					if (!bValues)
					{
						bWritten = false;
						return;
					}
					outSnapshot.Components.push_back(WorldSnapshot::Component{ ops->TypeMetadata, offset, outSnapshot.Bytes.GetBytes().size() - offset });
					++savedEntity.ComponentCount;
				}
				outSnapshot.Entities.push_back(savedEntity);

				if (prefabs && world.Has<PrefabInstanceComponent>(entity))
				{
					const std::string& path = world.Get<PrefabInstanceComponent>(entity).Prefab.Path;
					const PrefabAsset* prefab = !path.empty() && !outSnapshot.Prefabs.contains(path) ? prefabs->ResolvePrefab(path) : nullptr;
					if (prefab && !prefab->IsEmpty())
					{
						auto taken = std::make_shared<WorldSnapshot::Prefab>();
						taken->Root = prefab->GetTemplate().Get<GuidComponent>(prefab->GetRoot()).Guid;
						bWritten = TakeWorldSnapshot(prefab->GetTemplate(), taken->Entities);
						outSnapshot.Prefabs.emplace(path, std::move(taken));
					}
				}
			});
		return bWritten;
	}

	bool MakeWorldSaveBase(const std::filesystem::path& scenePath, const Registry& world, WorldSaveBase& outBase)
	{
		outBase.ScenePath = scenePath.generic_string();
		outBase.SceneHash = HashFile(scenePath);
		return TakeWorldSnapshot(world, outBase.Entities);
	}

	bool WriteWorldSave(const WorldSaveBase& base, const WorldSnapshot& snapshot, const WorldSaveClock& clock, BinaryWriter& out)
	{
		WorldSaveHeader header;
		header.ScenePath = base.ScenePath;
		header.SceneHash = base.SceneHash;
		header.StepIndex = clock.StepIndex;
		header.WorldTimeBits = std::bit_cast<uint64_t>(clock.WorldTimeSeconds);
		header.StepSecondsBits = std::bit_cast<uint64_t>(clock.StepSeconds);
		header.SavedAtUnixSeconds = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());

		BinaryWriter headerSection;
		if (!ReflectedArchiveSerializer::SerializeObject(headerSection, &header, Nyx::Reflection::GetTypeMetadata<WorldSaveHeader>()))
		{
			return false;
		}

		std::unordered_map<EntityGuid, const WorldSnapshot::SavedEntity*> baseByGuid;
		baseByGuid.reserve(base.Entities.Entities.size());
		for (const WorldSnapshot::SavedEntity& entity : base.Entities.Entities)
		{
			baseByGuid.emplace(entity.Guid, &entity);
		}

		std::unordered_map<EntityGuid, const WorldSnapshot::SavedEntity*> byGuid;
		byGuid.reserve(snapshot.Entities.size());
		for (const WorldSnapshot::SavedEntity& entity : snapshot.Entities)
		{
			byGuid.emplace(entity.Guid, &entity);
		}

		// The instances made at run time, as what differs from their prefabs. One whose prefab the
		// snapshot hasn't got is written whole: its root before anything else made, since reading
		// it makes its parts, which their own records then take the place of.
		BinaryWriter instanceRecords;
		uint32_t instanceCount = 0;
		BinaryWriter madeRoots;
		uint32_t madeCount = 0;
		std::unordered_set<EntityGuid> inInstances;
		for (const WorldSnapshot::SavedEntity& entity : snapshot.Entities)
		{
			const WorldSnapshot::Component* mark = FindComponent<PrefabInstanceComponent>(GetComponents(snapshot, entity));
			if (!mark || baseByGuid.contains(entity.Guid))
			{
				continue;
			}

			PrefabInstanceComponent instance;
			if (!ReadValues(snapshot, *mark, instance))
			{
				return false;
			}

			BinaryWriter record;
			if (const auto prefab = snapshot.Prefabs.find(instance.Prefab.Path); prefab != snapshot.Prefabs.end())
			{
				if (!WriteInstance(snapshot, entity, *prefab->second, byGuid, record, inInstances))
				{
					return false;
				}
				instanceRecords.WriteBlock(record);
				++instanceCount;
			}
			else
			{
				record.WriteUInt64(entity.Guid.Value);
				WriteWholeEntity(snapshot, entity, record);
				madeRoots.WriteBlock(record);
				++madeCount;
				inInstances.insert(entity.Guid);
			}
		}

		BinaryWriter changedRecords;
		uint32_t changedCount = 0;
		BinaryWriter madeRecords;
		madeRecords.WriteBytes(madeRoots.GetBytes().data(), madeRoots.GetBytes().size());
		for (const WorldSnapshot::SavedEntity& entity : snapshot.Entities)
		{
			BinaryWriter record;
			const auto found = baseByGuid.find(entity.Guid);
			if (found == baseByGuid.end())
			{
				if (!inInstances.contains(entity.Guid))
				{
					record.WriteUInt64(entity.Guid.Value);
					WriteWholeEntity(snapshot, entity, record);
					madeRecords.WriteBlock(record);
					++madeCount;
				}
				continue;
			}

			bool bChanged = false;
			if (!WriteEntityChanges(base.Entities, *found->second, snapshot, entity, record, bChanged))
			{
				return false;
			}
			if (bChanged)
			{
				changedRecords.WriteBlock(record);
				++changedCount;
			}
		}

		BinaryWriter destroyedGuids;
		uint32_t destroyedCount = 0;
		for (const WorldSnapshot::SavedEntity& entity : base.Entities.Entities)
		{
			if (!byGuid.contains(entity.Guid))
			{
				destroyedGuids.WriteUInt64(entity.Guid.Value);
				++destroyedCount;
			}
		}

		BinaryWriter motionRecords;
		for (const WorldSaveMotion& motion : snapshot.Motions)
		{
			if (!ReflectedArchiveSerializer::SerializeObject(motionRecords, &motion, Nyx::Reflection::GetTypeMetadata<WorldSaveMotion>()))
			{
				return false;
			}
		}

		const auto writeCounted = [&out](std::string_view name, uint32_t count, const BinaryWriter& records)
		{
			BinaryWriter section;
			section.WriteUInt32(count);
			section.WriteBytes(records.GetBytes().data(), records.GetBytes().size());
			WriteSection(out, name, section);
		};

		out.WriteUInt32(WorldSaveMagic);
		out.WriteUInt32(WorldSaveVersion);
		WriteSection(out, HeaderSection, headerSection);
		writeCounted(DestroyedSection, destroyedCount, destroyedGuids);
		writeCounted(ChangedSection, changedCount, changedRecords);
		writeCounted(MadeSection, madeCount, madeRecords);
		writeCounted(InstancesSection, instanceCount, instanceRecords);
		writeCounted(MotionsSection, static_cast<uint32_t>(snapshot.Motions.size()), motionRecords);

		BinaryWriter terrainRecords;
		uint32_t terrainCount = 0;
		WriteTerrains(base, snapshot, terrainRecords, terrainCount);
		writeCounted(TerrainsSection, terrainCount, terrainRecords);
		return true;
	}

	bool SaveWorldToFile(const WorldSaveBase& base, const Registry& world, const WorldSaveClock& clock, const std::filesystem::path& path, IAssetResolver* prefabs)
	{
		WorldSnapshot snapshot;
		if (!TakeWorldSnapshot(world, snapshot, prefabs))
		{
			return false;
		}
		BinaryWriter out;
		return WriteWorldSave(base, snapshot, clock, out) && out.SaveToFile(path);
	}

	bool LoadWorldSave(const std::filesystem::path& path, Registry& outWorld, ScenePostLoadContext postLoadContext, WorldSaveBase& outBase,
		WorldSaveClock& outClock, std::string& outError, std::vector<WorldSaveMotion>* outMotions)
	{
		BinaryReader reader;
		if (!reader.LoadFromFile(path))
		{
			outError = "can't be read";
			return false;
		}

		uint32_t magic = 0;
		uint32_t version = 0;
		if (!reader.ReadUInt32(magic) || magic != WorldSaveMagic || !reader.ReadUInt32(version) || version == 0)
		{
			outError = "is not a save";
			return false;
		}
		if (version > WorldSaveVersion)
		{
			outError = "is a save of version " + std::to_string(version) + ", but this build only reads up to version " + std::to_string(WorldSaveVersion);
			return false;
		}

		const std::string damaged = "ends early or is damaged";
		ReadWarnings warnings;
		std::map<std::string, BinaryReader, std::less<>> sections;
		while (reader.GetRemainingSize() > 0)
		{
			std::string name;
			BinaryReader section;
			if (!reader.ReadString(name) || !reader.ReadBlock(section))
			{
				outError = damaged;
				return false;
			}
			if (name != HeaderSection && name != DestroyedSection && name != ChangedSection && name != MadeSection && name != InstancesSection && name != MotionsSection && name != TerrainsSection)
			{
				warnings.Add("skipped section '" + name + "', which this build doesn't know");
				continue;
			}
			sections.emplace(std::move(name), std::move(section));
		}

		const auto header = sections.find(HeaderSection);
		WorldSaveHeader saved;
		if (header == sections.end() ||
			!ReflectedArchiveSerializer::DeserializeObject(header->second, &saved, Nyx::Reflection::GetTypeMetadata<WorldSaveHeader>(), warnings))
		{
			outError = damaged;
			return false;
		}

		// Into a separate world, so that outWorld stays as it was if anything fails
		Registry loadedWorld;
		if (!SceneSerializer::LoadFromFile(saved.ScenePath, loadedWorld, postLoadContext))
		{
			outError = "needs the scene '" + saved.ScenePath + "', which can't be loaded";
			return false;
		}

		WorldSaveBase loadedBase;
		if (!MakeWorldSaveBase(saved.ScenePath, loadedWorld, loadedBase))
		{
			outError = "needs the scene '" + saved.ScenePath + "', which can't be compared with";
			return false;
		}
		if (loadedBase.SceneHash != saved.SceneHash)
		{
			warnings.Add("its scene '" + saved.ScenePath + "' has changed since it was saved");
		}

		const EntityGuidLookup guids = MakeEntityGuidLookup(loadedWorld);
		const auto read = [&sections](std::string_view name) -> BinaryReader*
		{
			const auto found = sections.find(name);
			return found != sections.end() ? &found->second : nullptr;
		};

		// A section a later version left out has nothing to do
		BinaryReader* destroyed = read(DestroyedSection);
		BinaryReader* changed = read(ChangedSection);
		BinaryReader* made = read(MadeSection);
		BinaryReader* instances = read(InstancesSection);
		BinaryReader* motionRecords = read(MotionsSection);
		BinaryReader* terrains = read(TerrainsSection);
		std::vector<WorldSaveMotion> motions;
		if ((destroyed && !ReadDestroyed(*destroyed, loadedWorld, guids)) ||
			(changed && !ReadChanged(*changed, loadedWorld, guids, postLoadContext, warnings)) ||
			(made && !ReadMade(*made, loadedWorld, guids, postLoadContext, warnings)) ||
			(instances && !ReadInstances(*instances, loadedWorld, guids, postLoadContext, warnings)) ||
			(motionRecords && !ReadMotions(*motionRecords, motions, warnings)) ||
			(terrains && !ReadTerrains(*terrains, loadedWorld, warnings)))
		{
			warnings.Log(path.string());
			outError = damaged;
			return false;
		}
		warnings.Log(path.string());

		// Moving keeps outWorld at its address, so pointers to it (such as the renderer's) stay valid
		outWorld = std::move(loadedWorld);
		outBase = std::move(loadedBase);
		outClock = WorldSaveClock{ saved.StepIndex, std::bit_cast<double>(saved.WorldTimeBits), std::bit_cast<double>(saved.StepSecondsBits) };
		if (outMotions)
		{
			*outMotions = std::move(motions);
		}
		return true;
	}
}
