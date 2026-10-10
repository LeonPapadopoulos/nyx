#include "PropertyValueAccess.h"

#include "ReflectionUtils.h"

#include <cstdint>
#include <string>

namespace Nyx::Reflection
{
	PropertyValue GetPropertyValue(const void* object, const PropertyMetadata& property)
	{
		switch (property.Kind)
		{
		case EPropertyKind::Bool:   return AccessProperty<bool>(object, property);
		case EPropertyKind::Int32:  return AccessProperty<int32_t>(object, property);
		case EPropertyKind::UInt32: return AccessProperty<uint32_t>(object, property);
		case EPropertyKind::UInt64: return AccessProperty<uint64_t>(object, property);
		case EPropertyKind::Float:  return AccessProperty<float>(object, property);
		case EPropertyKind::Vec2:   return AccessProperty<glm::vec2>(object, property);
		case EPropertyKind::Vec3:   return AccessProperty<glm::vec3>(object, property);
		case EPropertyKind::Vec4:   return AccessProperty<glm::vec4>(object, property);
		case EPropertyKind::Quat:   return AccessProperty<glm::quat>(object, property);
		case EPropertyKind::String: return AccessProperty<std::string>(object, property);
		default:                    return std::monostate{};
		}
	}

	void SetPropertyValue(void* object, const PropertyMetadata& property, const PropertyValue& value)
	{
		switch (property.Kind)
		{
		case EPropertyKind::Bool:   AccessProperty<bool>(object, property) = std::get<bool>(value); break;
		case EPropertyKind::Int32:  AccessProperty<int32_t>(object, property) = std::get<int32_t>(value); break;
		case EPropertyKind::UInt32: AccessProperty<uint32_t>(object, property) = std::get<uint32_t>(value); break;
		case EPropertyKind::UInt64: AccessProperty<uint64_t>(object, property) = std::get<uint64_t>(value); break;
		case EPropertyKind::Float:  AccessProperty<float>(object, property) = std::get<float>(value); break;
		case EPropertyKind::Vec2:   AccessProperty<glm::vec2>(object, property) = std::get<glm::vec2>(value); break;
		case EPropertyKind::Vec3:   AccessProperty<glm::vec3>(object, property) = std::get<glm::vec3>(value); break;
		case EPropertyKind::Vec4:   AccessProperty<glm::vec4>(object, property) = std::get<glm::vec4>(value); break;
		case EPropertyKind::Quat:   AccessProperty<glm::quat>(object, property) = std::get<glm::quat>(value); break;
		case EPropertyKind::String: AccessProperty<std::string>(object, property) = std::get<std::string>(value); break;
		default:                    break;
		}
	}
}
