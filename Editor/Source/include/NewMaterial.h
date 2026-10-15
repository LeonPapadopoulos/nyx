#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace Nyx::Editor
{
	// What New Material made: paths relative to the assets folder, as materials name them
	struct NewMaterialFiles
	{
		std::string ShaderPath;   // "Shaders/Stone.slang"; empty when the material uses a shader that was there
		std::string MaterialPath; // "Materials/Stone.nyxmat"
		// The shader's line where shade() starts filling in its output, to open the shader at
		uint32_t ShadeLine = 0;
	};

	// The name as a shader's struct may have it: letters, digits and underscores, not starting
	// with a digit; empty when nothing of it is left
	std::string MakeMaterialName(const std::string& name);

	// A surface shader and a material on it, both called `name`: Shaders/<Name>.slang from the
	// template (Engine/Shaders/Templates/Surface.slang), its struct named after it, and
	// Materials/<Name>.nyxmat. Nothing is made, and `outError` says why, when the name has
	// nothing usable or either file exists.
	std::optional<NewMaterialFiles> CreateSurfaceMaterial(const std::filesystem::path& assetsFolder, const std::filesystem::path& templateFile, const std::string& name,
		std::string& outError);

	// A material on a shader that is there ("Shaders/Stone.slang"): Materials/<shader's name>.nyxmat,
	// or <name>2, 3, ... when that exists. Nothing is made when it can't be written.
	std::optional<NewMaterialFiles> CreateMaterialForShader(const std::filesystem::path& assetsFolder, const std::string& shaderPath, std::string& outError);

	// An instance of a material ("Materials/Stone.nyxmat"): Materials/<its name>Instance.nyxmat,
	// numbered when that exists, with only its parent; it changes nothing until values are set
	std::optional<NewMaterialFiles> CreateMaterialInstance(const std::filesystem::path& assetsFolder, const std::string& parentPath, std::string& outError);

	// The template's file in the project: Engine/Shaders/Templates/Surface.slang
	std::filesystem::path GetSurfaceTemplateFile();
}
