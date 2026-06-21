#pragma once

#include "ReflectionTypes.h"
#include "ReflectionUtils.h"

#include <cassert>
#include <string_view>
#include <type_traits>

namespace Nyx::Reflection
{
	struct ReflectedPropertyRef
	{
		void* Object = nullptr;
		const TypeMetadata* OwnerType = nullptr;
		const PropertyMetadata* Property = nullptr;

		bool IsValid() const
		{
			return Object != nullptr && OwnerType != nullptr && Property != nullptr;
		}

		void* GetPropertyAddress() const
		{
			return IsValid() ? Nyx::Reflection::GetPropertyAddress(Object, *Property) : nullptr;
		}

		template<typename T>
		T& Access() const
		{
			assert(IsValid());
			return Nyx::Reflection::AccessProperty<T>(Object, *Property);
		}

		const char* FindMetadataValue(std::string_view key) const
		{
			return IsValid() ? Nyx::Reflection::FindMetadataValue(*Property, key) : nullptr;
		}

		bool HasMetadata(std::string_view key) const
		{
			return IsValid() ? Nyx::Reflection::HasMetadata(*Property, key) : false;
		}

		std::string_view GetPropertyName() const
		{
			return IsValid() ? std::string_view(Property->Name) : std::string_view{};
		}

		std::string_view GetDisplayName() const
		{
			if (!IsValid())
			{
				return {};
			}

			return (Property->DisplayName && Property->DisplayName[0] != '\0')
				? std::string_view(Property->DisplayName)
				: std::string_view(Property->Name);
		}
	};

	struct ConstReflectedPropertyRef
	{
		const void* Object = nullptr;
		const TypeMetadata* OwnerType = nullptr;
		const PropertyMetadata* Property = nullptr;

		bool IsValid() const
		{
			return Object != nullptr && OwnerType != nullptr && Property != nullptr;
		}

		const void* GetPropertyAddress() const
		{
			return IsValid() ? Nyx::Reflection::GetPropertyAddress(Object, *Property) : nullptr;
		}

		template<typename T>
		const T& Access() const
		{
			assert(IsValid());
			return Nyx::Reflection::AccessProperty<T>(Object, *Property);
		}

		const char* FindMetadataValue(std::string_view key) const
		{
			return IsValid() ? Nyx::Reflection::FindMetadataValue(*Property, key) : nullptr;
		}

		bool HasMetadata(std::string_view key) const
		{
			return IsValid() ? Nyx::Reflection::HasMetadata(*Property, key) : false;
		}

		std::string_view GetPropertyName() const
		{
			return IsValid() ? std::string_view(Property->Name) : std::string_view{};
		}
	};

	inline ReflectedPropertyRef MakeReflectedPropertyRef(
		void* object,
		const TypeMetadata& ownerType,
		const PropertyMetadata& property)
	{
		return ReflectedPropertyRef{
			.Object = object,
			.OwnerType = &ownerType,
			.Property = &property
		};
	}

	inline ConstReflectedPropertyRef MakeConstReflectedPropertyRef(
		const void* object,
		const TypeMetadata& ownerType,
		const PropertyMetadata& property)
	{
		return ConstReflectedPropertyRef{
			.Object = object,
			.OwnerType = &ownerType,
			.Property = &property
		};
	}

	inline ReflectedPropertyRef FindReflectedProperty(
		void* object,
		const TypeMetadata& ownerType,
		std::string_view propertyName)
	{
		const PropertyMetadata* property = FindPropertyByName(ownerType, propertyName);
		if (!property)
		{
			return {};
		}

		return MakeReflectedPropertyRef(object, ownerType, *property);
	}

	inline ConstReflectedPropertyRef FindReflectedProperty(
		const void* object,
		const TypeMetadata& ownerType,
		std::string_view propertyName)
	{
		const PropertyMetadata* property = FindPropertyByName(ownerType, propertyName);
		if (!property)
		{
			return {};
		}

		return MakeConstReflectedPropertyRef(object, ownerType, *property);
	}

	template<typename TObject>
	inline ReflectedPropertyRef FindReflectedProperty(
		TObject& object,
		std::string_view propertyName)
	{
		const TypeMetadata& type = GetTypeMetadata<TObject>();
		return FindReflectedProperty(&object, type, propertyName);
	}

	template<typename TObject>
	inline ConstReflectedPropertyRef FindReflectedProperty(
		const TObject& object,
		std::string_view propertyName)
	{
		const TypeMetadata& type = GetTypeMetadata<TObject>();
		return FindReflectedProperty(&object, type, propertyName);
	}
}