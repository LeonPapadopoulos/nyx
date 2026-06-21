#pragma once

#include "ReflectionTypes.h"

#include <cassert>
#include <cstddef>
#include <cstring>
#include <optional>
#include <string_view>
#include <type_traits>

namespace Nyx::Reflection
{
	// ------------------------------------------------------------
	// Metadata lookup
	// ------------------------------------------------------------

	const MetadataEntry* FindMetadataEntry(
		const MetadataEntry* entries,
		size_t count,
		std::string_view key);

	const MetadataEntry* FindMetadataEntry(
		const PropertyMetadata& property,
		std::string_view key);

	const MetadataEntry* FindMetadataEntry(
		const TypeMetadata& type,
		std::string_view key);

	const char* FindMetadataValue(
		const MetadataEntry* entries,
		size_t count,
		std::string_view key);

	const char* FindMetadataValue(
		const PropertyMetadata& property,
		std::string_view key);

	const char* FindMetadataValue(
		const TypeMetadata& type,
		std::string_view key);

	bool HasMetadata(
		const PropertyMetadata& property,
		std::string_view key);

	bool HasMetadata(
		const TypeMetadata& type,
		std::string_view key);

	// ------------------------------------------------------------
	// Property lookup
	// ------------------------------------------------------------

	const PropertyMetadata* FindPropertyByName(
		const TypeMetadata& type,
		std::string_view propertyName);

	std::optional<size_t> FindPropertyIndexByName(
		const TypeMetadata& type,
		std::string_view propertyName);

	// ------------------------------------------------------------
	// Raw address access
	// ------------------------------------------------------------

	void* GetPropertyAddress(
		void* object,
		const PropertyMetadata& property);

	const void* GetPropertyAddress(
		const void* object,
		const PropertyMetadata& property);

	// ------------------------------------------------------------
	// Typed access helpers
	// ------------------------------------------------------------

	template<typename T>
	T& AccessByOffset(void* object, size_t offset)
	{
		assert(object != nullptr);
		return *reinterpret_cast<T*>(static_cast<std::byte*>(object) + offset);
	}

	template<typename T>
	const T& AccessByOffset(const void* object, size_t offset)
	{
		assert(object != nullptr);
		return *reinterpret_cast<const T*>(static_cast<const std::byte*>(object) + offset);
	}

	template<typename T>
	T& AccessProperty(void* object, const PropertyMetadata& property)
	{
		return AccessByOffset<T>(object, property.Offset);
	}

	template<typename T>
	const T& AccessProperty(const void* object, const PropertyMetadata& property)
	{
		return AccessByOffset<T>(object, property.Offset);
	}

	template<typename T>
	T* GetPropertyPointer(void* object, const PropertyMetadata& property)
	{
		assert(object != nullptr);
		return reinterpret_cast<T*>(static_cast<std::byte*>(object) + property.Offset);
	}

	template<typename T>
	const T* GetPropertyPointer(const void* object, const PropertyMetadata& property)
	{
		assert(object != nullptr);
		return reinterpret_cast<const T*>(static_cast<const std::byte*>(object) + property.Offset);
	}

	// ------------------------------------------------------------
	// Value-copy helpers for trivially copyable types
	// ------------------------------------------------------------

	template<typename T>
	void WritePropertyValue(void* object, const PropertyMetadata& property, const T& value)
	{
		static_assert(std::is_trivially_copyable_v<T>,
			"WritePropertyValue<T> only supports trivially copyable types. "
			"For non-trivial types, assign through AccessProperty<T>().");

		T& destination = AccessProperty<T>(object, property);
		destination = value;
	}

	template<typename T>
	T ReadPropertyValue(const void* object, const PropertyMetadata& property)
	{
		static_assert(std::is_trivially_copyable_v<T>,
			"ReadPropertyValue<T> only supports trivially copyable types.");

		return AccessProperty<T>(object, property);
	}

	// ------------------------------------------------------------
	// Convenience helpers
	// ------------------------------------------------------------

	template<typename TObject>
	const PropertyMetadata* FindPropertyByName(std::string_view propertyName)
	{
		return FindPropertyByName(GetTypeMetadata<TObject>(), propertyName);
	}

	template<typename TObject, typename TProperty>
	TProperty* FindPropertyPointer(
		TObject& object,
		std::string_view propertyName)
	{
		const PropertyMetadata* property = FindPropertyByName<TObject>(propertyName);
		if (!property)
		{
			return nullptr;
		}

		return GetPropertyPointer<TProperty>(&object, *property);
	}

	template<typename TObject, typename TProperty>
	const TProperty* FindPropertyPointer(
		const TObject& object,
		std::string_view propertyName)
	{
		const PropertyMetadata* property = FindPropertyByName<TObject>(propertyName);
		if (!property)
		{
			return nullptr;
		}

		return GetPropertyPointer<TProperty>(&object, *property);
	}
}