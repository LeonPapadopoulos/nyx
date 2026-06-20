#include "ReflectionSemantics.h"

#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace Nyx::HeaderTool
{
	void ReflectionSemantics::Apply(ParsedHeader& parsedHeader)
	{
		for (ParsedType& parsedType : parsedHeader.Types)
		{
			ApplyTypeSemantics(parsedType);

			for (ParsedProperty& parsedProperty : parsedType.Properties)
			{
				ApplyPropertySemantics(parsedProperty);
			}
		}
	}

	void ReflectionSemantics::ApplyTypeSemantics(ParsedType& parsedType)
	{
		parsedType.DisplayName = parsedType.Name;
		parsedType.RoleExpr = "EReflectedTypeRole::Plain";

		for (const ParsedMacroEntry& entry : parsedType.RawArguments.Specifiers)
		{
			if (entry.Name == "Component" && !entry.Value.has_value())
			{
				parsedType.RoleExpr = "EReflectedTypeRole::Component";
				continue;
			}

			if (entry.Name == "Role" && entry.Value.has_value())
			{
				if (entry.Value.value() == "Component")
				{
					parsedType.RoleExpr = "EReflectedTypeRole::Component";
					continue;
				}
				if (entry.Value.value() == "Plain")
				{
					parsedType.RoleExpr = "EReflectedTypeRole::Plain";
					continue;
				}
			}

			throw std::runtime_error(
				"Unsupported NYX_REFLECT specifier on type '" + parsedType.Name + "': " + entry.Name
			);
		}

		for (const ParsedMacroEntry& entry : parsedType.RawArguments.Metadata)
		{
			if (entry.Name == "DisplayName")
			{
				if (!entry.Value.has_value())
				{
					throw std::runtime_error(
						"DisplayName metadata on type '" + parsedType.Name + "' requires a value."
					);
				}

				parsedType.DisplayName = entry.Value.value();
				continue;
			}

			throw std::runtime_error(
				"Unsupported NYX_REFLECT metadata on type '" + parsedType.Name + "': " + entry.Name
			);
		}
	}

	void ReflectionSemantics::ApplyPropertySemantics(ParsedProperty& parsedProperty)
	{
		parsedProperty.DisplayName = parsedProperty.Name;
		parsedProperty.KindExpr = MapTypeToKind(parsedProperty.Type);

		bool bEdit = false;
		bool bUndo = false;
		bool bSerialize = false;

		parsedProperty.DragSpeed = "0.0f";
		parsedProperty.bDisplayAsDegrees = false;

		for (const ParsedMacroEntry& entry : parsedProperty.RawArguments.Specifiers)
		{
			if (entry.Name == "Edit" && !entry.Value.has_value())
			{
				bEdit = true;
				continue;
			}

			if (entry.Name == "Undo" && !entry.Value.has_value())
			{
				bUndo = true;
				continue;
			}

			if (entry.Name == "Serialize" && !entry.Value.has_value())
			{
				bSerialize = true;
				continue;
			}

			throw std::runtime_error(
				"Unsupported NYX_PROPERTY specifier on property '" + parsedProperty.Name + "': " + entry.Name
			);
		}

		for (const ParsedMacroEntry& entry : parsedProperty.RawArguments.Metadata)
		{
			if (entry.Name == "DisplayName")
			{
				if (!entry.Value.has_value())
				{
					throw std::runtime_error(
						"DisplayName metadata on property '" + parsedProperty.Name + "' requires a value."
					);
				}

				parsedProperty.DisplayName = entry.Value.value();
				continue;
			}

			if (entry.Name == "DragSpeed")
			{
				if (!entry.Value.has_value())
				{
					throw std::runtime_error(
						"DragSpeed metadata on property '" + parsedProperty.Name + "' requires a value."
					);
				}

				std::string value = entry.Value.value();
				if (!value.empty() && value.back() != 'f')
				{
					if (value.find('.') == std::string::npos)
					{
						value += ".0";
					}
					value += 'f';
				}

				parsedProperty.DragSpeed = value;
				continue;
			}

			if (entry.Name == "UI")
			{
				if (!entry.Value.has_value())
				{
					throw std::runtime_error(
						"UI metadata on property '" + parsedProperty.Name + "' requires a value."
					);
				}

				if (entry.Value.value() == "Degrees")
				{
					parsedProperty.bDisplayAsDegrees = true;
					continue;
				}

				throw std::runtime_error(
					"Unsupported UI metadata value on property '" + parsedProperty.Name + "': " + entry.Value.value()
				);
			}

			throw std::runtime_error(
				"Unsupported NYX_PROPERTY metadata on property '" + parsedProperty.Name + "': " + entry.Name
			);
		}

		std::vector<std::string> flags;
		if (bEdit) flags.push_back("EPropertyFlags::Edit");
		if (bUndo) flags.push_back("EPropertyFlags::Undo");
		if (bSerialize) flags.push_back("EPropertyFlags::Serialize");

		if (flags.empty())
		{
			parsedProperty.FlagsExpr = "EPropertyFlags::None";
		}
		else
		{
			std::ostringstream out;
			for (size_t i = 0; i < flags.size(); ++i)
			{
				if (i > 0)
				{
					out << " | ";
				}
				out << flags[i];
			}
			parsedProperty.FlagsExpr = out.str();
		}
	}

	std::string ReflectionSemantics::MapTypeToKind(const std::string& typeName)
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
}