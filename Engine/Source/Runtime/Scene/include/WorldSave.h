#pragma once

#include "BinaryArchive.h"
#include "EntityGuid.h"
#include "Entity.h"
#include "ReflectionMacros.h"
#include "SceneSerializationTypes.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Nyx
{
	namespace Reflection
	{
		struct TypeMetadata;
	}

	// A save game: a running world as what differs from the scene it started from, its entities named
	// by guid. Loading reads the scene and puts the save on top, so a fix to the scene reaches old
	// saves wherever the world didn't change the same value. An entity without a guid isn't saved:
	// whatever made it, such as a system's helper, makes it again.
	//
	// File (.nyxsave): WorldSaveMagic (u32) | WorldSaveVersion (u32) | sections, up to the file's end
	//   Section:   name (string) | byte size (u32, not counting itself) | its bytes. A section this
	//              build doesn't know is skipped.
	//   Header:    a WorldSaveHeader as a property block (ReflectedArchiveSerializer)
	//   Destroyed: count (u32) | guids (u64): the scene's entities the world no longer has
	//   Changed:   count (u32) | records, each with its size in front (BinaryWriter::WriteBlock()):
	//              guid (u64) | count (u32) and names of the components taken away | an entity as
	//              SceneSerializer::WriteEntity() stores it, with what differs from the scene: of a
	//              component the scene's entity has, the properties that differ; one it hasn't, whole
	//   Made:      count (u32) | records, each with its size in front: guid (u64) | an entity as
	//              SceneSerializer::WriteEntity() stores it, whole: those the scene hasn't got
	inline constexpr uint32_t WorldSaveMagic = 0x56534E4E; // 'NNSV'
	inline constexpr uint32_t WorldSaveVersion = 1;
	inline constexpr std::string_view WorldSaveExtension = ".nyxsave";

	// Where a world's time is, from its clock (FixedStepClock): what a save keeps, and what a load
	// hands back to set it to (FixedStepClock::SetStepSeconds(), SetTime())
	struct WorldSaveClock
	{
		uint64_t StepIndex = 0;
		double WorldTimeSeconds = 0.0;
		double StepSeconds = 0.0;
	};

	// What a save says about itself. The two doubles are kept as their bits, to come back exactly.
	NYX_REFLECT()
	struct WorldSaveHeader
	{
		// The scene it is a difference from, as it was given when the world started
		NYX_PROPERTY(Serialize)
		std::string ScenePath;

		// A hash of the scene's file then: another one when it loads says the scene changed since
		NYX_PROPERTY(Serialize)
		uint64_t SceneHash = 0;

		NYX_PROPERTY(Serialize)
		uint64_t StepIndex = 0;

		NYX_PROPERTY(Serialize)
		uint64_t WorldTimeBits = 0;

		NYX_PROPERTY(Serialize)
		uint64_t StepSecondsBits = 0;

		// When it was saved, in real time: seconds since 1970, UTC. For a list of saves; the world's
		// time doesn't move on with it.
		NYX_PROPERTY(Serialize)
		uint64_t SavedAtUnixSeconds = 0;
	};

	// The saved values of a world's entities at one moment, as bytes: all a save is made from, so it
	// can be made on a worker while the world runs on. What is taken from the world (the entities
	// with guids, their components but the run-time types) in the order the world and the component
	// types have them.
	struct WorldSnapshot
	{
		struct Component
		{
			const Nyx::Reflection::TypeMetadata* Type = nullptr;
			// Its property block's place in Bytes
			size_t Offset = 0;
			size_t Size = 0;
		};

		struct SavedEntity
		{
			EntityGuid Guid;
			uint32_t FirstComponent = 0;
			uint32_t ComponentCount = 0;
		};

		std::vector<SavedEntity> Entities;
		std::vector<Component> Components;
		BinaryWriter Bytes;
	};

	// Takes the world's values, on the thread that runs the world and between its steps. Returns
	// false if a component can't be written.
	bool TakeWorldSnapshot(const Registry& world, WorldSnapshot& outSnapshot);

	// What a world's save is a difference from: its scene, and its entities as they were when it was
	// just loaded, children placed by their parents and prefab instances made
	struct WorldSaveBase
	{
		std::string ScenePath;
		uint64_t SceneHash = 0;
		WorldSnapshot Entities;
	};

	// For a world that just loaded this scene, before its first step. False if a component can't be
	// written.
	bool MakeWorldSaveBase(const std::filesystem::path& scenePath, const Registry& world, WorldSaveBase& outBase);

	// Writes the file's bytes from a snapshot of the world, on any thread: it touches no world. False
	// if the snapshot or the base aren't whole.
	bool WriteWorldSave(const WorldSaveBase& base, const WorldSnapshot& snapshot, const WorldSaveClock& clock, BinaryWriter& out);

	// All of it at once: the snapshot, the bytes, and the file, written whole or not at all. Between
	// the world's steps. False, and a file already there left as it was, if it can't.
	bool SaveWorldToFile(const WorldSaveBase& base, const Registry& world, const WorldSaveClock& clock, const std::filesystem::path& path);

	// Replaces everything in outWorld with the scene the save names and the save on top. Call it with
	// a world no step has run in yet: the systems' and the bodies' state are never saved, so they must
	// be new. Children stay where the save has them, not placed by their parents again: one that
	// doesn't follow its parent may have been left behind. Read as scenes are: properties and component
	// types this build doesn't know are skipped, and what the save has of an entity the scene no longer
	// has is dropped, with warnings. On failure, outError says why and outWorld stays as it was: not a
	// save, one of a later version, not whole, or its scene not there. outBase gets what the next save
	// of this world is a difference from, outClock where its time was.
	bool LoadWorldSave(const std::filesystem::path& path, Registry& outWorld, ScenePostLoadContext postLoadContext, WorldSaveBase& outBase,
		WorldSaveClock& outClock, std::string& outError);
}

#include "Generated/Runtime/WorldSave.reflect.h"
