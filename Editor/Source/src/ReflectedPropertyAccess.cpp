#include "ReflectedPropertyAccess.h"

#include "PropertyValueAccess.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

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
}
