#pragma once

#include "ReflectionAst.h"

namespace Nyx::HeaderTool
{
	class ReflectionSemantics
	{
	public:
		static void Apply(ParsedHeader& parsedHeader);

	private:
		static void ApplyTypeSemantics(ParsedType& parsedType);
		static void ApplyPropertySemantics(ParsedProperty& parsedProperty);

		static std::string MapTypeToKind(const std::string& typeName);
	};
}