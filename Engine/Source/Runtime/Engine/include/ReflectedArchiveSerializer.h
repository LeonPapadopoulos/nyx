#pragma once

#include "BinaryArchive.h"
#include "PropertyValue.h"
#include "ReflectionTypes.h"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace Nyx::Engine
{
	// Problems found while reading data, collected so that each one is logged once
	// instead of once per entity.
	class ReadWarnings
	{
	public:
		void Add(const std::string& message);

		// Logs every collected warning once, with how often it happened.
		void Log(std::string_view source) const;

	private:
		std::map<std::string, uint32_t> CountByMessage;
	};

	// Writes and reads reflected objects in Nyx's tagged binary format. Scene files use it,
	// and so will the messages between the editor and the game. All numbers are little-endian.
	//
	//   Property block: byte size (u32, not counting itself) | property count (u16) | properties
	//   Property:       name hash (u32) | kind (u8) | value
	//   Value:          Bool = 1 byte; Int32, UInt32 and Float = 4 bytes; Vec2 = 8; Vec3 = 12;
	//                   Vec4 and Quat (w, x, y, z) = 16; String = length (u32) + bytes;
	//                   Struct = a property block
	//
	// Every property carries its name hash and kind, and every block its size, so data stays
	// readable after properties are added, removed, reordered or change their type.
	class ReflectedArchiveSerializer
	{
	public:
		// Writes the object's Serialize properties as one property block.
		// Returns false if a property has a kind that can't be written.
		static bool SerializeObject(
			BinaryWriter& writer,
			const void* object,
			const Nyx::Reflection::TypeMetadata& typeMetadata);

		// Reads one property block into the object:
		// - Properties the data doesn't have keep their current value (in a new object: the default).
		// - Number kinds (Int32, UInt32, Float) are converted into each other.
		// - Properties the type doesn't have or doesn't save, and other changes of kind, are skipped.
		// - A kind this build doesn't know (from a newer build) ends reading of the block, because
		//   its size is unknown. The properties after it keep their current value.
		// Everything that didn't fit is added to the warnings. Returns false only if the data
		// is cut off or damaged.
		static bool DeserializeObject(
			BinaryReader& reader,
			void* object,
			const Nyx::Reflection::TypeMetadata& typeMetadata,
			ReadWarnings& warnings);

		// Skips one property block, for example of a component type this build doesn't know.
		static bool SkipObject(BinaryReader& reader);

		// Reads data from before the tagged format (scene files of version 1): the values of the
		// Serialize properties in declaration order, without names, kinds or sizes.
		static bool DeserializeUntaggedObject(
			BinaryReader& reader,
			void* object,
			const Nyx::Reflection::TypeMetadata& typeMetadata);

		// One value of any kind except Struct, without its kind in front.
		static void WriteValue(BinaryWriter& writer, const Nyx::Reflection::PropertyValue& value);
		static bool ReadValue(
			BinaryReader& reader,
			Nyx::Reflection::EPropertyKind kind,
			Nyx::Reflection::PropertyValue& outValue);
	};
}