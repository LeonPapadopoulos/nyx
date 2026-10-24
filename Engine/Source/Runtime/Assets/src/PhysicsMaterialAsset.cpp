#include "PhysicsMaterialAsset.h"

#include "MaterialAsset.h"
#include "StringUtil.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <sstream>

namespace Nyx
{
	namespace
	{
		constexpr std::string_view FrictionKey = "Friction";
		constexpr std::string_view RestitutionKey = "Restitution";
		constexpr std::string_view DensityKey = "Density";

		std::string FormatNumber(float value)
		{
			return FormatMaterialNumbers(std::span<const float>(&value, 1));
		}

		// One number, as a material file writes it; nullopt for anything else
		std::optional<float> ParseNumber(std::string_view text)
		{
			const std::optional<std::vector<double>> numbers = ParseMaterialNumbers(text);
			if (!numbers || numbers->size() != 1)
			{
				return std::nullopt;
			}
			return static_cast<float>(numbers->front());
		}
	}

	float GetExtra(const PhysicsMaterialData& material, std::string_view name, float fallback)
	{
		const auto found = std::find_if(material.Extras.begin(), material.Extras.end(), [name](const PhysicsMaterialExtra& extra)
			{
				return extra.Name == name;
			});
		return found != material.Extras.end() ? found->Value : fallback;
	}

	bool IsPhysicsMaterialExtraName(std::string_view name)
	{
		return !name.empty() && std::all_of(name.begin(), name.end(), [](char c)
			{
				return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
			});
	}

	bool PhysicsMaterialAsset::Parse(std::string_view text, std::string& outError)
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

	std::string PhysicsMaterialAsset::ToText() const
	{
		std::string text;
		for (const Line& line : Lines)
		{
			text += line.Name.empty() || !line.Text.empty() ? line.Text : line.Name + " = " + line.Value;
			text += '\n';
		}
		return text;
	}

	bool PhysicsMaterialAsset::Load(const std::filesystem::path& file, std::string& outError)
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

	bool PhysicsMaterialAsset::Save(const std::filesystem::path& file, std::string& outError) const
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

	PhysicsMaterialData PhysicsMaterialAsset::Read(std::vector<std::string>* outProblems) const
	{
		PhysicsMaterialData data;
		for (const Line& line : Lines)
		{
			if (line.Name.empty())
			{
				continue;
			}
			const bool bExtra = line.Name.starts_with(ExtraPrefix);
			const bool bKnown = bExtra || line.Name == FrictionKey || line.Name == RestitutionKey || line.Name == DensityKey;
			if (!bKnown)
			{
				// Kept, for a later Nyx or the game's own tools
				continue;
			}
			const std::optional<float> value = ParseNumber(line.Value);
			if (!value)
			{
				if (outProblems)
				{
					outProblems->push_back(line.Name + ": \"" + line.Value + "\" isn't a number");
				}
				continue;
			}

			if (line.Name == FrictionKey)
			{
				data.Friction = *value;
			}
			else if (line.Name == RestitutionKey)
			{
				data.Restitution = *value;
			}
			else if (line.Name == DensityKey)
			{
				data.Density = *value;
			}
			else
			{
				const std::string name = line.Name.substr(ExtraPrefix.size());
				const auto found = std::find_if(data.Extras.begin(), data.Extras.end(), [&name](const PhysicsMaterialExtra& extra)
					{
						return extra.Name == name;
					});
				if (found != data.Extras.end())
				{
					found->Value = *value;
				}
				else
				{
					data.Extras.push_back(PhysicsMaterialExtra{ .Name = name, .Value = *value });
				}
			}
		}
		return data;
	}

	void PhysicsMaterialAsset::Write(const PhysicsMaterialData& data)
	{
		// What the lines say now: a value that reads the same keeps its line as written
		const PhysicsMaterialData current = Read();
		const auto writeValue = [this](std::string_view name, float value, float now)
		{
			if (value != now)
			{
				SetValue(name, FormatNumber(value));
			}
		};
		writeValue(FrictionKey, data.Friction, current.Friction);
		writeValue(RestitutionKey, data.Restitution, current.Restitution);
		writeValue(DensityKey, data.Density, current.Density);

		// Extras gone from the data lose their lines
		Lines.erase(std::remove_if(Lines.begin(), Lines.end(),
						[&data](const Line& line)
						{
							if (!line.Name.starts_with(ExtraPrefix))
							{
								return false;
							}
							const std::string_view name = std::string_view(line.Name).substr(ExtraPrefix.size());
							return std::none_of(data.Extras.begin(), data.Extras.end(), [name](const PhysicsMaterialExtra& extra)
								{
									return extra.Name == name;
								});
						}),
			Lines.end());

		for (const PhysicsMaterialExtra& extra : data.Extras)
		{
			const bool bWritten = std::any_of(current.Extras.begin(), current.Extras.end(), [&extra](const PhysicsMaterialExtra& now)
				{
					return now.Name == extra.Name && now.Value == extra.Value;
				});
			if (bWritten || !IsPhysicsMaterialExtraName(extra.Name))
			{
				continue;
			}

			// One line a name: the first takes the value, later ones go
			const std::string key = std::string(ExtraPrefix) + extra.Name;
			SetValue(key, FormatNumber(extra.Value));
			bool bFirst = true;
			Lines.erase(std::remove_if(Lines.begin(), Lines.end(),
							[&](const Line& line)
							{
								if (line.Name != key)
								{
									return false;
								}
								const bool bLater = !bFirst;
								bFirst = false;
								return bLater;
							}),
				Lines.end());
		}
	}

	const std::string* PhysicsMaterialAsset::FindValue(std::string_view name) const
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

	void PhysicsMaterialAsset::SetValue(std::string_view name, std::string value)
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

	bool PhysicsMaterialAsset::RemoveValue(std::string_view name)
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
}
