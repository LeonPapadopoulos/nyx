#include "NewMaterial.h"

#include "Paths.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace Nyx::Editor
{
	namespace
	{
		// The template's struct, named after the material
		constexpr std::string_view TemplateName = "NyxTemplateMaterial";

		bool WriteNewFile(const std::filesystem::path& file, const std::string& text, std::string& outError)
		{
			std::error_code error;
			std::filesystem::create_directories(file.parent_path(), error);
			std::ofstream stream(file, std::ios::binary | std::ios::trunc);
			stream << text;
			if (!stream)
			{
				outError = "can't write " + file.string();
				return false;
			}
			return true;
		}
	}

	std::string MakeMaterialName(const std::string& name)
	{
		std::string result;
		for (const char c : name)
		{
			if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
			{
				result += c;
			}
		}
		if (!result.empty() && std::isdigit(static_cast<unsigned char>(result.front())))
		{
			result.insert(result.begin(), '_');
		}
		return result;
	}

	std::optional<NewMaterialFiles> CreateSurfaceMaterial(const std::filesystem::path& assetsFolder, const std::filesystem::path& templateFile, const std::string& name,
		std::string& outError)
	{
		const std::string materialName = MakeMaterialName(name);
		if (materialName.empty())
		{
			outError = "a name needs letters or digits";
			return std::nullopt;
		}
		NewMaterialFiles files;
		files.ShaderPath = "Shaders/" + materialName + ".slang";
		files.MaterialPath = "Materials/" + materialName + ".nyxmat";
		const std::filesystem::path shaderFile = (assetsFolder / files.ShaderPath).make_preferred();
		const std::filesystem::path materialFile = (assetsFolder / files.MaterialPath).make_preferred();
		for (const std::filesystem::path& file : { shaderFile, materialFile })
		{
			if (std::filesystem::exists(file))
			{
				outError = file.filename().string() + " exists already";
				return std::nullopt;
			}
		}

		std::ifstream stream(templateFile, std::ios::binary);
		std::stringstream text;
		text << stream.rdbuf();
		std::string shader = text.str();
		const size_t structName = shader.find(TemplateName);
		if (!stream || structName == std::string::npos)
		{
			outError = "the template " + templateFile.string() + " can't be read";
			return std::nullopt;
		}
		shader.replace(structName, TemplateName.size(), materialName);

		// The line after the one where shade() makes its output, where the look is filled in
		const size_t defaults = shader.find("SurfaceOutput::defaults");
		files.ShadeLine = defaults == std::string::npos ? 1 : static_cast<uint32_t>(std::count(shader.begin(), shader.begin() + static_cast<std::ptrdiff_t>(defaults), '\n')) + 2;

		if (!WriteNewFile(shaderFile, shader, outError) || !WriteNewFile(materialFile, "Shader = " + files.ShaderPath + "\n", outError))
		{
			return std::nullopt;
		}
		return files;
	}

	std::optional<NewMaterialFiles> CreateMaterialForShader(const std::filesystem::path& assetsFolder, const std::string& shaderPath, std::string& outError)
	{
		const std::string base = MakeMaterialName(std::filesystem::path(shaderPath).stem().string());
		NewMaterialFiles files;
		for (int number = 1;; ++number)
		{
			files.MaterialPath = "Materials/" + (base.empty() ? std::string("Material") : base) + (number > 1 ? std::to_string(number) : std::string()) + ".nyxmat";
			if (!std::filesystem::exists(assetsFolder / files.MaterialPath))
			{
				break;
			}
		}
		if (!WriteNewFile((assetsFolder / files.MaterialPath).make_preferred(), "Shader = " + shaderPath + "\n", outError))
		{
			return std::nullopt;
		}
		return files;
	}

	std::filesystem::path GetSurfaceTemplateFile()
	{
		return Nyx::Paths::GetShadersDir() / "Templates" / "Surface.slang";
	}
}
