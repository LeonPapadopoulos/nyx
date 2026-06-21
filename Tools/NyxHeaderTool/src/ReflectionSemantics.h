#pragma once

#include "ReflectionAst.h"
#include "SpecifierRegistry.h"

namespace Nyx::HeaderTool
{
	class ReflectionSemantics
	{
	public:
		static void Apply(ParsedHeader& parsedHeader);

	private:
		static void ApplyTypeSemantics(ParsedType& parsedType);
		static void ApplyPropertySemantics(ParsedProperty& parsedProperty);

		static EParsedPropertyKind MapTypeToKind(const std::string& typeName);
		static void ValidateEntryValue(const ParsedMacroEntry& entry, ESpecifierValueKind valueKind);
	};
}