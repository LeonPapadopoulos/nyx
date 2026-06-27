#include "ReflectionSemantics.h"

#include <stdexcept>
#include <unordered_map>

namespace Nyx::HeaderTool
{
	void ReflectionSemantics::Apply(ParsedHeader& parsedHeader, const ReflectedTypeIndex& typeIndex)
	{
		for (ParsedType& parsedType : parsedHeader.Types)
		{
			ApplyTypeSemantics(parsedType);

			for (ParsedProperty& parsedProperty : parsedType.Properties)
			{
				parsedProperty.DisplayName = parsedProperty.Name;
				parsedProperty.Flags = EParsedPropertyFlags::None;

				ResolvePropertyType(parsedProperty, typeIndex);
				ApplyPropertySemantics(parsedProperty);
			}
		}
	}

	void ReflectionSemantics::ResolvePropertyType(
		ParsedProperty& parsedProperty,
		const ReflectedTypeIndex& typeIndex)
	{
		static const std::unordered_map<std::string, EParsedPropertyKind> PrimitiveKinds =
		{
			{ "bool", EParsedPropertyKind::Bool },
			{ "int32_t", EParsedPropertyKind::Int32 },
			{ "uint32_t", EParsedPropertyKind::UInt32 },
			{ "float", EParsedPropertyKind::Float },
			{ "glm::vec2", EParsedPropertyKind::Vec2 },
			{ "glm::vec3", EParsedPropertyKind::Vec3 },
			{ "glm::vec4", EParsedPropertyKind::Vec4 },
			{ "glm::quat", EParsedPropertyKind::Quat },
			{ "std::string", EParsedPropertyKind::String }
		};

		if (const auto it = PrimitiveKinds.find(parsedProperty.Type); it != PrimitiveKinds.end())
		{
			parsedProperty.Kind = it->second;
			parsedProperty.StructQualifiedTypeName.clear();
			return;
		}

		if (const std::optional<std::string> resolved = typeIndex.Resolve(parsedProperty.Type))
		{
			parsedProperty.Kind = EParsedPropertyKind::Struct;
			parsedProperty.StructQualifiedTypeName = *resolved;
			return;
		}

		throw std::runtime_error("Unsupported reflected property type: " + parsedProperty.Type);
	}

	void ReflectionSemantics::ApplyTypeSemantics(ParsedType& parsedType)
	{
		parsedType.DisplayName = parsedType.Name;
		parsedType.Role = EParsedTypeRole::Plain;

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
		parsedProperty.Flags = EParsedPropertyFlags::None;

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
			if (!entry.Value.has_value() || entry.Value->Kind != EMacroValueKind::String)
			{
				throw std::runtime_error("Specifier '" + entry.Name + "' requires a string value.");
			}
			return;

		case ESpecifierValueKind::RequiredIdentifier:
			if (!entry.Value.has_value() || entry.Value->Kind != EMacroValueKind::Identifier)
			{
				throw std::runtime_error("Specifier '" + entry.Name + "' requires an identifier value.");
			}
			return;

		case ESpecifierValueKind::RequiredNumber:
			if (!entry.Value.has_value() || entry.Value->Kind != EMacroValueKind::Number)
			{
				throw std::runtime_error("Specifier '" + entry.Name + "' requires a numeric value.");
			}
			return;

		case ESpecifierValueKind::OptionalString:
			if (entry.Value.has_value() && entry.Value->Kind != EMacroValueKind::String)
			{
				throw std::runtime_error("Specifier '" + entry.Name + "' requires an optional string value.");
			}
			return;
		}
	}
}