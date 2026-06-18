#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <optional>

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
	std::string QualifiedName;

	std::string DisplayName;
	std::string RoleExpr;

	std::vector<ParsedProperty> Properties;
};

struct ParsedHeader
{
	std::vector<ParsedType> Types;
};

struct ScannedHeader
{
	fs::path HeaderPath;
	fs::path RelativePath;
	ParsedHeader Parsed;
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
	bool bInString = false;
	bool bEscape = false;

	for (char c : input)
	{
		if (bInString)
		{
			current.push_back(c);

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
				bInString = false;
			}

			continue;
		}

		if (c == '"')
		{
			bInString = true;
			current.push_back(c);
			continue;
		}

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

static size_t FindMatchingClosingBrace(const std::string& text, size_t bodyStart)
{
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
				return i;
			}
		}
	}

	return std::string::npos;
}

static std::string EscapeCString(const std::string& value)
{
	std::string out;
	out.reserve(value.size());

	for (char c : value)
	{
		switch (c)
		{
		case '\\': out += "\\\\"; break;
		case '"':  out += "\\\""; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default:   out += c; break;
		}
	}

	return out;
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

static fs::path MakeGeneratedHeaderPath(
	const fs::path& headerPath,
	const fs::path& scanRoot,
	const fs::path& outputDir)
{
	fs::path relativePath = fs::relative(headerPath, scanRoot);
	relativePath.replace_extension(".reflect.h");
	return outputDir / relativePath;
}

static void ParseReflectArgs(const std::string& reflectArgs, ParsedType& parsedType)
{
	parsedType.DisplayName = parsedType.Name;
	parsedType.RoleExpr = "EReflectedTypeRole::Plain";

	const std::vector<std::string> tokens = SplitCommaSeparated(reflectArgs);

	for (const std::string& tokenRaw : tokens)
	{
		const std::string token = Trim(tokenRaw);
		if (token.empty())
		{
			continue;
		}

		if (token == "Component")
		{
			parsedType.RoleExpr = "EReflectedTypeRole::Component";
			continue;
		}

		std::smatch match;
		if (std::regex_match(token, match, std::regex(R"REGEX(^DisplayName\s*=\s*"([^"]*)"$)REGEX")))
		{
			parsedType.DisplayName = match[1].str();
			continue;
		}

		throw std::runtime_error(
			"Unsupported NYX_REFLECT token on type '" + parsedType.Name + "': " + token
		);
	}
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
				value += 'f';
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

static std::string ParseQualifiedTypeNameFromPrefix(
	const std::string& fullText,
	size_t reflectMatchPos,
	const std::string& typeName)
{
	const std::string prefix = fullText.substr(0, reflectMatchPos);

	// Version 0: use the last namespace declaration before the reflected struct.
	// Supports:
	//   namespace Nyx::Engine
	// and
	//   namespace Foo
	const std::regex namespaceRegex(
		R"(namespace\s+([A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)\s*\{)"
	);

	std::string namespaceName;
	for (auto it = std::sregex_iterator(prefix.begin(), prefix.end(), namespaceRegex);
		it != std::sregex_iterator();
		++it)
	{
		namespaceName = (*it)[1].str();
	}

	if (namespaceName.empty())
	{
		return typeName;
	}

	return namespaceName + "::" + typeName;
}

static std::vector<ParsedProperty> ParsePropertiesFromBody(const std::string& body)
{
	// Matches:
	//   NYX_PROPERTY(...)
	//   TypeName PropertyName;
	//   TypeName PropertyName{ ... };
	//   TypeName PropertyName = ...;

	const std::string qualifiedType = R"(([A-Za-z_]\w*(?:::[A-Za-z_]\w*)*))";
	const std::string identifier = R"(([A-Za-z_]\w*))";
	const std::string macroArgs = R"((.*?))";
	const std::string optionalInitializer = R"((?:(?:\{[\s\S]*?\})|(?:=\s*[^;]+))?)";

	const std::regex propertyRegex(
		R"(NYX_PROPERTY\s*\()"
		+ macroArgs
		+ R"(\)\s*)"
		+ qualifiedType
		+ R"(\s+)"
		+ identifier
		+ R"(\s*)"
		+ optionalInitializer
		+ R"(\s*;)"
	);

	std::vector<ParsedProperty> properties;

	for (auto it = std::sregex_iterator(body.begin(), body.end(), propertyRegex);
		it != std::sregex_iterator();
		++it)
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

		properties.push_back(std::move(property));
	}

	return properties;
}

static ParsedHeader ParseHeader(const std::string& strippedText)
{
	const std::regex headerRegex(
		R"(NYX_REFLECT\s*\((.*?)\)\s*struct\s+([A-Za-z_]\w*)\s*\{)"
	);

	ParsedHeader parsedHeader{};

	for (auto it = std::sregex_iterator(strippedText.begin(), strippedText.end(), headerRegex);
		it != std::sregex_iterator();
		++it)
	{
		const std::smatch& headerMatch = *it;

		const std::string reflectArgs = headerMatch[1].str();

		ParsedType parsedType{};
		parsedType.Name = headerMatch[2].str();
		parsedType.QualifiedName = ParseQualifiedTypeNameFromPrefix(
			strippedText,
			static_cast<size_t>(headerMatch.position(0)),
			parsedType.Name
		);

		ParseReflectArgs(reflectArgs, parsedType);

		const size_t bodyStart = static_cast<size_t>(headerMatch.position(0) + headerMatch.length(0));
		const size_t bodyEnd = FindMatchingClosingBrace(strippedText, bodyStart);

		if (bodyEnd == std::string::npos)
		{
			throw std::runtime_error(
				"Could not find end of reflected struct body for type: " + parsedType.Name
			);
		}

		const std::string body = strippedText.substr(bodyStart, bodyEnd - bodyStart);
		parsedType.Properties = ParsePropertiesFromBody(body);

		if (parsedType.Properties.empty())
		{
			throw std::runtime_error(
				"Reflected type '" + parsedType.Name + "' has no NYX_PROPERTY members."
			);
		}

		parsedHeader.Types.push_back(std::move(parsedType));
	}

	if (parsedHeader.Types.empty())
	{
		throw std::runtime_error("Could not find any NYX_REFLECT() structs.");
	}

	return parsedHeader;
}

static std::string GenerateModuleInitHeader()
{
	std::ostringstream out;

	out << "#pragma once\n\n";
	out << "namespace Nyx::Reflection::Generated\n";
	out << "{\n";
	out << "    void RegisterRuntimeReflectedTypes();\n";
	out << "}\n";

	return out.str();
}

static std::string GenerateModuleInitCpp(const std::vector<ScannedHeader>& scannedHeaders)
{
	std::ostringstream out;

	out << "#include \"Generated/Runtime/Runtime.reflect.init.h\"\n";
	out << "#include \"ReflectedComponentAutoRegistration.h\"\n\n";

	for (const ScannedHeader& header : scannedHeaders)
	{
		std::string includePath = header.RelativePath.generic_string();
		out << "#include \"" << includePath << "\"\n";
	}

	out << "\nnamespace Nyx::Reflection::Generated\n";
	out << "{\n";
	out << "    void RegisterRuntimeReflectedTypes()\n";
	out << "    {\n";
	out << "        static bool bRegistered = false;\n";
	out << "        if (bRegistered)\n";
	out << "        {\n";
	out << "            return;\n";
	out << "        }\n";
	out << "        bRegistered = true;\n\n";

	for (const ScannedHeader& header : scannedHeaders)
	{
		for (const ParsedType& parsedType : header.Parsed.Types)
		{
			if (parsedType.RoleExpr == "EReflectedTypeRole::Component")
			{
				out << "        Nyx::Editor::RegisterReflectedComponentType<"
					<< parsedType.QualifiedName
					<< ">(\""
					<< EscapeCString(parsedType.DisplayName)
					<< "\");\n";
			}
		}
	}

	out << "    }\n";
	out << "}\n";

	return out.str();
}

static std::string GenerateMetadataHeader(const ParsedHeader& parsedHeader)
{
	std::ostringstream out;

	out << "#pragma once\n\n";
	out << "#include \"ReflectionTypes.h\"\n\n";
	out << "namespace Nyx::Reflection::Generated\n";
	out << "{\n";

	for (const ParsedType& parsedType : parsedHeader.Types)
	{
		out << "    inline constexpr PropertyMetadata " << parsedType.Name << "_Properties[] =\n";
		out << "    {\n";

		for (const ParsedProperty& property : parsedType.Properties)
		{
			out << "        {\n";
			out << "            \"" << property.Name << "\",\n";
			out << "            \"" << property.Name << "\",\n";
			out << "            " << property.KindExpr << ",\n";
			out << "            " << property.FlagsExpr << ",\n";
			out << "            offsetof(" << parsedType.QualifiedName << ", " << property.Name << "),\n";
			out << "            " << property.DragSpeed << ",\n";
			out << "            " << (property.bDisplayAsDegrees ? "true" : "false") << "\n";
			out << "        },\n";
		}

		out << "    };\n\n";

		out << "    inline constexpr TypeMetadata " << parsedType.Name << "_TypeMetadata\n";
		out << "    {\n";
		out << "        \"" << parsedType.QualifiedName << "\",\n";
		out << "        \"" << parsedType.DisplayName << "\",\n";
		out << "        " << parsedType.RoleExpr << ",\n";
		out << "        " << parsedType.Name << "_Properties,\n";
		out << "        sizeof(" << parsedType.Name << "_Properties) / sizeof(" << parsedType.Name << "_Properties[0])\n";
		out << "    };\n\n";
	}

	out << "}\n\n";

	out << "namespace Nyx::Reflection\n";
	out << "{\n";

	for (const ParsedType& parsedType : parsedHeader.Types)
	{
		out << "    template<>\n";
		out << "    inline const TypeMetadata& GetTypeMetadata<" << parsedType.QualifiedName << ">()\n";
		out << "    {\n";
		out << "        return Generated::" << parsedType.Name << "_TypeMetadata;\n";
		out << "    }\n\n";
	}

	out << "}\n";

	return out.str();
}

static bool IsHeaderFile(const fs::path& path)
{
	const std::string ext = path.extension().string();
	return ext == ".h" || ext == ".hpp";
}

static bool MightContainReflection(const std::string& text)
{
	return text.find("NYX_REFLECT") != std::string::npos;
}

static void GenerateForSingleHeader(const fs::path& inputHeader, const fs::path& outputHeader)
{
	const std::string rawText = ReadAllText(inputHeader);
	const std::string strippedText = StripComments(rawText);
	const ParsedHeader parsedHeader = ParseHeader(strippedText);
	const std::string generated = GenerateMetadataHeader(parsedHeader);

	WriteAllText(outputHeader, generated);

	std::cout << "Generated: " << outputHeader.string() << "\n";
}

static size_t GenerateForScanRoots(
	const std::vector<fs::path>& scanRoots,
	const fs::path& outputDir,
	std::vector<ScannedHeader>& outScannedHeaders)
{
	size_t generatedCount = 0;
	std::unordered_set<std::string> processedHeaders;

	for (const fs::path& root : scanRoots)
	{
		if (!fs::exists(root))
		{
			throw std::runtime_error("Scan root does not exist: " + root.string());
		}

		for (const fs::directory_entry& entry : fs::recursive_directory_iterator(root))
		{
			if (!entry.is_regular_file())
			{
				continue;
			}

			const fs::path headerPath = entry.path();
			if (!IsHeaderFile(headerPath))
			{
				continue;
			}

			const std::string canonicalHeaderPath = fs::weakly_canonical(headerPath).string();
			if (processedHeaders.contains(canonicalHeaderPath))
			{
				continue;
			}

			const std::string rawText = ReadAllText(headerPath);
			if (!MightContainReflection(rawText))
			{
				continue;
			}

			const std::string strippedText = StripComments(rawText);
			if (!MightContainReflection(strippedText))
			{
				continue;
			}

			ParsedHeader parsedHeader;
			try
			{
				parsedHeader = ParseHeader(strippedText);
			}
			catch (const std::exception& ex)
			{
				throw std::runtime_error(
					"Failed parsing reflected header '" + headerPath.string() + "': " + ex.what()
				);
			}

			const fs::path relativePath = fs::relative(headerPath, root);
			const fs::path outputHeader = outputDir / relativePath;
			fs::path finalOutputHeader = outputHeader;
			finalOutputHeader.replace_extension(".reflect.h");

			const std::string generated = GenerateMetadataHeader(parsedHeader);

			WriteAllText(finalOutputHeader, generated);
			std::cout << "Generated: " << finalOutputHeader.string() << "\n";

			outScannedHeaders.push_back(ScannedHeader{
				.HeaderPath = headerPath,
				.RelativePath = relativePath,
				.Parsed = std::move(parsedHeader)
				});

			processedHeaders.insert(canonicalHeaderPath);
			++generatedCount;
		}
	}

	return generatedCount;
}

static int RunSingleHeaderMode(int argc, char** argv)
{
	if (argc != 4)
	{
		std::cerr << "Usage: NyxHeaderTool <input_header> <output_header> <qualified_type_name>\n";
		return 1;
	}

	const fs::path inputHeader = argv[1];
	const fs::path outputHeader = argv[2];

	// Old mode keeps the 3rd arg for backward compatibility, but it is now ignored.
	(void)argv[3];

	GenerateForSingleHeader(inputHeader, outputHeader);
	return 0;
}

static int RunScanMode(int argc, char** argv)
{
	std::vector<fs::path> scanRoots;
	std::optional<fs::path> outputDir;
	std::optional<fs::path> moduleInitHeader;
	std::optional<fs::path> moduleInitCpp;

	for (int i = 1; i < argc; ++i)
	{
		const std::string arg = argv[i];

		if (arg == "--scan-root")
		{
			if (i + 1 >= argc)
			{
				throw std::runtime_error("--scan-root requires a value");
			}
			scanRoots.emplace_back(argv[++i]);
		}
		else if (arg == "--output-dir")
		{
			if (i + 1 >= argc)
			{
				throw std::runtime_error("--output-dir requires a value");
			}
			outputDir = fs::path(argv[++i]);
		}
		else if (arg == "--module-init-header")
		{
			if (i + 1 >= argc)
			{
				throw std::runtime_error("--module-init-header requires a value");
			}
			moduleInitHeader = fs::path(argv[++i]);
		}
		else if (arg == "--module-init-cpp")
		{
			if (i + 1 >= argc)
			{
				throw std::runtime_error("--module-init-cpp requires a value");
			}
			moduleInitCpp = fs::path(argv[++i]);
		}
		else
		{
			throw std::runtime_error("Unknown argument: " + arg);
		}
	}

	if (scanRoots.empty())
	{
		throw std::runtime_error("No --scan-root arguments were provided.");
	}

	if (!outputDir.has_value())
	{
		throw std::runtime_error("Missing --output-dir argument.");
	}

	std::vector<ScannedHeader> scannedHeaders;
	const size_t count = GenerateForScanRoots(scanRoots, outputDir.value(), scannedHeaders);

	if (moduleInitHeader.has_value())
	{
		WriteAllText(moduleInitHeader.value(), GenerateModuleInitHeader());
		std::cout << "Generated: " << moduleInitHeader.value().string() << "\n";
	}

	if (moduleInitCpp.has_value())
	{
		WriteAllText(moduleInitCpp.value(), GenerateModuleInitCpp(scannedHeaders));
		std::cout << "Generated: " << moduleInitCpp.value().string() << "\n";
	}

	std::cout << "Reflection generation complete. Generated " << count << " file(s).\n";
	return 0;
}

int main(int argc, char** argv)
{
	try
	{
		if (argc <= 1)
		{
			std::cerr << "Usage:\n";
			std::cerr << "  NyxHeaderTool <input_header> <output_header> <qualified_type_name>\n";
			std::cerr << "  NyxHeaderTool --scan-root <dir> [--scan-root <dir> ...] --output-dir <dir>\n";
			return 1;
		}

		// New mode
		if (std::string_view(argv[1]) == "--scan-root")
		{
			return RunScanMode(argc, argv);
		}

		// Old mode
		return RunSingleHeaderMode(argc, argv);
	}
	catch (const std::exception& ex)
	{
		std::cerr << "NyxHeaderTool error: " << ex.what() << "\n";
		return 1;
	}
}