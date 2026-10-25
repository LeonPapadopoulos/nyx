#include "NyxPCH.h"
#include "ValueOverride.h"

#include <algorithm>

namespace Nyx
{
	float OverridableValue::Clamp(float value) const
	{
		return std::clamp(value, Min, std::max(Min, Max));
	}

	const ValueOverride* FindValueOverride(const std::vector<ValueOverride>& overrides, std::string_view name)
	{
		for (const ValueOverride& entry : overrides)
		{
			if (entry.Name == name)
			{
				return &entry;
			}
		}
		return nullptr;
	}

	float GetOverriddenValue(const std::vector<ValueOverride>& overrides, std::string_view name, float inherited)
	{
		const ValueOverride* entry = FindValueOverride(overrides, name);
		return entry ? entry->Value : inherited;
	}
}
