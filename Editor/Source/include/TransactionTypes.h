#pragma once

#include "PropertyValue.h"
#include "ReflectionTypes.h"
#include "ReflectedObjectSnapshot.h"
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

	// Where an object inside a root object is: in which subobject (component), and through which
	// struct properties of it, e.g. MeshRenderer, then its property Mesh, for the AssetReference
	// that holds a mesh path. Without a subobject type, the object is a subobject itself.
	struct SubobjectPath
	{
		const Nyx::Reflection::TypeMetadata* SubobjectType = nullptr;

		// Indices into the Properties of each type on the way, starting with SubobjectType's
		std::vector<size_t> PropertyIndices;

		bool operator==(const SubobjectPath&) const = default;
	};

	struct SetValueChange
	{
		ObjectRef Target{};

		// The type the property belongs to: a component, or a struct inside one (see Location)
		const Nyx::Reflection::TypeMetadata* TypeMetadata = nullptr;
		size_t PropertyIndex = 0;
		Nyx::Reflection::PropertyValue Before;
		Nyx::Reflection::PropertyValue After;

		// Set when TypeMetadata is a struct inside a component, so undo can find that struct
		SubobjectPath Location;
	};

	struct AddObjectChange
	{
		ObjectRef Target{};
		RootObjectSnapshot AfterCreate;
	};

	struct DeleteObjectChange
	{
		ObjectRef Target{};
		RootObjectSnapshot BeforeDelete;
	};

	using ChangePayload = std::variant<
		SetValueChange,
		AddObjectChange,
		DeleteObjectChange>;

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