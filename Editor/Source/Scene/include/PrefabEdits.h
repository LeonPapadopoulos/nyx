#pragma once

#include "CameraProjection.h"
#include "Entity.h"

#include <filesystem>
#include <optional>
#include <string>

namespace Nyx::Editor
{
	class SceneEditor;

	// Where a prefab dropped on a scene view goes: where the ray meets an SDF terrain within 2 km, else
	// where it meets the ground plane (y = 0) ahead, else 10 m along it
	glm::vec3 FindPrefabDropPoint(const Nyx::Registry& world, const Nyx::Ray& ray);

	// Places an instance of the prefab at the path ("Prefabs/Lamp.nyxprefab") at `position`, turned
	// and scaled as the prefab's root is: its root and parts (ApplyPrefab()), as one undo step ("Place
	// Prefab"), the root after the selected entity in the outliner and selected. Nothing, with a
	// warning, if the editor's asset resolver (SceneEditor::Assets) has no such prefab.
	std::optional<Nyx::Entity> PlacePrefab(SceneEditor& editing, const std::string& prefabPath, const glm::vec3& position);

	// Saves the entity and everything below it as a prefab at `file`, whose path in the asset folders
	// is `prefabPath`, and makes them an instance of it, as one undo step ("Create Prefab"): the entity
	// stays, its root; the entities below it are made again as its parts, with their values but guids
	// made from the root's (GetPrefabPartGuid()), so what named them by guid names nothing then.
	// False, and nothing changed, if one of them is an instance or part already, or the file can't be
	// written or read back.
	bool CreatePrefab(SceneEditor& editing, Nyx::Entity entity, const std::filesystem::path& file, const std::string& prefabPath);

	// The root of the instance the entity is the root or a part of, if it is either and the root is there
	std::optional<Nyx::Entity> FindPrefabInstanceRoot(const Nyx::Registry& world, Nyx::Entity entity);

	// Makes the instance the entity is the root or a part of plain entities, keeping all they have
	// but the marks of the instance and its parts, as one undo step ("Unpack Prefab"); false if it
	// is no instance
	bool UnpackPrefab(SceneEditor& editing, Nyx::Entity entity);

	// The path of a file in the asset folders, as assets name each other ("Prefabs/Lamp.nyxprefab");
	// none for a file outside them
	std::optional<std::string> GetAssetPathOf(const std::filesystem::path& file);
}
