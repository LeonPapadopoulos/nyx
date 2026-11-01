#pragma once

#include "CameraProjection.h"
#include "Entity.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Nyx
{
	class PrefabAsset;
	struct ComponentTypeOps;
}

namespace Nyx::Editor
{
	class SceneEditor;

	// Where a prefab dropped on a scene view goes: where the ray meets an SDF terrain within 2 km, else
	// where it meets the ground plane (y = 0) ahead, else 10 m along it
	glm::vec3 FindPrefabDropPoint(const Nyx::Registry& world, const Nyx::Ray& ray);

	// Places an instance of the prefab at the path ("Prefabs/Lamp.nyxprefab") at `position`, turned
	// and scaled as the prefab's root is: its root and parts (ApplyPrefab()), as one undo step ("Place
	// Prefab"), the root after the selected entity in the outliner and selected. Nothing, with a
	// warning, if the editor's asset resolver (SceneEditor::Assets) has no such prefab, or in prefab
	// mode.
	std::optional<Nyx::Entity> PlacePrefab(SceneEditor& editing, const std::string& prefabPath, const glm::vec3& position);

	// Saves the entity and everything below it as a prefab at `file`, whose path in the asset folders
	// is `prefabPath`, and makes them an instance of it, as one undo step ("Create Prefab"): the entity
	// stays, its root; the entities below it are made again as its parts, with their values but guids
	// made from the root's (GetPrefabPartGuid()), so what named them by guid names nothing then.
	// False, and nothing changed, if one of them is an instance or part already, if the file can't be
	// written or read back, or in prefab mode.
	bool CreatePrefab(SceneEditor& editing, Nyx::Entity entity, const std::filesystem::path& file, const std::string& prefabPath);

	// The root of the instance the entity is the root or a part of, if it is either and the root is there
	std::optional<Nyx::Entity> FindPrefabInstanceRoot(const Nyx::Registry& world, Nyx::Entity entity);

	// Makes the instance the entity is the root or a part of plain entities, keeping all they have
	// but the marks of the instance and its parts, as one undo step ("Unpack Prefab"); false if it
	// is no instance
	bool UnpackPrefab(SceneEditor& editing, Nyx::Entity entity);

	// The prefab an instance's root or part follows, and the entity of it that it is made from
	struct PrefabSource
	{
		Nyx::PrefabAsset* Prefab = nullptr;
		std::string Path;
		Nyx::Entity Entity{};
		bool bPart = false;
	};

	// For an instance's root or part whose prefab is there and has its entity
	std::optional<PrefabSource> FindPrefabSource(SceneEditor& editing, Nyx::Entity entity);

	// Whether an instance's component follows its prefab at all: not the guid, the marks of instances
	// and parts, nor the root's transform and parent, which are always its own
	bool FollowsPrefab(const Nyx::Registry& world, Nyx::Entity entity, const Nyx::ComponentTypeOps& componentType);

	// The properties of the entity's component that differ from its prefab's: its overrides. Empty
	// for an entity that is no instance's, and for a component that doesn't follow its prefab or that
	// the prefab hasn't got.
	std::vector<size_t> FindOverriddenProperties(SceneEditor& editing, Nyx::Entity entity, const Nyx::ComponentTypeOps& componentType);

	// Whether the instance added the component itself: its prefab hasn't got it
	bool IsAddedToInstance(SceneEditor& editing, Nyx::Entity entity, const Nyx::ComponentTypeOps& componentType);

	// Gives the instance's component the prefab's value of the property, or of every property it
	// overrides, as one undo step ("Revert to Prefab"); false if there is nothing to revert
	bool RevertToPrefab(SceneEditor& editing, Nyx::Entity entity, const Nyx::ComponentTypeOps& componentType, std::optional<size_t> propertyIndex = std::nullopt);

	// Gives the prefab the instance's value of the property, or of every property it overrides, or
	// for a component the instance added, the component, as one undo step ("Apply to Prefab"). The
	// prefab is saved and its other instances follow (PrefabSubscriber); undo takes the prefab back
	// and leaves this instance its value. False if there is nothing to apply.
	bool ApplyToPrefab(SceneEditor& editing, Nyx::Entity entity, const Nyx::ComponentTypeOps& componentType, std::optional<size_t> propertyIndex = std::nullopt);

	// The path of a file in the asset folders, as assets name each other ("Prefabs/Lamp.nyxprefab");
	// none for a file outside them
	std::optional<std::string> GetAssetPathOf(const std::filesystem::path& file);
}
