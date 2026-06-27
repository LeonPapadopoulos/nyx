#pragma once

#include "ReflectionAst.h"
#include "SpecifierRegistry.h"
#include "ReflectedTypeIndex.h"

namespace Nyx::HeaderTool
{
	class ReflectionSemantics
	{
	public:
		static void Apply(ParsedHeader& parsedHeader, const ReflectedTypeIndex& typeIndex);

	private:
		static void ApplyTypeSemantics(ParsedType& parsedType);
		static void ApplyPropertySemantics(ParsedProperty& parsedProperty);

		static void ResolvePropertyType(ParsedProperty& parsedProperty, const ReflectedTypeIndex& typeIndex);
		static void ValidateEntryValue(const ParsedMacroEntry& entry, ESpecifierValueKind valueKind);
	};
}