#pragma once

#include "PropertyValue.h"
#include "PropertyValueUtils.h"
#include "ReflectionTypes.h"

#include <cstddef>
#include <vector>

namespace Nyx::Editor
{
	// Nyx::Reflection::GetPropertyValue and SetPropertyValue (PropertyValueAccess.h), with
	// rotations (Quat) normalized, as the editor compares and records them.
	Nyx::Reflection::PropertyValue ReadReflectedPropertyValue(
		const void* object,
		const Nyx::Reflection::PropertyMetadata& property);

	void WriteReflectedPropertyValue(
		void* object,
		const Nyx::Reflection::PropertyMetadata& property,
		const Nyx::Reflection::PropertyValue& value);

	// What an array's elements are, as a type: their struct type, or for other elements (numbers,
	// vectors, ...) a type with one property, the element itself, at offset 0
	const Nyx::Reflection::TypeMetadata* GetArrayElementType(const Nyx::Reflection::ArrayMetadata& array);

	// An object inside a subobject, by its path (property indices, as SubobjectPath has them), and
	// its type. Null if the path doesn't fit, e.g. an element that isn't there any more.
	struct ResolvedObject
	{
		void* Object = nullptr;
		const Nyx::Reflection::TypeMetadata* TypeMetadata = nullptr;
	};

	ResolvedObject ResolvePropertyPath(
		void* subobject,
		const Nyx::Reflection::TypeMetadata& subobjectType,
		const std::vector<size_t>& propertyIndices);

	// One property of an object, whole, as a property block, e.g. an array before and after its
	// elements change (ReplaceArrayChange)
	std::vector<std::byte> SaveReflectedProperty(
		const void* object,
		const Nyx::Reflection::TypeMetadata& typeMetadata,
		size_t propertyIndex);

	bool LoadReflectedProperty(
		void* object,
		const Nyx::Reflection::TypeMetadata& typeMetadata,
		const std::vector<std::byte>& bytes);
}