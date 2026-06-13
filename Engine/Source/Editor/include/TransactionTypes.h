#pragma once

#include "InspectorTargetId.h"
#include "PropertyValue.h"
#include "ReflectionTypes.h"

#include <string>
#include <variant>
#include <vector>

namespace Nyx::Editor
{
	enum class EChangeKind : uint8_t
	{
		SetValue = 0,
		AddObject,
		DeleteObject
	};

	struct SetValueChange
	{
		InspectorTargetId TargetId{};
		const Nyx::Reflection::TypeMetadata* TypeMetadata = nullptr;
		size_t PropertyIndex = 0;
		Nyx::Reflection::PropertyValue Before;
		Nyx::Reflection::PropertyValue After;
	};

	// Placeholders for the next step.
	struct AddObjectChange
	{
		InspectorTargetId TargetId{};
		const Nyx::Reflection::TypeMetadata* TypeMetadata = nullptr;
	};

	struct DeleteObjectChange
	{
		InspectorTargetId TargetId{};
		const Nyx::Reflection::TypeMetadata* TypeMetadata = nullptr;
	};

	using ChangePayload = std::variant<
		SetValueChange,
		AddObjectChange,
		DeleteObjectChange
	>;

	struct Change
	{
		EChangeKind Kind = EChangeKind::SetValue;
		ChangePayload Payload{};
	};

	struct Transaction
	{
		std::string Label;
		std::vector<Change> Changes;

		bool IsEmpty() const
		{
			return Changes.empty();
		}
	};
}