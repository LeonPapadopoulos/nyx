#pragma once

#include "Entity.h"
#include "SceneSerializationTypes.h"

#include <filesystem>
#include <string>

namespace Nyx
{
	class BinaryReader;
	class BinaryWriter;
	class ReadWarnings;

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

		// Replaces everything in outWorld with the scene from the file; if the file can't be loaded,
		// outWorld stays as it was. Only the current version is read, as version 1 stored values
		// without names. Afterwards every entity has a guid no other entity in the scene has.
		static bool LoadFromFile(
			const std::filesystem::path& path,
			Registry& outWorld,
			ScenePostLoadContext postLoadContext = {});

		// Describes a scene file as readable text: its entities, components and properties.
		// Returns false, with the reason at the end of outText, if the file can't be printed.
		static bool PrintFile(const std::filesystem::path& path, std::string& outText);

		// One entity, as stored in scene files and sent to the game (CreateEntity): its component
		// count and its components. Returns false if a component can't be written.
		static bool WriteEntity(const Registry& world, Entity entity, BinaryWriter& writer);

		// Reads what WriteEntity wrote into an entity of the world. Returns false if the data is cut
		// off or damaged; the entity may have some of its components then.
		static bool ReadEntity(
			BinaryReader& reader,
			Registry& world,
			Entity entity,
			ScenePostLoadContext& postLoadContext,
			ReadWarnings& warnings);

		// Prints what WriteEntity wrote, indented by two spaces per indent level. Returns false if
		// the data is cut off or damaged.
		static bool PrintEntity(BinaryReader& reader, int indentLevel, std::string& outText);
	};
}
