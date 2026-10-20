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
		// A module's files are named after it: Register<Module>ReflectedTypes() in
		// Generated/<Module>/<Module>.reflect.init.h. The engine's module is Runtime.
		static std::string GenerateModuleInitHeader(const std::string& moduleName);
		static std::string GenerateModuleInitCpp(
			const std::vector<ScannedHeader>& scannedHeaders,
			const std::string& moduleName);
		static std::string GenerateEditorSourcesCpp(
			const std::vector<ScannedHeader>& scannedHeaders,
			const std::string& moduleName);

		// A module name is part of C++ function names, so it must be an identifier
		static bool IsValidModuleName(const std::string& moduleName);

		static std::filesystem::path MakeGeneratedHeaderPath(
			const std::filesystem::path& relativeHeaderPath,
			const std::filesystem::path& outputDir);

		static std::string MetadataValueToString(const ParsedValue& value);

	private:
		static std::string EscapeCString(const std::string& value);

		static const char* ToGeneratedTypeRole(EParsedTypeRole role);
		static const char* ToGeneratedPropertyKind(EParsedPropertyKind kind);
		static std::string ToGeneratedPropertyFlags(EParsedPropertyFlags flags);
		static std::string ToGeneratedHash(uint32_t hash);
	};
}
