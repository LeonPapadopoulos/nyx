#pragma once

#include "ReflectionTypes.h"

#include <cassert>
#include <cstddef>
#include <optional>
#include <string_view>
#include <type_traits>

namespace Nyx::Reflection
{
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

	const PropertyMetadata* FindPropertyByName(
		const TypeMetadata& type,
		std::string_view propertyName);

	std::optional<size_t> FindPropertyIndexByName(
		const TypeMetadata& type,
		std::string_view propertyName);

	void* GetPropertyAddress(
		void* object,
		const PropertyMetadata& property);

	const void* GetPropertyAddress(
		const void* object,
		const PropertyMetadata& property);

	const TypeMetadata* TryGetNestedType(const PropertyMetadata& property);
	bool IsStructProperty(const PropertyMetadata& property);

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

	template<typename TObject>
	const PropertyMetadata* FindPropertyByName(std::string_view propertyName)
	{
		return FindPropertyByName(GetTypeMetadata<TObject>(), propertyName);
	}
}