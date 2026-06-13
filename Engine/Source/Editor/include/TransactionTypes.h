#pragma once

#include "PropertyValue.h"
#include "ReflectionTypes.h"
#include "SceneEntitySnapshot.h"
#include "TransactionObjectRef.h"

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
		ObjectRef Target{};
		const Nyx::Reflection::TypeMetadata* TypeMetadata = nullptr;
		size_t PropertyIndex = 0;
		Nyx::Reflection::PropertyValue Before;
		Nyx::Reflection::PropertyValue After;
	};

	struct AddObjectChange
	{
		ObjectRef Target{};
		std::variant<std::monostate, SceneEntitySnapshot> AfterCreate;
	};

	struct DeleteObjectChange
	{
		ObjectRef Target{};
		std::variant<std::monostate, SceneEntitySnapshot> BeforeDelete;
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