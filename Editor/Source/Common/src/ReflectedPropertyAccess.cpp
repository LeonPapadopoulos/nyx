#include "ReflectedPropertyAccess.h"

#include "PropertyValueAccess.h"
#include "ReflectedArchiveSerializer.h"
#include "ReflectionUtils.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <map>
#include <memory>
#include <variant>

namespace Nyx::Editor
{
	Nyx::Reflection::PropertyValue ReadReflectedPropertyValue(
		const void* object,
		const Nyx::Reflection::PropertyMetadata& property)
	{
		Nyx::Reflection::PropertyValue value = Nyx::Reflection::GetPropertyValue(object, property);

		// The editor compares and records rotations normalized
		if (glm::quat* rotation = std::get_if<glm::quat>(&value))
		{
			*rotation = glm::normalize(*rotation);
		}

		return value;
	}

	void WriteReflectedPropertyValue(
		void* object,
		const Nyx::Reflection::PropertyMetadata& property,
		const Nyx::Reflection::PropertyValue& value)
	{
		if (const glm::quat* rotation = std::get_if<glm::quat>(&value))
		{
			Nyx::Reflection::SetPropertyValue(object, property, glm::normalize(*rotation));
			return;
		}

		Nyx::Reflection::SetPropertyValue(object, property, value);
	}

	const Nyx::Reflection::TypeMetadata* GetArrayElementType(const Nyx::Reflection::ArrayMetadata& array)
	{
		if (const Nyx::Reflection::TypeMetadata* structType = Nyx::Reflection::TryGetNestedType(array.Element))
		{
			return structType;
		}

		// One type per array property, made the first time it is asked for, so changes can
		// compare it by address
		static std::map<const Nyx::Reflection::ArrayMetadata*, std::unique_ptr<Nyx::Reflection::TypeMetadata>> elementTypes;
		std::unique_ptr<Nyx::Reflection::TypeMetadata>& type = elementTypes[&array];
		if (!type)
		{
			type = std::make_unique<Nyx::Reflection::TypeMetadata>();
			type->Name = "Element";
			type->DisplayName = "Element";
			type->Properties = &array.Element;
			type->PropertyCount = 1;
		}
		return type.get();
	}

	ResolvedObject ResolvePropertyPath(
		void* subobject,
		const Nyx::Reflection::TypeMetadata& subobjectType,
		const std::vector<size_t>& propertyIndices)
	{
		void* object = subobject;
		const Nyx::Reflection::TypeMetadata* type = &subobjectType;

		for (size_t step = 0; step < propertyIndices.size(); ++step)
		{
			const size_t propertyIndex = propertyIndices[step];
			if (!object || !type->Properties || propertyIndex >= type->PropertyCount)
			{
				return {};
			}

			const Nyx::Reflection::PropertyMetadata& property = type->Properties[propertyIndex];
			if (const Nyx::Reflection::ArrayMetadata* array = Nyx::Reflection::TryGetArray(property))
			{
				// The next index is the element's
				void* arrayAddress = Nyx::Reflection::GetPropertyAddress(object, property);
				if (step + 1 >= propertyIndices.size() || propertyIndices[step + 1] >= array->GetSize(arrayAddress))
				{
					return {};
				}
				object = array->GetElement(arrayAddress, propertyIndices[++step]);
				type = GetArrayElementType(*array);
				continue;
			}

			const Nyx::Reflection::TypeMetadata* nestedType = Nyx::Reflection::TryGetNestedType(property);
			if (!nestedType)
			{
				return {};
			}
			object = Nyx::Reflection::GetPropertyAddress(object, property);
			type = nestedType;
		}

		return { object, type };
	}

	std::vector<std::byte> SaveReflectedProperty(
		const void* object,
		const Nyx::Reflection::TypeMetadata& typeMetadata,
		size_t propertyIndex)
	{
		Nyx::BinaryWriter writer;
		Nyx::ReflectedArchiveSerializer::SerializeProperties(writer, object, typeMetadata, { propertyIndex });
		return writer.GetBytes();
	}

	bool LoadReflectedProperty(
		void* object,
		const Nyx::Reflection::TypeMetadata& typeMetadata,
		const std::vector<std::byte>& bytes)
	{
		Nyx::BinaryReader reader;
		reader.LoadFromMemory(bytes);
		Nyx::ReadWarnings warnings;
		return Nyx::ReflectedArchiveSerializer::DeserializeObject(reader, object, typeMetadata, warnings);
	}
}
