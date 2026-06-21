#pragma once

#include "ReflectionAst.h"

#include <optional>
#include <string_view>

namespace Nyx::HeaderTool
{
	enum class EMacroEntrySource : uint8_t
	{
		Specifier = 0,
		Metadata
	};

	enum class ESpecifierTarget : uint8_t
	{
		Type = 1 << 0,
		Property = 1 << 1
	};

	inline constexpr uint8_t operator|(ESpecifierTarget a, ESpecifierTarget b)
	{
		return static_cast<uint8_t>(a) | static_cast<uint8_t>(b);
	}

	enum class ESpecifierValueKind : uint8_t
	{
		None = 0,
		RequiredString,
		RequiredIdentifier,
		RequiredNumber,
		OptionalString
	};

	struct TypeSemanticContext
	{
		ParsedType& Type;

		void AddMetadata(std::string key, std::string value);
		void SetRole(EParsedTypeRole role);
	};

	struct PropertySemanticContext
	{
		ParsedProperty& Property;

		void AddMetadata(std::string key, std::string value);
		void AddFlag(EParsedPropertyFlags flag);
	};

	using ApplyTypeFn = void(*)(TypeSemanticContext&, const ParsedMacroEntry&);
	using ApplyPropertyFn = void(*)(PropertySemanticContext&, const ParsedMacroEntry&);

	struct SpecifierDefinition
	{
		const char* Name = "";
		EMacroEntrySource Source = EMacroEntrySource::Specifier;
		uint8_t TargetMask = 0;
		ESpecifierValueKind ValueKind = ESpecifierValueKind::None;

		ApplyTypeFn ApplyType = nullptr;
		ApplyPropertyFn ApplyProperty = nullptr;
	};

	class SpecifierRegistry
	{
	public:
		static const SpecifierDefinition* Find(
			EMacroEntrySource source,
			std::string_view name,
			ESpecifierTarget target);

	private:
		static const SpecifierDefinition Definitions[];
		static const size_t DefinitionCount;
	};
}