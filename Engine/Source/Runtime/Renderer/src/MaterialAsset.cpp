#include "MaterialAsset.h"

#include "BindlessTextures.h"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace Nyx::Engine
{
	namespace
	{
		constexpr std::string_view ShaderKey = "Shader";

		std::string_view Trim(std::string_view text)
		{
			const auto isSpace = [](char c)
			{
				return c == ' ' || c == '\t' || c == '\r';
			};
			while (!text.empty() && isSpace(text.front()))
			{
				text.remove_prefix(1);
			}
			while (!text.empty() && isSpace(text.back()))
			{
				text.remove_suffix(1);
			}
			return text;
		}

		// "0.62 0.60, 0.56": the numbers, apart by spaces or commas
		std::vector<std::string_view> SplitComponents(std::string_view text)
		{
			std::vector<std::string_view> parts;
			size_t start = std::string_view::npos;
			for (size_t index = 0; index <= text.size(); ++index)
			{
				const bool bSeparator = index == text.size() || text[index] == ' ' || text[index] == '\t' || text[index] == ',';
				if (bSeparator && start != std::string_view::npos)
				{
					parts.push_back(text.substr(start, index - start));
					start = std::string_view::npos;
				}
				else if (!bSeparator && start == std::string_view::npos)
				{
					start = index;
				}
			}
			return parts;
		}

		// One component as the 4 bytes the shader reads; nullopt when it isn't of the type
		std::optional<uint32_t> ParseComponent(std::string_view text, EShaderScalarType type)
		{
			const char* first = text.data();
			const char* last = text.data() + text.size();
			uint32_t word = 0;
			switch (type)
			{
			case EShaderScalarType::Float:
			{
				float value = 0.0f;
				const auto [end, error] = std::from_chars(first, last, value);
				if (error != std::errc() || end != last)
				{
					return std::nullopt;
				}
				std::memcpy(&word, &value, sizeof(word));
				return word;
			}
			case EShaderScalarType::Int:
			{
				int32_t value = 0;
				const auto [end, error] = std::from_chars(first, last, value);
				if (error != std::errc() || end != last)
				{
					return std::nullopt;
				}
				std::memcpy(&word, &value, sizeof(word));
				return word;
			}
			case EShaderScalarType::UInt:
			{
				const auto [end, error] = std::from_chars(first, last, word);
				if (error != std::errc() || end != last)
				{
					return std::nullopt;
				}
				return word;
			}
			case EShaderScalarType::Bool:
				if (text == "true" || text == "1")
				{
					return 1u;
				}
				if (text == "false" || text == "0")
				{
					return 0u;
				}
				return std::nullopt;
			default:
				return std::nullopt;
			}
		}

		const char* TypeText(EShaderScalarType type)
		{
			switch (type)
			{
			case EShaderScalarType::Float:
				return "numbers";
			case EShaderScalarType::Int:
				return "whole numbers";
			case EShaderScalarType::UInt:
				return "whole numbers from 0";
			case EShaderScalarType::Bool:
				return "true or false";
			default:
				return "values";
			}
		}

		class ValueWriter
		{
		public:
			ValueWriter(const MaterialAsset& asset, std::span<std::byte> bytes, const std::function<std::optional<uint32_t>(const std::string&, bool)>& textureSlot)
				: Bytes(bytes)
				, TextureSlot(textureSlot)
			{
				for (auto& [name, value] : asset.GetValues())
				{
					Values.emplace(name, value);
				}
			}

			void WriteFields(const std::vector<ShaderField>& fields, const std::string& prefix, uint32_t base)
			{
				for (const ShaderField& field : fields)
				{
					const std::string name = prefix + field.Name;
					const uint32_t offset = base + field.Offset;
					if (field.Kind == EShaderFieldKind::Struct)
					{
						WriteFields(field.Fields, name + ".", offset);
						continue;
					}

					const auto found = Values.find(name);
					const std::string* value = found != Values.end() ? &found->second : nullptr;
					if (value)
					{
						Used.insert(name);
					}

					if (field.Kind == EShaderFieldKind::Texture)
					{
						WriteTexture(field, name, offset, value);
					}
					else if (value && (field.Kind == EShaderFieldKind::Scalar || field.Kind == EShaderFieldKind::Vector) && field.ArrayCount == 0)
					{
						WriteComponents(field, name, offset, *value);
					}
					else if (value)
					{
						Problems.push_back({ name, "a parameter of type " + field.TypeName + " can't be set from a material file; the file keeps the value" });
					}
				}
			}

			void ListUnused()
			{
				for (const auto& [name, value] : Values)
				{
					if (!Used.contains(name))
					{
						Problems.push_back({ name, "the shader has no parameter of that name; the file keeps the value" });
					}
				}
			}

			std::vector<MaterialValueProblem> Problems;

		private:
			void WriteWord(uint32_t offset, uint32_t word)
			{
				if (offset + sizeof(word) <= Bytes.size())
				{
					std::memcpy(Bytes.data() + offset, &word, sizeof(word));
				}
			}

			void WriteTexture(const ShaderField& field, const std::string& name, uint32_t offset, const std::string* value)
			{
				if (value && !value->empty())
				{
					const std::optional<uint32_t> slot = TextureSlot ? TextureSlot(*value, field.FindAttribute("Linear") != nullptr) : std::nullopt;
					if (!slot)
					{
						Problems.push_back({ name, "the texture " + *value + " can't be loaded; the field shows the missing texture" });
					}
					WriteWord(offset, slot.value_or(static_cast<uint32_t>(EBindlessTexture::Missing)));
					return;
				}
				if (const ShaderAttribute* attribute = field.FindAttribute("Default"); attribute && !attribute->Arguments.empty())
				{
					const std::optional<EBindlessTexture> slot = FindBindlessTexture(attribute->Arguments[0].Text);
					if (slot)
					{
						WriteWord(offset, static_cast<uint32_t>(*slot));
					}
				}
			}

			void WriteComponents(const ShaderField& field, const std::string& name, uint32_t offset, const std::string& value)
			{
				const std::vector<std::string_view> parts = SplitComponents(value);
				const uint32_t count = field.Kind == EShaderFieldKind::Vector ? field.Columns : 1;
				std::vector<uint32_t> words;
				for (std::string_view part : parts)
				{
					const std::optional<uint32_t> word = ParseComponent(part, field.Scalar);
					if (!word)
					{
						break;
					}
					words.push_back(*word);
				}
				if (parts.size() != count || words.size() != count)
				{
					Problems.push_back({ name, "expects " + std::to_string(count) + " " + TypeText(field.Scalar) + ", not \"" + value + "\"; the file keeps the value" });
					return;
				}
				for (uint32_t index = 0; index < count; ++index)
				{
					WriteWord(offset + index * 4, words[index]);
				}
			}

			std::span<std::byte> Bytes;
			const std::function<std::optional<uint32_t>(const std::string&, bool)>& TextureSlot;
			std::map<std::string, std::string> Values;
			std::set<std::string> Used;
		};
	}

	bool MaterialAsset::Parse(std::string_view text, std::string& outError)
	{
		std::vector<Line> lines;
		size_t number = 0;
		while (!text.empty())
		{
			++number;
			const size_t end = text.find('\n');
			std::string_view raw = text.substr(0, end);
			text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
			if (!raw.empty() && raw.back() == '\r')
			{
				raw.remove_suffix(1);
			}

			const std::string_view trimmed = Trim(raw);
			if (trimmed.empty() || trimmed.front() == '#')
			{
				lines.push_back(Line{ .Text = std::string(raw) });
			}
			else
			{
				const size_t equals = trimmed.find('=');
				const std::string_view name = equals == std::string_view::npos ? std::string_view() : Trim(trimmed.substr(0, equals));
				if (name.empty())
				{
					outError = "line " + std::to_string(number) + ": expected \"Name = value\", found \"" + std::string(trimmed) + "\"";
					return false;
				}
				lines.push_back(Line{ .Name = std::string(name), .Value = std::string(Trim(trimmed.substr(equals + 1))), .Text = std::string(raw) });
			}
			if (end == std::string_view::npos)
			{
				break;
			}
		}
		Lines = std::move(lines);
		return true;
	}

	std::string MaterialAsset::ToText() const
	{
		std::string text;
		for (const Line& line : Lines)
		{
			text += line.Name.empty() || !line.Text.empty() ? line.Text : line.Name + " = " + line.Value;
			text += '\n';
		}
		return text;
	}

	bool MaterialAsset::Load(const std::filesystem::path& file, std::string& outError)
	{
		std::ifstream stream(file, std::ios::binary);
		if (!stream)
		{
			outError = "can't open " + file.string();
			return false;
		}
		std::stringstream text;
		text << stream.rdbuf();
		if (!Parse(text.str(), outError))
		{
			outError = file.string() + ", " + outError;
			return false;
		}
		return true;
	}

	bool MaterialAsset::Save(const std::filesystem::path& file, std::string& outError) const
	{
		std::ofstream stream(file, std::ios::binary | std::ios::trunc);
		stream << ToText();
		if (!stream)
		{
			outError = "can't write " + file.string();
			return false;
		}
		return true;
	}

	std::string MaterialAsset::GetShader() const
	{
		const std::string* shader = FindValue(ShaderKey);
		return shader ? *shader : std::string();
	}

	void MaterialAsset::SetShader(std::string path)
	{
		SetValue(ShaderKey, std::move(path));
	}

	const std::string* MaterialAsset::FindValue(std::string_view name) const
	{
		for (const Line& line : Lines)
		{
			if (!line.Name.empty() && line.Name == name)
			{
				return &line.Value;
			}
		}
		return nullptr;
	}

	void MaterialAsset::SetValue(std::string_view name, std::string value)
	{
		for (Line& line : Lines)
		{
			if (!line.Name.empty() && line.Name == name)
			{
				line.Value = std::move(value);
				line.Text.clear();
				return;
			}
		}
		Lines.push_back(Line{ .Name = std::string(name), .Value = std::move(value) });
	}

	bool MaterialAsset::RemoveValue(std::string_view name)
	{
		const auto found = std::find_if(Lines.begin(), Lines.end(), [name](const Line& line)
			{
				return !line.Name.empty() && line.Name == name;
			});
		if (found == Lines.end())
		{
			return false;
		}
		Lines.erase(found);
		return true;
	}

	std::vector<std::pair<std::string, std::string>> MaterialAsset::GetValues() const
	{
		std::vector<std::pair<std::string, std::string>> values;
		for (const Line& line : Lines)
		{
			if (!line.Name.empty() && line.Name != ShaderKey)
			{
				values.emplace_back(line.Name, line.Value);
			}
		}
		return values;
	}

	std::vector<MaterialValueProblem> WriteMaterialValues(const ShaderLayout& layout, const MaterialAsset& asset, std::span<std::byte> bytes,
		const std::function<std::optional<uint32_t>(const std::string& path, bool bLinear)>& textureSlot)
	{
		ValueWriter writer(asset, bytes, textureSlot);
		writer.WriteFields(layout.Fields, "", 0);
		writer.ListUnused();
		return std::move(writer.Problems);
	}
}
