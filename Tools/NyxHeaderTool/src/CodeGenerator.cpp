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
			// Type metadata entries
			if (!parsedType.EmittedMetadata.empty())
			{
				out << "    inline constexpr MetadataEntry " << parsedType.Name << "_TypeMetadataEntries[] =\n";
				out << "    {\n";
				for (const ParsedMacroEntry& entry : parsedType.EmittedMetadata)
				{
					out << "        { \""
						<< EscapeCString(entry.Name)
						<< "\", \""
						<< EscapeCString(entry.Value.value_or(""))
						<< "\" },\n";
				}
				out << "    };\n\n";
			}

			// Property metadata entries
			for (const ParsedProperty& property : parsedType.Properties)
			{
				if (!property.EmittedMetadata.empty())
				{
					out << "    inline constexpr MetadataEntry "
						<< parsedType.Name << "_" << property.Name << "_MetadataEntries[] =\n";
					out << "    {\n";
					for (const ParsedMacroEntry& entry : property.EmittedMetadata)
					{
						out << "        { \""
							<< EscapeCString(entry.Name)
							<< "\", \""
							<< EscapeCString(entry.Value.value_or(""))
							<< "\" },\n";
					}
					out << "    };\n\n";
				}
			}

			out << "    inline constexpr PropertyMetadata " << parsedType.Name << "_Properties[] =\n";
			out << "    {\n";

			for (const ParsedProperty& property : parsedType.Properties)
			{
				const std::string propertyMetadataArrayName =
					parsedType.Name + "_" + property.Name + "_MetadataEntries";

				const std::string propertyMetadataPointer =
					property.EmittedMetadata.empty() ? "nullptr" : propertyMetadataArrayName;

				const std::string propertyMetadataCount =
					property.EmittedMetadata.empty()
					? "0"
					: "sizeof(" + propertyMetadataArrayName + ") / sizeof(" + propertyMetadataArrayName + "[0])";

				out << "        {\n";
				out << "            \"" << EscapeCString(property.Name) << "\",\n";
				out << "            \"" << EscapeCString(property.DisplayName) << "\",\n";
				out << "            " << property.KindExpr << ",\n";
				out << "            " << property.FlagsExpr << ",\n";
				out << "            offsetof(" << parsedType.QualifiedName << ", " << property.Name << "),\n";
				out << "            " << propertyMetadataPointer << ",\n";
				out << "            " << propertyMetadataCount << "\n";
				out << "        },\n";
			}

			out << "    };\n\n";

			const std::string typeMetadataArrayName = parsedType.Name + "_TypeMetadataEntries";
			const std::string typeMetadataPointer =
				parsedType.EmittedMetadata.empty() ? "nullptr" : typeMetadataArrayName;
			const std::string typeMetadataCount =
				parsedType.EmittedMetadata.empty()
				? "0"
				: "sizeof(" + typeMetadataArrayName + ") / sizeof(" + typeMetadataArrayName + "[0])";

			out << "    inline constexpr TypeMetadata " << parsedType.Name << "_TypeMetadata\n";
			out << "    {\n";
			out << "        \"" << EscapeCString(parsedType.QualifiedName) << "\",\n";
			out << "        \"" << EscapeCString(parsedType.DisplayName) << "\",\n";
			out << "        " << parsedType.RoleExpr << ",\n";
			out << "        " << typeMetadataPointer << ",\n";
			out << "        " << typeMetadataCount << ",\n";
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