#pragma once

#include "ShaderLayout.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Nyx::Engine
{
	// A material file (.nyxmat, Design/shader-materials.md): which shader, and values for some of
	// its parameters, one per line; every parameter not listed takes the shader's default.
	//
	//   # Assets/Materials/WeatheredStone.nyxmat
	//   Shader = Shaders/Environment/Stone.slang
	//   BaseColor = 0.62 0.60 0.56
	//   Albedo = Textures/Stone_Albedo.png
	//
	// Text, so it diffs, merges and is written by hand. The lines are kept as they are: comments,
	// blank lines, and values the shader doesn't know (any more, or yet). Setting a value changes
	// its own line alone. A member of a struct field is named with a dot ("Layer.Tint").
	class MaterialAsset
	{
	public:
		// False for a line that is neither blank, a comment ('#') nor "Name = value", with its
		// number and what is wrong in `outError`; nothing is kept then
		bool Parse(std::string_view text, std::string& outError);
		std::string ToText() const;

		bool Load(const std::filesystem::path& file, std::string& outError);
		bool Save(const std::filesystem::path& file, std::string& outError) const;

		// The shader's path, relative to the assets folder ("Shader = ..."); empty without one
		std::string GetShader() const;
		void SetShader(std::string path);

		// A parameter's value as written, e.g. "0.62 0.60 0.56"; null when the file sets none
		const std::string* FindValue(std::string_view name) const;
		// Changes the value's line, or adds a line at the end
		void SetValue(std::string_view name, std::string value);
		bool RemoveValue(std::string_view name);
		// Every parameter's name and value, in the file's order; the shader isn't one
		std::vector<std::pair<std::string, std::string>> GetValues() const;

	private:
		struct Line
		{
			// Empty for a blank line or a comment, which Text holds as it is
			std::string Name;
			std::string Value;
			std::string Text;
		};
		std::vector<Line> Lines;
	};

	struct MaterialValueProblem
	{
		std::string Name;
		std::string Message;
	};

	// Writes `asset`'s values into `bytes`, which hold the shader's defaults (layout.Size of them),
	// at the layout's offsets. A texture's value is an asset path, which `textureSlot` turns into
	// a slot of the texture array (nullopt when it can't be loaded: the field shows the missing
	// texture), told whether the field is [Linear] (data, not colour); a texture the file doesn't
	// set takes the slot its Default(...) attribute names.
	// Values the layout has no field for, or that don't fit their field, are left out and listed;
	// the file keeps them, so a shader that gets the field back finds them again.
	std::vector<MaterialValueProblem> WriteMaterialValues(const ShaderLayout& layout, const MaterialAsset& asset, std::span<std::byte> bytes,
		const std::function<std::optional<uint32_t>(const std::string& path, bool bLinear)>& textureSlot);
}
