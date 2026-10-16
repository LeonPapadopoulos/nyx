#pragma once

#include <filesystem>
#include <string>

namespace Nyx
{
	class BinaryReader;

	// Describes a scene file as readable text: its entities, components and properties.
	// Returns false, with the reason at the end of outText, if the file can't be printed.
	bool PrintSceneFile(const std::filesystem::path& path, std::string& outText);

	// Prints what SceneSerializer::WriteEntity wrote, indented by two spaces per indent level.
	// Returns false if the data is cut off or damaged.
	bool PrintSceneEntity(BinaryReader& reader, int indentLevel, std::string& outText);
}
