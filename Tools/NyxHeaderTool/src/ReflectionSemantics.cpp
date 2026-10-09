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

			AssignPropertyNameHashes(parsedType);
		}
	}

	void ReflectionSemantics::AssignPropertyNameHashes(ParsedType& parsedType)
	{
		// Scene files and messages identify properties by these hashes instead of their names,
		// so two properties of one type must never share a hash.
		std::unordered_map<uint32_t, const ParsedProperty*> propertyByHash;

		for (ParsedProperty& parsedProperty : parsedType.Properties)
		{
			parsedProperty.NameHash = HashPropertyName(parsedProperty.Name);

			const auto [existing, bInserted] = propertyByHash.emplace(parsedProperty.NameHash, &parsedProperty);
			if (!bInserted)
			{
				throw std::runtime_error(
					"Properties '" + existing->second->Name + "' and '" + parsedProperty.Name + "' of type '" +
					parsedType.Name + "' have the same name hash, so saved data couldn't tell them apart. Rename one of them.");
			}
		}
	}

	uint32_t ReflectionSemantics::HashPropertyName(std::string_view name)
	{
		// 32-bit FNV-1a
		uint32_t hash = 2166136261u;

		for (const char c : name)
		{
			hash ^= static_cast<uint8_t>(c);
			hash *= 16777619u;
		}

		return hash;
	}

	void ReflectionSemantics::ResolvePropertyType(
		ParsedProperty& parsedProperty,
		const ReflectedTypeIndex& typeIndex)
	{
		static const std::unordered_map<std::string, EParsedPropertyKind> PrimitiveKinds = {
			{ "bool", EParsedPropertyKind::Bool },
			{ "int32_t", EParsedPropertyKind::Int32 },
			{ "uint32_t", EParsedPropertyKind::UInt32 },
			{ "uint64_t", EParsedPropertyKind::UInt64 },
			{ "float", EParsedPropertyKind::Float },
			{ "glm::vec2", EParsedPropertyKind::Vec2 },
			{ "glm::vec3", EParsedPropertyKind::Vec3 },
			{ "glm::vec4", EParsedPropertyKind::Vec4 },
			{ "glm::quat", EParsedPropertyKind::Quat },
			{ "std::string", EParsedPropertyKind::String }
		};

		// Engine types that are saved and edited as the single value inside them, while C++ code
		// keeps a type of its own for them. Since they are recognised by name, the generated code
		// checks that the member really has the engine type and not another type of the same name.
		struct WrappedValueType
		{
			EParsedPropertyKind Kind;
			const char* CppType;
		};

		static const std::unordered_map<std::string, WrappedValueType> WrappedValueTypes = {
			{ "EntityGuid", { EParsedPropertyKind::UInt64, "Nyx::Engine::EntityGuid" } },
			{ "Nyx::Engine::EntityGuid", { EParsedPropertyKind::UInt64, "Nyx::Engine::EntityGuid" } }
		};

		parsedProperty.StructQualifiedTypeName.clear();
		parsedProperty.RequiredCppType.clear();

		if (const auto it = PrimitiveKinds.find(parsedProperty.Type); it != PrimitiveKinds.end())
		{
			parsedProperty.Kind = it->second;
			return;
		}

		if (const auto it = WrappedValueTypes.find(parsedProperty.Type); it != WrappedValueTypes.end())
		{
			parsedProperty.Kind = it->second.Kind;
			parsedProperty.RequiredCppType = it->second.CppType;
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
					"Unsupported NYX_REFLECT specifier on type '" + parsedType.Name + "': " + entry.Name);
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
					"Unsupported NYX_REFLECT metadata on type '" + parsedType.Name + "': " + entry.Name);
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
					"Unsupported NYX_PROPERTY specifier on property '" + parsedProperty.Name + "': " + entry.Name);
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
					"Unsupported NYX_PROPERTY metadata on property '" + parsedProperty.Name + "': " + entry.Name);
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