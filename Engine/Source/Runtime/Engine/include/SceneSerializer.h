#pragma once

#include "SceneSerializationTypes.h"

#include <filesystem>
#include <string>

namespace Nyx::Engine
{
	class Registry;

	// Saves and loads all entities of a world, with every component type in the
	// ComponentTypeRegistry, which the Application fills at startup.
	//
	// Scene file, version 2:
	//   magic (u32) | version (u32) | entity count (u32) | entities
	//   Entity:    component count (u32) | components
	//   Component: type name (string) | property block (see ReflectedArchiveSerializer)
	class SceneSerializer
	{
	public:
		// Replaces the destination only after the complete scene has been written successfully.
		// Serialization or file I/O failures return false without overwriting the previous file.
		static bool SaveToFile(const Registry& world, const std::filesystem::path& path);

		// Replaces everything in outWorld with the scene from the file. If the file can't be loaded,
		// outWorld stays as it was. The context's asset resolver turns the asset paths stored in
		// components into loaded assets.
		// Reads versions 1 and 2. In version 2 files, component types and properties this build
		// doesn't know are skipped with a warning. After loading successfully, every entity has a
		// guid no other entity in the scene has (see GuidComponent): entities without one, or with
		// a duplicate, get a new one.
		static bool LoadFromFile(
			const std::filesystem::path& path,
			Registry& outWorld,
			ScenePostLoadContext postLoadContext = {});

		// Describes a scene file as readable text: its entities, components and properties.
		// Files of older versions are shown as converted to the current version.
		// Returns false, with the reason at the end of outText, if the file can't be printed.
		static bool PrintFile(const std::filesystem::path& path, std::string& outText);
	};
}
