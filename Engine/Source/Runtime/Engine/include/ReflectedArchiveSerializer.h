#pragma once

#include "BinaryArchive.h"
#include "ReflectionTypes.h"

namespace Nyx::Engine
{
	class ReflectedArchiveSerializer
	{
	public:
		static bool SerializeObject(
			BinaryWriter& writer,
			const void* object,
			const Nyx::Reflection::TypeMetadata& typeMetadata);

		static bool DeserializeObject(
			BinaryReader& reader,
			void* object,
			const Nyx::Reflection::TypeMetadata& typeMetadata);
	};
}