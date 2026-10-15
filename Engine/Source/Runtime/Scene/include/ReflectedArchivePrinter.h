#pragma once

#include "BinaryArchive.h"
#include "PropertyValue.h"
#include "ReflectionTypes.h"

#include <string>

namespace Nyx::Engine
{
	// Turns data written by ReflectedArchiveSerializer into readable text, for NyxDump and
	// later the editor's message log.
	class ReflectedArchivePrinter
	{
	public:
		// Prints one property block, one line per property, indented by two spaces per level.
		// With a type, properties are shown by name. Without one (e.g. a component type this
		// build doesn't know), or for properties the type doesn't have, they are shown by name
		// hash and kind. Returns false if the data is cut off or damaged.
		static bool PrintObject(
			BinaryReader& reader,
			const Nyx::Reflection::TypeMetadata* typeMetadata,
			int indentLevel,
			std::string& outText);

		// A value as text, for example: true, 1.5, (0, 2, 6) or "Main Camera"
		static std::string ValueToText(const Nyx::Reflection::PropertyValue& value);
	};
}
