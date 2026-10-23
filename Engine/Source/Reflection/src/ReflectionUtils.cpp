#include "ReflectionUtils.h"

#include <cstdio>
#include <cstring>

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

	const PropertyMetadata* FindPropertyByNameHash(
		const TypeMetadata& type,
		uint32_t nameHash)
	{
		if (!type.Properties)
		{
			return nullptr;
		}

		for (size_t i = 0; i < type.PropertyCount; ++i)
		{
			if (type.Properties[i].NameHash == nameHash)
			{
				return &type.Properties[i];
			}
		}

		return nullptr;
	}

	std::string NameHashToText(uint32_t nameHash)
	{
		char text[16];
		std::snprintf(text, sizeof(text), "0x%08X", nameHash);
		return text;
	}

	bool IsKnownPropertyKind(EPropertyKind kind)
	{
		// The kinds are numbered without gaps, from Bool to LastPropertyKind
		return kind >= EPropertyKind::Bool && kind <= LastPropertyKind;
	}

	const char* GetPropertyKindName(EPropertyKind kind)
	{
		switch (kind)
		{
		case EPropertyKind::Bool:   return "Bool";
		case EPropertyKind::Int32:  return "Int32";
		case EPropertyKind::UInt32: return "UInt32";
		case EPropertyKind::UInt64: return "UInt64";
		case EPropertyKind::Float:  return "Float";
		case EPropertyKind::Vec2:   return "Vec2";
		case EPropertyKind::Vec3:   return "Vec3";
		case EPropertyKind::Vec4:   return "Vec4";
		case EPropertyKind::Quat:   return "Quat";
		case EPropertyKind::String: return "String";
		case EPropertyKind::Struct: return "Struct";
		case EPropertyKind::Enum:   return "Enum";
		case EPropertyKind::Array:  return "Array";
		default:                    return "Unknown";
		}
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

	const TypeMetadata* TryGetNestedType(const PropertyMetadata& property)
	{
		if (!property.NestedTypeResolver)
		{
			return nullptr;
		}

		return &property.NestedTypeResolver();
	}

	bool IsStructProperty(const PropertyMetadata& property)
	{
		return property.Kind == EPropertyKind::Struct && property.NestedTypeResolver != nullptr;
	}

	const EnumMetadata* TryGetEnum(const PropertyMetadata& property)
	{
		return property.EnumResolver ? &property.EnumResolver() : nullptr;
	}

	int64_t ReadEnumValue(const void* address, const EnumMetadata& enumMetadata)
	{
		switch (enumMetadata.Size)
		{
		case 1:
		{
			uint8_t value = 0;
			std::memcpy(&value, address, 1);
			return enumMetadata.bSigned ? static_cast<int64_t>(static_cast<int8_t>(value)) : value;
		}
		case 2:
		{
			uint16_t value = 0;
			std::memcpy(&value, address, 2);
			return enumMetadata.bSigned ? static_cast<int64_t>(static_cast<int16_t>(value)) : value;
		}
		case 4:
		{
			uint32_t value = 0;
			std::memcpy(&value, address, 4);
			return enumMetadata.bSigned ? static_cast<int64_t>(static_cast<int32_t>(value)) : value;
		}
		case 8:
		{
			int64_t value = 0;
			std::memcpy(&value, address, 8);
			return value;
		}
		default:
			return 0;
		}
	}

	void WriteEnumValue(void* address, const EnumMetadata& enumMetadata, int64_t value)
	{
		// Little-endian: the low bytes are the value in the enum's size
		if (enumMetadata.Size == 1 || enumMetadata.Size == 2 || enumMetadata.Size == 4 || enumMetadata.Size == 8)
		{
			std::memcpy(address, &value, enumMetadata.Size);
		}
	}

	const EnumEntry* FindEnumEntry(const EnumMetadata& enumMetadata, int64_t value)
	{
		for (size_t i = 0; i < enumMetadata.EntryCount; ++i)
		{
			if (enumMetadata.Entries[i].Value == value)
			{
				return &enumMetadata.Entries[i];
			}
		}
		return nullptr;
	}

	const EnumEntry* FindEnumEntryByName(const EnumMetadata& enumMetadata, std::string_view name)
	{
		for (size_t i = 0; i < enumMetadata.EntryCount; ++i)
		{
			if (enumMetadata.Entries[i].Name == name)
			{
				return &enumMetadata.Entries[i];
			}
		}
		return nullptr;
	}

	std::string EnumValueToText(const EnumMetadata& enumMetadata, int64_t value)
	{
		const EnumEntry* entry = FindEnumEntry(enumMetadata, value);
		return entry ? entry->Name : std::to_string(value);
	}

	const ArrayMetadata* TryGetArray(const PropertyMetadata& property)
	{
		return property.Kind == EPropertyKind::Array ? property.Array : nullptr;
	}

	size_t GetArraySize(const void* object, const PropertyMetadata& property)
	{
		const ArrayMetadata* array = TryGetArray(property);
		return array && object ? array->GetSize(GetPropertyAddress(object, property)) : 0;
	}
}