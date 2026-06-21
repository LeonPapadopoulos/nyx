#include "CodeGenerator.h"

#include <sstream>

namespace Nyx::HeaderTool
{
	const char* CodeGenerator::ToGeneratedTypeRole(EParsedTypeRole role)
	{
		switch (role)
		{
		case EParsedTypeRole::Plain:     return "EReflectedTypeRole::Plain";
		case EParsedTypeRole::Component: return "EReflectedTypeRole::Component";
		default:                         return "EReflectedTypeRole::Plain";
		}
	}

	const char* CodeGenerator::ToGeneratedPropertyKind(EParsedPropertyKind kind)
	{
		switch (kind)
		{
		case EParsedPropertyKind::Bool:   return "EPropertyKind::Bool";
		case EParsedPropertyKind::Int32:  return "EPropertyKind::Int32";
		case EParsedPropertyKind::UInt32: return "EPropertyKind::UInt32";
		case EParsedPropertyKind::Float:  return "EPropertyKind::Float";
		case EParsedPropertyKind::Vec2:   return "EPropertyKind::Vec2";
		case EParsedPropertyKind::Vec3:   return "EPropertyKind::Vec3";
		case EParsedPropertyKind::Vec4:   return "EPropertyKind::Vec4";
		case EParsedPropertyKind::Quat:   return "EPropertyKind::Quat";
		case EParsedPropertyKind::String: return "EPropertyKind::String";
		default:                          return "EPropertyKind::Unknown";
		}
	}

	std::string CodeGenerator::ToGeneratedPropertyFlags(EParsedPropertyFlags flags)
	{
		if (flags == EParsedPropertyFlags::None)
		{
			return "EPropertyFlags::None";
		}

		std::vector<const char*> parts;

		if (HasFlag(flags, EParsedPropertyFlags::Edit))      parts.push_back("EPropertyFlags::Edit");
		if (HasFlag(flags, EParsedPropertyFlags::Undo))      parts.push_back("EPropertyFlags::Undo");
		if (HasFlag(flags, EParsedPropertyFlags::Serialize)) parts.push_back("EPropertyFlags::Serialize");
		if (HasFlag(flags, EParsedPropertyFlags::Hidden))    parts.push_back("EPropertyFlags::Hidden");
		if (HasFlag(flags, EParsedPropertyFlags::ReadOnly))  parts.push_back("EPropertyFlags::ReadOnly");

		std::ostringstream out;
		for (size_t i = 0; i < parts.size(); ++i)
		{
			if (i > 0)
			{
				out << " | ";
			}
			out << parts[i];
		}

		return out.str();
	}

	std::string CodeGenerator::GenerateMetadataHeader(const ParsedHeader& parsedHeader)
	{
		std::ostringstream out;

		out << "#pragma once\n\n";
		out << "#include \"ReflectionTypes.h\"\n\n";
		out << "namespace Nyx::Reflection::Generated\n";
		out << "{\n";

		for (const ParsedType& parsedType : parsedHeader.Types)
		{
			if (!parsedType.EmittedMetadata.empty())
			{
				out << "    inline constexpr MetadataEntry " << parsedType.Name << "_TypeMetadataEntries[] =\n";
				out << "    {\n";
				for (const ParsedMacroEntry& entry : parsedType.EmittedMetadata)
				{
					out << "        { \"" << EscapeCString(entry.Name) << "\", \"" << EscapeCString(entry.Value.value_or("")) << "\" },\n";
				}
				out << "    };\n\n";
			}

			for (const ParsedProperty& property : parsedType.Properties)
			{
				if (!property.EmittedMetadata.empty())
				{
					out << "    inline constexpr MetadataEntry "
						<< parsedType.Name << "_" << property.Name << "_MetadataEntries[] =\n";
					out << "    {\n";
					for (const ParsedMacroEntry& entry : property.EmittedMetadata)
					{
						out << "        { \"" << EscapeCString(entry.Name) << "\", \"" << EscapeCString(entry.Value.value_or("")) << "\" },\n";
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
				out << "            " << ToGeneratedPropertyKind(property.Kind) << ",\n";
				out << "            " << ToGeneratedPropertyFlags(property.Flags) << ",\n";
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
			out << "        " << ToGeneratedTypeRole(parsedType.Role) << ",\n";
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
				if (parsedType.Role == EParsedTypeRole::Component)
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