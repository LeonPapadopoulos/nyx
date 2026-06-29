#include "ReflectedArchiveSerializer.h"

#include "ReflectionUtils.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace Nyx::Engine
{
	bool ReflectedArchiveSerializer::SerializeObject(
		BinaryWriter& writer,
		const void* object,
		const Nyx::Reflection::TypeMetadata& typeMetadata)
	{
		using namespace Nyx::Reflection;

		for (size_t i = 0; i < typeMetadata.PropertyCount; ++i)
		{
			const PropertyMetadata& property = typeMetadata.Properties[i];

			if (!HasFlag(property.Flags, EPropertyFlags::Serialize))
			{
				continue;
			}

			switch (property.Kind)
			{
			case EPropertyKind::Bool:
				writer.WriteBool(AccessProperty<bool>(object, property));
				break;

			case EPropertyKind::Int32:
				writer.WriteInt32(AccessProperty<int32_t>(object, property));
				break;

			case EPropertyKind::UInt32:
				writer.WriteUInt32(AccessProperty<uint32_t>(object, property));
				break;

			case EPropertyKind::Float:
				writer.WriteFloat(AccessProperty<float>(object, property));
				break;

			case EPropertyKind::String:
				writer.WriteString(AccessProperty<std::string>(object, property));
				break;

			case EPropertyKind::Vec2:
			{
				const glm::vec2& v = AccessProperty<glm::vec2>(object, property);
				writer.WriteFloat(v.x);
				writer.WriteFloat(v.y);
				break;
			}

			case EPropertyKind::Vec3:
			{
				const glm::vec3& v = AccessProperty<glm::vec3>(object, property);
				writer.WriteFloat(v.x);
				writer.WriteFloat(v.y);
				writer.WriteFloat(v.z);
				break;
			}

			case EPropertyKind::Vec4:
			{
				const glm::vec4& v = AccessProperty<glm::vec4>(object, property);
				writer.WriteFloat(v.x);
				writer.WriteFloat(v.y);
				writer.WriteFloat(v.z);
				writer.WriteFloat(v.w);
				break;
			}

			case EPropertyKind::Quat:
			{
				const glm::quat& q = AccessProperty<glm::quat>(object, property);
				writer.WriteFloat(q.w);
				writer.WriteFloat(q.x);
				writer.WriteFloat(q.y);
				writer.WriteFloat(q.z);
				break;
			}

			case EPropertyKind::Struct:
			{
				const TypeMetadata* nestedType = TryGetNestedType(property);
				if (!nestedType)
				{
					return false;
				}

				const void* nestedObject = GetPropertyAddress(object, property);
				if (!SerializeObject(writer, nestedObject, *nestedType))
				{
					return false;
				}
				break;
			}

			default:
				return false;
			}
		}

		return true;
	}

	bool ReflectedArchiveSerializer::DeserializeObject(
		BinaryReader& reader,
		void* object,
		const Nyx::Reflection::TypeMetadata& typeMetadata)
	{
		using namespace Nyx::Reflection;

		for (size_t i = 0; i < typeMetadata.PropertyCount; ++i)
		{
			const PropertyMetadata& property = typeMetadata.Properties[i];

			if (!HasFlag(property.Flags, EPropertyFlags::Serialize))
			{
				continue;
			}

			switch (property.Kind)
			{
			case EPropertyKind::Bool:
			{
				bool value = false;
				if (!reader.ReadBool(value)) return false;
				AccessProperty<bool>(object, property) = value;
				break;
			}

			case EPropertyKind::Int32:
			{
				int32_t value = 0;
				if (!reader.ReadInt32(value)) return false;
				AccessProperty<int32_t>(object, property) = value;
				break;
			}

			case EPropertyKind::UInt32:
			{
				uint32_t value = 0;
				if (!reader.ReadUInt32(value)) return false;
				AccessProperty<uint32_t>(object, property) = value;
				break;
			}

			case EPropertyKind::Float:
			{
				float value = 0.0f;
				if (!reader.ReadFloat(value)) return false;
				AccessProperty<float>(object, property) = value;
				break;
			}

			case EPropertyKind::String:
			{
				std::string value;
				if (!reader.ReadString(value)) return false;
				AccessProperty<std::string>(object, property) = std::move(value);
				break;
			}

			case EPropertyKind::Vec2:
			{
				glm::vec2 value{};
				if (!reader.ReadFloat(value.x)) return false;
				if (!reader.ReadFloat(value.y)) return false;
				AccessProperty<glm::vec2>(object, property) = value;
				break;
			}

			case EPropertyKind::Vec3:
			{
				glm::vec3 value{};
				if (!reader.ReadFloat(value.x)) return false;
				if (!reader.ReadFloat(value.y)) return false;
				if (!reader.ReadFloat(value.z)) return false;
				AccessProperty<glm::vec3>(object, property) = value;
				break;
			}

			case EPropertyKind::Vec4:
			{
				glm::vec4 value{};
				if (!reader.ReadFloat(value.x)) return false;
				if (!reader.ReadFloat(value.y)) return false;
				if (!reader.ReadFloat(value.z)) return false;
				if (!reader.ReadFloat(value.w)) return false;
				AccessProperty<glm::vec4>(object, property) = value;
				break;
			}

			case EPropertyKind::Quat:
			{
				glm::quat value{};
				if (!reader.ReadFloat(value.w)) return false;
				if (!reader.ReadFloat(value.x)) return false;
				if (!reader.ReadFloat(value.y)) return false;
				if (!reader.ReadFloat(value.z)) return false;
				AccessProperty<glm::quat>(object, property) = value;
				break;
			}

			case EPropertyKind::Struct:
			{
				const TypeMetadata* nestedType = TryGetNestedType(property);
				if (!nestedType)
				{
					return false;
				}

				void* nestedObject = GetPropertyAddress(object, property);
				if (!DeserializeObject(reader, nestedObject, *nestedType))
				{
					return false;
				}
				break;
			}

			default:
				return false;
			}
		}

		return reader.IsValid();
	}
}