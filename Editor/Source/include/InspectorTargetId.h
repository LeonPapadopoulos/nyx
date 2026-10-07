#pragma once

#include <cstdint>

namespace Nyx::Editor
{
	struct InspectorTargetId
	{
		uint64_t Value = 0;

		bool operator==(const InspectorTargetId&) const = default;
		bool operator!=(const InspectorTargetId&) const = default;
	};
}