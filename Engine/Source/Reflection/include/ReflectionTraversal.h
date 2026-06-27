#pragma once

#include "ReflectedPropertyRef.h"
#include "ReflectionUtils.h"

#include <vector>

namespace Nyx::Reflection
{
	struct PropertyPathSegment
	{
		const TypeMetadata* OwnerType = nullptr;
		const PropertyMetadata* Property = nullptr;
		void* OwnerObject = nullptr;
	};

	struct PropertyPathView
	{
		const std::vector<PropertyPathSegment>* Segments = nullptr;
	};

	template<typename Visitor>
	static void VisitReflectedPropertiesRecursiveImpl(
		void* object,
		const TypeMetadata& type,
		std::vector<PropertyPathSegment>& path,
		Visitor&& visitor)
	{
		for (size_t i = 0; i < type.PropertyCount; ++i)
		{
			const PropertyMetadata& property = type.Properties[i];

			path.push_back(PropertyPathSegment{
				.OwnerType = &type,
				.Property = &property,
				.OwnerObject = object
				});

			ReflectedPropertyRef propertyRef =
				MakeReflectedPropertyRef(object, type, property);

			PropertyPathView pathView{ .Segments = &path };

			if constexpr (std::is_same_v<decltype(visitor(propertyRef, pathView)), bool>)
			{
				if (!visitor(propertyRef, pathView))
				{
					path.pop_back();
					return;
				}
			}
			else
			{
				visitor(propertyRef, pathView);
			}

			if (const TypeMetadata* nestedType = TryGetNestedType(property))
			{
				void* nestedObject = GetPropertyAddress(object, property);
				VisitReflectedPropertiesRecursiveImpl(nestedObject, *nestedType, path, visitor);
			}

			path.pop_back();
		}
	}

	template<typename Visitor>
	void VisitReflectedPropertiesRecursive(
		void* object,
		const TypeMetadata& type,
		Visitor&& visitor)
	{
		std::vector<PropertyPathSegment> path;
		VisitReflectedPropertiesRecursiveImpl(object, type, path, std::forward<Visitor>(visitor));
	}
}