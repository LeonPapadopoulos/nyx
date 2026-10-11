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
		DeleteObject,
		AddSubobject,
		RemoveSubobject
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

	// A subobject (component) added to a root object (entity); redo adds it again as it was
	// right after being added
	struct AddSubobjectChange
	{
		ObjectRef Target{};
		SubobjectSnapshot AfterAdd;
	};

	// A subobject removed from a root object; undo brings it back as it was
	struct RemoveSubobjectChange
	{
		ObjectRef Target{};
		SubobjectSnapshot BeforeRemove;
	};

	using ChangePayload = std::variant<
		SetValueChange,
		AddObjectChange,
		DeleteObjectChange,
		AddSubobjectChange,
		RemoveSubobjectChange>;

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