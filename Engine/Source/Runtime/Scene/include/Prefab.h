#pragma once

#include "AssetReference.h"
#include "Entity.h"
#include "EntityGuid.h"
#include "ReflectionMacros.h"
#include "SceneSerializationTypes.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Nyx
{
	class BinaryWriter;
	class ReadWarnings;

	// A prefab is an entity kept as an asset with everything below it (its children, at any depth),
	// placed many times. A placed copy, an instance, keeps only which prefab it is, its own guid,
	// transform and parent, and what it changes; everything else comes from the prefab, also after
	// the prefab changes (Docs/Design/prefabs.md).
	//
	// File (.nyxprefab): PrefabFileMagic (u32) | SceneFileVersion (u32) | entity count (u32) |
	// entities as scene files store them (SceneSerializer::WriteEntity), the root first, each
	// parent before its children. Their guids are ids within the prefab, never the guids of
	// anything placed: an instance's parts get guids made from its own (GetPrefabPartGuid()).
	inline constexpr uint32_t PrefabFileMagic = 0x42465250; // 'PRFB'
	inline constexpr std::string_view PrefabExtension = ".nyxprefab";
	inline constexpr std::string_view PrefabAssetType = "Prefab";

	// Makes its entity the root of an instance of a prefab. Loading a scene gives the entity the
	// prefab root's components and makes the prefab's other entities as its parts, then reads what
	// the scene file has of it on top. The entity's guid, transform and parent are always its own.
	NYX_REFLECT(Component, meta = (DisplayName = "Prefab Instance"))
	struct PrefabInstanceComponent
	{
		NYX_PROPERTY(Serialize, ReadOnly, meta = (AssetType = Prefab, Tooltip = "The prefab this entity is placed from"))
		AssetReference Prefab;

		// The prefab root's component types this instance doesn't have. Worked out when the scene is
		// saved; kept away when it loads, also after the prefab changes.
		NYX_PROPERTY(Serialize, ReadOnly)
		std::vector<std::string> RemovedComponents;
	};

	// Marks an entity that an instance made from one of its prefab's entities, below the instance's
	// root. Its guid is GetPrefabPartGuid(Instance, Local), the same at every load, in the editor and
	// in a game.
	NYX_REFLECT(Component, meta = (DisplayName = "Prefab Part"))
	struct PrefabPartComponent
	{
		NYX_PROPERTY(Serialize, ReadOnly, meta = (Tooltip = "The instance's root"))
		EntityGuid Instance;

		// The prefab's entity it is made from
		NYX_PROPERTY(Serialize, ReadOnly, meta = (UI = Hex, Tooltip = "The id, within the prefab, of the entity this one is made from"))
		EntityGuid Local;

		// As PrefabInstanceComponent's, for this part
		NYX_PROPERTY(Serialize, ReadOnly)
		std::vector<std::string> RemovedComponents;
	};

	// The guid of the part an instance makes from the prefab's entity `local`: unique per instance,
	// the same every time, never 0
	EntityGuid GetPrefabPartGuid(EntityGuid instance, EntityGuid local);

	// A prefab as read from its file: its entities in a world of their own, the template that
	// instances are made from and compared with
	class PrefabAsset
	{
	public:
		// Writes `root` and everything below it as a prefab file, the root without its parent. False,
		// and the file left as it was, if a component can't be written, or if one of them is an
		// instance or a part (prefabs in prefabs come later).
		static bool SaveToFile(const Registry& world, Entity root, const std::filesystem::path& path);

		// Replaces what this holds with the prefab in the file. On failure, outError says why, and
		// this stays as it was.
		bool LoadFromFile(const std::filesystem::path& path, std::string& outError);

		// The same, from a file's bytes
		bool LoadFromMemory(const std::vector<std::byte>& bytes, std::string& outError);

		// The prefab as its file has it, e.g. to keep it as it was before it is changed
		std::vector<std::byte> SaveToMemory() const;

		// Writes the template as the prefab's file; false, and the file as it was, if it can't
		bool SaveToFile(const std::filesystem::path& path) const;

		// The template, to change its values, as the editor applies an instance's to it; Refresh()
		// once changed, before instances are made from it again
		Registry& GetEditableTemplate()
		{
			return *Template;
		}

		// After the template was changed: what instances are made from follows it
		void Refresh();

		bool IsEmpty() const
		{
			return Entities.empty();
		}

		// The prefab's entities, the root first, each parent before its children
		const Registry& GetTemplate() const
		{
			return *Template;
		}
		const std::vector<Entity>& GetEntities() const
		{
			return Entities;
		}
		Entity GetRoot() const
		{
			return Entities.front();
		}

		// The template's entity with this id, if the prefab has one
		std::optional<Entity> FindLocal(EntityGuid local) const;

		// An entity of the template as scene files store it, for making one like it
		const std::vector<std::byte>& GetEntityBytes(size_t index) const
		{
			return EntityBytes[index];
		}

	private:
		std::unique_ptr<Registry> Template = std::make_unique<Registry>();
		std::vector<Entity> Entities;
		std::vector<std::vector<std::byte>> EntityBytes;
		std::unordered_map<EntityGuid, Entity> ByLocal;
	};

	// Makes `root` an instance of the prefab: it gets the prefab root's components, except those it
	// has already and its PrefabInstanceComponent's RemovedComponents, and keeps its guid, transform
	// and parent; each other entity of the prefab becomes a new entity of the world below it, with its
	// guid from GetPrefabPartGuid(), its parent's guid likewise, a PrefabPartComponent and its place
	// in the world worked out from the root's. Runs the post-load steps of what it adds. outParts, if
	// given, gets the parts, each parent before its children. The root needs a guid.
	void ApplyPrefab(Registry& world, Entity root, const PrefabAsset& prefab, ScenePostLoadContext& context,
		std::vector<Entity>* outParts = nullptr);

	// For scene files: an instance's root, or one of its parts, as what differs from its prefab.
	// The root: its PrefabInstanceComponent first, then its guid, transform and parent, whole, then
	// of each of its other components the properties that differ from the prefab root's, a component
	// with none left out, and those it added whole. A part: its PrefabPartComponent first, then the
	// same, but neither its guid nor its transform, which loading makes, nor its parent's guid. False
	// if a component can't be written. outbNothing tells a part that differs in nothing, which needn't
	// be written at all.
	bool WritePrefabInstanceEntity(const Registry& world, Entity entity, const PrefabAsset& prefab, BinaryWriter& writer, bool* outbNothing = nullptr);

	// The prefab's entity an instance's entity is made from: the root's for its root, the one it
	// names for a part; none for another entity, or a part of an entity the prefab no longer has
	std::optional<Entity> FindPrefabEntityOf(const Registry& world, Entity entity, const PrefabAsset& prefab);

	// The Serialize properties of a component whose values differ from the prefab's component of its
	// type, by index: what the instance overrides. `component` and `prefabComponent` are of `type`.
	// A part's parent is never one: its guid is made, not taken from the prefab.
	std::vector<size_t> FindPrefabOverrides(const void* component, const void* prefabComponent, const Nyx::Reflection::TypeMetadata& type, bool bPart);

	// Makes the instance again from its prefab as the asset resolver of `context` has it now, keeping
	// what the instance changes from `before`, the prefab as it was: the values it overrides, the
	// components it added and took away, its guid, transform and parent. Its parts are made again,
	// with the same guids, in new entities. The prefab's new values and components reach it, ones it
	// lost go. False, and nothing changed, if a component can't be written.
	bool ReapplyPrefab(Registry& world, Entity root, const PrefabAsset& before, ScenePostLoadContext& context);

	// The prefabs that instances name, read once each from the asset folders and kept
	class PrefabLibrary
	{
	public:
		// The prefab at the path, relative to an asset folder ("Prefabs/Wall.nyxprefab"); null if it
		// can't be read, which is logged once
		PrefabAsset* Find(const std::string& path);

		// Reads the prefab again on the next Find(), e.g. after its file changed
		void Forget(const std::string& path);

		// For tests: the prefab at the path is this one, whatever the files have
		void Set(const std::string& path, std::unique_ptr<PrefabAsset> prefab);

	private:
		std::unordered_map<std::string, std::unique_ptr<PrefabAsset>> Prefabs;
	};
}

#include "Generated/Runtime/Prefab.reflect.h"
