#include "CodeGenerator.h"

#include <sstream>

namespace Nyx::HeaderTool
{
	std::string CodeGenerator::GenerateMetadataHeader(const ParsedHeader& parsedHeader)
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
			out << "        \"" << EscapeCString(parsedType.DisplayName) << "\",\n";
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

	std::string CodeGenerator::GenerateModuleInitHeader()
	{
		std::ostringstream out;

		out << "#pragma once\n\n";
		out << "namespace Nyx::Reflection::Generated\n";
		out << "{\n";
		out << "    void RegisterRuntimeReflectedTypes();\n";
		out << "}\n";

		return out.str();
	}

	std::string CodeGenerator::GenerateModuleInitCpp(const std::vector<ScannedHeader>& scannedHeaders)
	{
		std::ostringstream out;

		out << "#include \"Generated/Runtime/Runtime.reflect.init.h\"\n";
		out << "#include \"ReflectedComponentAutoRegistration.h\"\n\n";

		for (const ScannedHeader& header : scannedHeaders)
		{
			out << "#include \"" << header.RelativePath.generic_string() << "\"\n";
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

	std::filesystem::path CodeGenerator::MakeGeneratedHeaderPath(
		const std::filesystem::path& relativeHeaderPath,
		const std::filesystem::path& outputDir)
	{
		std::filesystem::path outputPath = outputDir / relativeHeaderPath;
		outputPath.replace_extension(".reflect.h");
		return outputPath;
	}

	std::string CodeGenerator::EscapeCString(const std::string& value)
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
}