#pragma once

#include "ReflectionAst.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Nyx::HeaderTool
{
	class CodeGenerator
	{
	public:
		static std::string GenerateMetadataHeader(const ParsedHeader& parsedHeader);
		static std::string GenerateModuleInitHeader();
		static std::string GenerateModuleInitCpp(const std::vector<ScannedHeader>& scannedHeaders);

		static std::filesystem::path MakeGeneratedHeaderPath(
			const std::filesystem::path& relativeHeaderPath,
			const std::filesystem::path& outputDir);

	private:
		static std::string EscapeCString(const std::string& value);

		static const char* ToGeneratedTypeRole(EParsedTypeRole role);
		static const char* ToGeneratedPropertyKind(EParsedPropertyKind kind);
		static std::string ToGeneratedPropertyFlags(EParsedPropertyFlags flags);
	};
}