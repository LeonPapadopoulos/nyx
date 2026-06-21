#include "ReflectionUtils.h"

namespace Nyx::Reflection
{
	const MetadataEntry* FindMetadataEntry(
		const MetadataEntry* entries,
		size_t count,
		std::string_view key)
	{
		if (!entries)
		{
			return nullptr;
		}

		for (size_t i = 0; i < count; ++i)
		{
			if (entries[i].Key == key)
			{
				return &entries[i];
			}
		}

		return nullptr;
	}

	const MetadataEntry* FindMetadataEntry(
		const PropertyMetadata& property,
		std::string_view key)
	{
		return FindMetadataEntry(property.Metadata, property.MetadataCount, key);
	}

	const MetadataEntry* FindMetadataEntry(
		const TypeMetadata& type,
		std::string_view key)
	{
		return FindMetadataEntry(type.Metadata, type.MetadataCount, key);
	}

	const char* FindMetadataValue(
		const MetadataEntry* entries,
		size_t count,
		std::string_view key)
	{
		const MetadataEntry* entry = FindMetadataEntry(entries, count, key);
		return entry ? entry->Value : nullptr;
	}

	const char* FindMetadataValue(
		const PropertyMetadata& property,
		std::string_view key)
	{
		return FindMetadataValue(property.Metadata, property.MetadataCount, key);
	}

	const char* FindMetadataValue(
		const TypeMetadata& type,
		std::string_view key)
	{
		return FindMetadataValue(type.Metadata, type.MetadataCount, key);
	}

	bool HasMetadata(
		const PropertyMetadata& property,
		std::string_view key)
	{
		return FindMetadataEntry(property, key) != nullptr;
	}

	bool HasMetadata(
		const TypeMetadata& type,
		std::string_view key)
	{
		return FindMetadataEntry(type, key) != nullptr;
	}

	const PropertyMetadata* FindPropertyByName(
		const TypeMetadata& type,
		std::string_view propertyName)
	{
		if (!type.Properties)
		{
			return nullptr;
		}

		for (size_t i = 0; i < type.PropertyCount; ++i)
		{
			if (type.Properties[i].Name == propertyName)
			{
				return &type.Properties[i];
			}
		}

		return nullptr;
	}

	std::optional<size_t> FindPropertyIndexByName(
		const TypeMetadata& type,
		std::string_view propertyName)
	{
		if (!type.Properties)
		{
			return std::nullopt;
		}

		for (size_t i = 0; i < type.PropertyCount; ++i)
		{
			if (type.Properties[i].Name == propertyName)
			{
				return i;
			}
		}

		return std::nullopt;
	}

	void* GetPropertyAddress(
		void* object,
		const PropertyMetadata& property)
	{
		if (!object)
		{
			return nullptr;
		}

		return static_cast<void*>(static_cast<std::byte*>(object) + property.Offset);
	}

	const void* GetPropertyAddress(
		const void* object,
		const PropertyMetadata& property)
	{
		if (!object)
		{
			return nullptr;
		}

		return static_cast<const void*>(static_cast<const std::byte*>(object) + property.Offset);
	}
}