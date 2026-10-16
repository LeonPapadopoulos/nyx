#include "ScenePrinter.h"

#include "BinaryArchive.h"
#include "ComponentTypeRegistry.h"
#include "ReflectedArchivePrinter.h"
#include "SceneSerializer.h"

#include <string>

namespace
{
	using namespace Nyx;

	// Prints what SceneSerializer wrote after the header: the entity count, then the entities
	bool PrintEntities(BinaryReader& reader, std::string& outText)
	{
		uint32_t entityCount = 0;
		if (!reader.ReadUInt32(entityCount))
		{
			return false;
		}

		outText += std::to_string(entityCount) + " entities\n";

		for (uint32_t entityIndex = 0; entityIndex < entityCount; ++entityIndex)
		{
			outText += "\nEntity " + std::to_string(entityIndex) + "\n";

			if (!PrintSceneEntity(reader, 1, outText))
			{
				return false;
			}
		}

		return true;
	}
}

namespace Nyx
{
	bool PrintSceneEntity(BinaryReader& reader, int indentLevel, std::string& outText)
	{
		uint32_t componentCount = 0;
		if (!reader.ReadUInt32(componentCount))
		{
			return false;
		}

		const std::string indent(static_cast<size_t>(indentLevel) * 2, ' ');

		for (uint32_t componentIndex = 0; componentIndex < componentCount; ++componentIndex)
		{
			std::string componentTypeName;
			if (!reader.ReadString(componentTypeName))
			{
				return false;
			}

			// Unknown component types are printed too, with name hashes instead of property names
			const ComponentTypeOps* ops = ComponentTypeRegistry::Get().FindByName(componentTypeName);
			outText += indent + componentTypeName + (ops ? "" : " (a component type this build doesn't know)") + "\n";

			if (!ReflectedArchivePrinter::PrintObject(reader, ops ? ops->TypeMetadata : nullptr, indentLevel + 1, outText))
			{
				return false;
			}
		}

		return true;
	}

	bool PrintSceneFile(const std::filesystem::path& path, std::string& outText)
	{
		BinaryReader reader;
		if (!reader.LoadFromFile(path))
		{
			outText += "Can't open '" + path.string() + "'.\n";
			return false;
		}

		uint32_t version = 0;
		std::string error;

		if (!SceneSerializer::ReadHeader(reader, version, error))
		{
			outText += "'" + path.string() + "' " + error + ".\n";
			return false;
		}

		outText += "Scene file '" + path.string() + "'\n";
		outText += "Version " + std::to_string(version) + "\n";

		if (!PrintEntities(reader, outText))
		{
			outText += "The file ends early or is damaged here.\n";
			return false;
		}

		return true;
	}
}
