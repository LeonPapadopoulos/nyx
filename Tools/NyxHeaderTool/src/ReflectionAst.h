#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Nyx::HeaderTool
{
	struct ParsedMacroEntry
	{
		std::string Name;
		std::optional<std::string> Value;
	};

	struct ParsedMacroArguments
	{
		std::vector<ParsedMacroEntry> Specifiers;
		std::vector<ParsedMacroEntry> Metadata;
	};

	struct ParsedProperty
	{
		std::string Type;
		std::string Name;
		std::string DisplayName;

		ParsedMacroArguments RawArguments;

		std::string FlagsExpr;
		std::string KindExpr;

		// generic metadata that will be emitted into generated code
		std::vector<ParsedMacroEntry> EmittedMetadata;
	};

	struct ParsedType
	{
		std::string Name;
		std::string QualifiedName;
		std::string DisplayName;
		std::string RoleExpr;

		ParsedMacroArguments RawArguments;
		std::vector<ParsedMacroEntry> EmittedMetadata;

		std::vector<ParsedProperty> Properties;
	};

	struct ParsedHeader
	{
		std::vector<ParsedType> Types;
	};

	struct ScannedHeader
	{
		std::filesystem::path HeaderPath;
		std::filesystem::path RelativePath;
		ParsedHeader Parsed;
	};
}