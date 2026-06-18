#pragma once

#include <filesystem>
#include <string>
#include <vector>

// Abstract Syntax Tree
namespace Nyx::HeaderTool
{
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
		std::filesystem::path HeaderPath;
		std::filesystem::path RelativePath;
		ParsedHeader Parsed;
	};
}