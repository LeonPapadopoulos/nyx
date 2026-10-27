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
	struct SceneOutline;

	// Saves and loads all entities of a world, with every component type in the
	// ComponentTypeRegistry, which the Application fills at startup.
	//
	// Scene file, version 2:
	//   magic (u32) | version (u32) | entity count (u32) | entities
	//   Entity:    component count (u32) | components
	//   Component: type name (string) | property block (see ReflectedArchiveSerializer)
	//   Outline:   optional, after the entities: SceneOutlineMarker (u32) | the SceneOutline as a
	//              property block. The editor's alone; scenes saved before it ended after their
	//              entities, and builds from before it stop reading there too.
	class SceneSerializer
	{
	public:
		// Reads a scene file's magic and version, which must be this build's. On failure, outError
		// says why, e.g. "is not a scene file".
		static bool ReadHeader(BinaryReader& reader, uint32_t& outVersion, std::string& outError);

		// Replaces the destination only after the complete scene has been written successfully.
		// Serialization or file I/O failures return false without overwriting the previous file.
		// With an outline, the editor's outliner (SceneOutline.h), saves it after the entities.
		static bool SaveToFile(const Registry& world, const std::filesystem::path& path, const SceneOutline* outline = nullptr);

		// Replaces everything in outWorld with the scene from the file; if the file can't be loaded,
		// outWorld stays as it was. Only the current version is read, as version 1 stored values
		// without names. Afterwards every entity has a guid no other entity in the scene has.
		// outOutline, if given, gets the scene's outline, empty if it has none; a game passes none,
		// and the outline is skipped.
		static bool LoadFromFile(
			const std::filesystem::path& path,
			Registry& outWorld,
			ScenePostLoadContext postLoadContext = {},
			SceneOutline* outOutline = nullptr);

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
	};
}
