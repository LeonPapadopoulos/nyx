#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

struct ParsedProperty
{
	std::string Type;
	std::string Name;
	std::string FlagsExpr;
	std::string DragSpeed;
	bool bDisplayAsDegrees = false;
	std::string KindExpr;
};

struct ParsedType
{
	std::string Name;
	std::vector<ParsedProperty> Properties;
};

static std::string ReadAllText(const fs::path& path)
{
	std::ifstream file(path, std::ios::in | std::ios::binary);
	if (!file)
	{
		throw std::runtime_error("Failed to open input file: " + path.string());
	}

	std::ostringstream stream;
	stream << file.rdbuf();
	return stream.str();
}

static void WriteAllText(const fs::path& path, const std::string& text)
{
	fs::create_directories(path.parent_path());

	std::ofstream file(path, std::ios::out | std::ios::binary | std::ios::trunc);
	if (!file)
	{
		throw std::runtime_error("Failed to open output file: " + path.string());
	}

	file << text;
}

static std::string Trim(const std::string& value)
{
	const size_t first = value.find_first_not_of(" \t\r\n");
	if (first == std::string::npos)
	{
		return "";
	}

	const size_t last = value.find_last_not_of(" \t\r\n");
	return value.substr(first, last - first + 1);
}

static std::vector<std::string> SplitCommaSeparated(const std::string& input)
{
	std::vector<std::string> result;
	std::string current;
	int angleDepth = 0;

	for (char c : input)
	{
		if (c == '<')
		{
			++angleDepth;
		}
		else if (c == '>')
		{
			--angleDepth;
		}

		if (c == ',' && angleDepth == 0)
		{
			result.push_back(Trim(current));
			current.clear();
		}
		else
		{
			current.push_back(c);
		}
	}

	if (!Trim(current).empty())
	{
		result.push_back(Trim(current));
	}

	return result;
}

static std::string MapTypeToKind(const std::string& typeName)
{
	static const std::unordered_map<std::string, std::string> Map =
	{
		{ "bool", "EPropertyKind::Bool" },
		{ "int32_t", "EPropertyKind::Int32" },
		{ "uint32_t", "EPropertyKind::UInt32" },
		{ "float", "EPropertyKind::Float" },
		{ "glm::vec2", "EPropertyKind::Vec2" },
		{ "glm::vec3", "EPropertyKind::Vec3" },
		{ "glm::vec4", "EPropertyKind::Vec4" },
		{ "glm::quat", "EPropertyKind::Quat" },
		{ "std::string", "EPropertyKind::String" }
	};

	const auto it = Map.find(typeName);
	if (it == Map.end())
	{
		throw std::runtime_error("Unsupported reflected property type: " + typeName);
	}

	return it->second;
}

static std::string StripComments(const std::string& input)
{
	std::string output;
	output.reserve(input.size());

	bool bInLineComment = false;
	bool bInBlockComment = false;
	bool bInStringLiteral = false;
	bool bInCharLiteral = false;
	bool bEscape = false;

	for (size_t i = 0; i < input.size(); ++i)
	{
		const char c = input[i];
		const char next = (i + 1 < input.size()) ? input[i + 1] : '\0';

		if (bInLineComment)
		{
			if (c == '\n')
			{
				bInLineComment = false;
				output.push_back(c);
			}
			continue;
		}

		if (bInBlockComment)
		{
			if (c == '*' && next == '/')
			{
				bInBlockComment = false;
				++i;
			}
			continue;
		}

		if (bInStringLiteral)
		{
			output.push_back(c);

			if (bEscape)
			{
				bEscape = false;
			}
			else if (c == '\\')
			{
				bEscape = true;
			}
			else if (c == '"')
			{
				bInStringLiteral = false;
			}

			continue;
		}

		if (bInCharLiteral)
		{
			output.push_back(c);

			if (bEscape)
			{
				bEscape = false;
			}
			else if (c == '\\')
			{
				bEscape = true;
			}
			else if (c == '\'')
			{
				bInCharLiteral = false;
			}

			continue;
		}

		if (c == '/' && next == '/')
		{
			bInLineComment = true;
			++i;
			continue;
		}

		if (c == '/' && next == '*')
		{
			bInBlockComment = true;
			++i;
			continue;
		}

		if (c == '"')
		{
			bInStringLiteral = true;
			output.push_back(c);
			continue;
		}

		if (c == '\'')
		{
			bInCharLiteral = true;
			output.push_back(c);
			continue;
		}

		output.push_back(c);
	}

	return output;
}

static std::string ParseFlags(const std::vector<std::string>& tokens)
{
	std::vector<std::string> flags;

	for (const std::string& tokenRaw : tokens)
	{
		const std::string token = Trim(tokenRaw);

		if (token == "Edit")
		{
			flags.push_back("EPropertyFlags::Edit");
		}
		else if (token == "Undo")
		{
			flags.push_back("EPropertyFlags::Undo");
		}
		else if (token == "Serialize")
		{
			flags.push_back("EPropertyFlags::Serialize");
		}
	}

	if (flags.empty())
	{
		return "EPropertyFlags::None";
	}

	std::ostringstream out;
	for (size_t i = 0; i < flags.size(); ++i)
	{
		if (i > 0)
		{
			out << " | ";
		}
		out << flags[i];
	}

	return out.str();
}

static std::string ParseDragSpeed(const std::vector<std::string>& tokens)
{
	for (const std::string& token : tokens)
	{
		const std::string trimmed = Trim(token);

		const std::regex dragSpeedRegex(R"(^DragSpeed\s*=\s*([0-9]*\.?[0-9]+f?)$)");
		std::smatch match;
		if (std::regex_match(trimmed, match, dragSpeedRegex))
		{
			std::string value = match[1].str();
			if (!value.empty() && value.back() != 'f')
			{
				value += "f";
			}
			return value;
		}
	}

	return "0.0f";
}

static bool ParseDisplayAsDegrees(const std::vector<std::string>& tokens)
{
	for (const std::string& token : tokens)
	{
		const std::string trimmed = Trim(token);
		if (std::regex_match(trimmed, std::regex(R"(^UI\s*=\s*Degrees$)")))
		{
			return true;
		}
	}

	return false;
}

static ParsedType ParseHeader(const std::string& text)
{
	// Version 0 assumptions:
	// - one reflected struct in the file
	// - NYX_REFLECT() immediately precedes `struct Name { ... }`
	// - we find the struct body by matching braces, not with a lazy regex

	const std::regex headerRegex(
		R"(NYX_REFLECT\s*\(\s*\)\s*struct\s+([A-Za-z_]\w*)\s*\{)"
	);

	std::smatch headerMatch;
	if (!std::regex_search(text, headerMatch, headerRegex))
	{
		throw std::runtime_error("Could not find NYX_REFLECT() struct.");
	}

	ParsedType parsedType{};
	parsedType.Name = headerMatch[1].str();

	const size_t bodyStart = static_cast<size_t>(headerMatch.position(0) + headerMatch.length(0));
	size_t bodyEnd = std::string::npos;

	int braceDepth = 1;
	for (size_t i = bodyStart; i < text.size(); ++i)
	{
		const char c = text[i];

		if (c == '{')
		{
			++braceDepth;
		}
		else if (c == '}')
		{
			--braceDepth;
			if (braceDepth == 0)
			{
				bodyEnd = i;
				break;
			}
		}
	}

	if (bodyEnd == std::string::npos)
	{
		throw std::runtime_error("Could not find end of reflected struct body.");
	}

	const std::string body = text.substr(bodyStart, bodyEnd - bodyStart);

	//const std::string whitespace = R"(\s*)";
	//const std::string requiredWhitespace = R"(\s+)";
	//const std::string identifier = R"(([A-Za-z_]\w*))";
	//const std::string qualifiedType = R"(([A-Za-z_]\w*(?:::[A-Za-z_]\w*)*))";
	//const std::string macroArgs = R"((.*?))";
	//const std::string braceInitializer = R"((?:\{[\s\S]*?\})?)";
	//const std::string terminator = R"(\s*;)";

	//const std::regex propertyRegex(
	//	R"(NYX_PROPERTY)"
	//	+ whitespace
	//	+ R"(\()"
	//	+ macroArgs
	//	+ R"(\))"
	//	+ whitespace
	//	+ qualifiedType
	//	+ requiredWhitespace
	//	+ identifier
	//	+ whitespace
	//	+ braceInitializer
	//	+ terminator
	//);

	const std::regex propertyRegex(
		R"(NYX_PROPERTY\s*\((.*?)\)\s*)"
		R"(([A-Za-z_]\w*(?:::[A-Za-z_]\w*)*))"
		R"(\s+)"
		R"(([A-Za-z_]\w*))"
		R"(\s*)"
		R"((?:\{[\s\S]*?\})?)"
		R"(\s*;)"
	);

	auto begin = std::sregex_iterator(body.begin(), body.end(), propertyRegex);
	auto end = std::sregex_iterator();

	for (auto it = begin; it != end; ++it)
	{
		const std::smatch& match = *it;

		const std::string metaText = match[1].str();
		const std::string typeName = match[2].str();
		const std::string propertyName = match[3].str();

		const std::vector<std::string> metaTokens = SplitCommaSeparated(metaText);

		ParsedProperty property{};
		property.Type = typeName;
		property.Name = propertyName;
		property.KindExpr = MapTypeToKind(typeName);
		property.FlagsExpr = ParseFlags(metaTokens);
		property.DragSpeed = ParseDragSpeed(metaTokens);
		property.bDisplayAsDegrees = ParseDisplayAsDegrees(metaTokens);

		parsedType.Properties.push_back(std::move(property));
	}

	if (parsedType.Properties.empty())
	{
		throw std::runtime_error("Reflected type has no NYX_PROPERTY members.");
	}

	return parsedType;
}

static std::string GenerateMetadataHeader(
	const ParsedType& parsedType,
	const std::string& qualifiedTypeName)
{
	std::ostringstream out;

	out << "#pragma once\n\n";
	out << "#include \"ReflectionTypes.h\"\n\n";
	out << "namespace Nyx::Reflection::Generated\n";
	out << "{\n";
	out << "    inline constexpr PropertyMetadata " << parsedType.Name << "_Properties[] =\n";
	out << "    {\n";

	for (const ParsedProperty& property : parsedType.Properties)
	{
		out << "        {\n";
		out << "            \"" << property.Name << "\",\n";
		out << "            \"" << property.Name << "\",\n";
		out << "            " << property.KindExpr << ",\n";
		out << "            " << property.FlagsExpr << ",\n";
		out << "            offsetof(" << qualifiedTypeName << ", " << property.Name << "),\n";
		out << "            " << property.DragSpeed << ",\n";
		out << "            " << (property.bDisplayAsDegrees ? "true" : "false") << "\n";
		out << "        },\n";
	}

	out << "    };\n\n";
	out << "    inline constexpr TypeMetadata " << parsedType.Name << "_TypeMetadata\n";
	out << "    {\n";
	out << "        \"" << qualifiedTypeName << "\",\n";
	out << "        " << parsedType.Name << "_Properties,\n";
	out << "        sizeof(" << parsedType.Name << "_Properties) / sizeof(" << parsedType.Name << "_Properties[0])\n";
	out << "    };\n";
	out << "}\n\n";

	out << "namespace Nyx::Reflection\n";
	out << "{\n";
	out << "    template<>\n";
	out << "    inline const TypeMetadata& GetTypeMetadata<" << qualifiedTypeName << ">()\n";
	out << "    {\n";
	out << "        return Generated::" << parsedType.Name << "_TypeMetadata;\n";
	out << "    }\n";
	out << "}\n";

	return out.str();
}

int main(int argc, char** argv)
{
	try
	{
		if (argc != 4)
		{
			std::cerr << "Usage: NyxHeaderTool <input_header> <output_header> <qualified_type_name>\n";
			return 1;
		}

		const fs::path inputHeader = argv[1];
		const fs::path outputHeader = argv[2];
		const std::string qualifiedTypeName = argv[3];

		const std::string text = ReadAllText(inputHeader);
		const std::string strippedText = StripComments(text);
		const ParsedType parsedType = ParseHeader(strippedText);
		const std::string generated = GenerateMetadataHeader(parsedType, qualifiedTypeName);

		WriteAllText(outputHeader, generated);

		std::cout << "Generated: " << outputHeader.string() << "\n";
		return 0;
	}
	catch (const std::exception& ex)
	{
		std::cerr << "NyxHeaderTool error: " << ex.what() << "\n";
		return 1;
	}
}