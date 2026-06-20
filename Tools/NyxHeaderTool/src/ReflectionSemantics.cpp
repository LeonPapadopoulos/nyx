#include "ReflectionSemantics.h"
#include "SpecifierRegistry.h"

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

		TypeSemanticContext ctx{ parsedType };

		for (const ParsedMacroEntry& entry : parsedType.RawArguments.Specifiers)
		{
			const SpecifierDefinition* def =
				SpecifierRegistry::Find(EMacroEntrySource::Specifier, entry.Name, ESpecifierTarget::Type);

			if (!def)
			{
				throw std::runtime_error(
					"Unsupported NYX_REFLECT specifier on type '" + parsedType.Name + "': " + entry.Name
				);
			}

			ValidateEntryValue(entry, def->ValueKind);

			if (def->ApplyType)
			{
				def->ApplyType(ctx, entry);
			}
		}

		for (const ParsedMacroEntry& entry : parsedType.RawArguments.Metadata)
		{
			const SpecifierDefinition* def =
				SpecifierRegistry::Find(EMacroEntrySource::Metadata, entry.Name, ESpecifierTarget::Type);

			if (!def)
			{
				throw std::runtime_error(
					"Unsupported NYX_REFLECT metadata on type '" + parsedType.Name + "': " + entry.Name
				);
			}

			ValidateEntryValue(entry, def->ValueKind);

			if (def->ApplyType)
			{
				def->ApplyType(ctx, entry);
			}
		}
	}

	void ReflectionSemantics::ApplyPropertySemantics(ParsedProperty& parsedProperty)
	{
		parsedProperty.DisplayName = parsedProperty.Name;
		parsedProperty.KindExpr = MapTypeToKind(parsedProperty.Type);
		parsedProperty.FlagsExpr = "EPropertyFlags::None";

		PropertySemanticContext ctx{ parsedProperty };

		for (const ParsedMacroEntry& entry : parsedProperty.RawArguments.Specifiers)
		{
			const SpecifierDefinition* def =
				SpecifierRegistry::Find(EMacroEntrySource::Specifier, entry.Name, ESpecifierTarget::Property);

			if (!def)
			{
				throw std::runtime_error(
					"Unsupported NYX_PROPERTY specifier on property '" + parsedProperty.Name + "': " + entry.Name
				);
			}

			ValidateEntryValue(entry, def->ValueKind);

			if (def->ApplyProperty)
			{
				def->ApplyProperty(ctx, entry);
			}
		}

		for (const ParsedMacroEntry& entry : parsedProperty.RawArguments.Metadata)
		{
			const SpecifierDefinition* def =
				SpecifierRegistry::Find(EMacroEntrySource::Metadata, entry.Name, ESpecifierTarget::Property);

			if (!def)
			{
				throw std::runtime_error(
					"Unsupported NYX_PROPERTY metadata on property '" + parsedProperty.Name + "': " + entry.Name
				);
			}

			ValidateEntryValue(entry, def->ValueKind);

			if (def->ApplyProperty)
			{
				def->ApplyProperty(ctx, entry);
			}
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

	void ReflectionSemantics::ValidateEntryValue(const ParsedMacroEntry& entry, ESpecifierValueKind valueKind)
	{
		switch (valueKind)
		{
		case ESpecifierValueKind::None:
			if (entry.Value.has_value())
			{
				throw std::runtime_error("Specifier '" + entry.Name + "' must not have a value.");
			}
			return;

		case ESpecifierValueKind::RequiredString:
		case ESpecifierValueKind::RequiredIdentifier:
		case ESpecifierValueKind::RequiredNumber:
			if (!entry.Value.has_value())
			{
				throw std::runtime_error("Specifier '" + entry.Name + "' requires a value.");
			}
			return;

		case ESpecifierValueKind::OptionalString:
			return;
		}
	}
}