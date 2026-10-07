#include "ReflectedPropertyAccess.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstring>
#include <stdexcept>

namespace
{
	template<typename T>
	const T& ReadByOffset(const void* object, size_t offset)
	{
		return *reinterpret_cast<const T*>(static_cast<const unsigned char*>(object) + offset);
	}

	template<typename T>
	T& WriteByOffset(void* object, size_t offset)
	{
		return *reinterpret_cast<T*>(static_cast<unsigned char*>(object) + offset);
	}
}

namespace Nyx::Editor
{
	Nyx::Reflection::PropertyValue ReadReflectedPropertyValue(
		const void* object,
		const Nyx::Reflection::PropertyMetadata& property)
	{
		using namespace Nyx::Reflection;

		switch (property.Kind)
		{
		case EPropertyKind::Bool:   return ReadByOffset<bool>(object, property.Offset);
		case EPropertyKind::Int32:  return ReadByOffset<int32_t>(object, property.Offset);
		case EPropertyKind::UInt32: return ReadByOffset<uint32_t>(object, property.Offset);
		case EPropertyKind::Float:  return ReadByOffset<float>(object, property.Offset);
		case EPropertyKind::Vec2:   return ReadByOffset<glm::vec2>(object, property.Offset);
		case EPropertyKind::Vec3:   return ReadByOffset<glm::vec3>(object, property.Offset);
		case EPropertyKind::Vec4:   return ReadByOffset<glm::vec4>(object, property.Offset);
		case EPropertyKind::Quat:   return glm::normalize(ReadByOffset<glm::quat>(object, property.Offset));
		case EPropertyKind::String: return ReadByOffset<std::string>(object, property.Offset);
		default:                    return std::monostate{};
		}
	}

	void WriteReflectedPropertyValue(
		void* object,
		const Nyx::Reflection::PropertyMetadata& property,
		const Nyx::Reflection::PropertyValue& value)
	{
		using namespace Nyx::Reflection;

		switch (property.Kind)
		{
		case EPropertyKind::Bool:
			WriteByOffset<bool>(object, property.Offset) = std::get<bool>(value);
			break;

		case EPropertyKind::Int32:
			WriteByOffset<int32_t>(object, property.Offset) = std::get<int32_t>(value);
			break;

		case EPropertyKind::UInt32:
			WriteByOffset<uint32_t>(object, property.Offset) = std::get<uint32_t>(value);
			break;

		case EPropertyKind::Float:
			WriteByOffset<float>(object, property.Offset) = std::get<float>(value);
			break;

		case EPropertyKind::Vec2:
			WriteByOffset<glm::vec2>(object, property.Offset) = std::get<glm::vec2>(value);
			break;

		case EPropertyKind::Vec3:
			WriteByOffset<glm::vec3>(object, property.Offset) = std::get<glm::vec3>(value);
			break;

		case EPropertyKind::Vec4:
			WriteByOffset<glm::vec4>(object, property.Offset) = std::get<glm::vec4>(value);
			break;

		case EPropertyKind::Quat:
			WriteByOffset<glm::quat>(object, property.Offset) = glm::normalize(std::get<glm::quat>(value));
			break;

		case EPropertyKind::String:
			WriteByOffset<std::string>(object, property.Offset) = std::get<std::string>(value);
			break;

		default:
			break;
		}
	}
}