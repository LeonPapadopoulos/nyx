#pragma once

#include "ReflectionMacros.h"

#include <string>
#include <vector>

namespace Nyx
{
	// Written on one line in an asset's text: "Range.Speed = 2 0 5"
	NYX_REFLECT(meta = (Inline))
	struct TextRange
	{
		NYX_PROPERTY(Edit, Undo, Serialize)
		std::string Name;

		NYX_PROPERTY(Edit, Undo, Serialize)
		float Default = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize)
		float Min = 0.0f;
	};

	NYX_REFLECT()
	struct TextMetadataTest
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (TextName = Range, Tooltip = "Lines named Range.<Name>"))
		std::vector<TextRange> Ranges;
	};
}

#include "Generated/Runtime/Input.reflect.h"
