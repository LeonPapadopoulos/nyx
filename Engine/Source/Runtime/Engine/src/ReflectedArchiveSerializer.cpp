#include "ReflectedArchiveSerializer.h"

#include "Log.h"
#include "PropertyValueAccess.h"
#include "ReflectionUtils.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace
{
	using namespace Nyx::Reflection;
	using Nyx::Engine::BinaryReader;
	using Nyx::Engine::BinaryWriter;
	using Nyx::Engine::ReadWarnings;
	using Nyx::Engine::ReflectedArchiveSerializer;

	void WriteFloats(BinaryWriter& writer, std::initializer_list<float> values)
	{
		for (const float value : values)
		{
			writer.WriteFloat(value);
		}
	}

	bool ReadFloats(BinaryReader& reader, std::initializer_list<float*> outValues)
	{
		for (float* outValue : outValues)
		{
			if (!reader.ReadFloat(*outValue))
			{
				return false;
			}
		}

		return true;
	}

	// Number kinds can be converted into each other when a property changes its type. UInt64 is
	// left out: its values are IDs, and most of them don't fit any other number kind.
	bool IsNumberKind(EPropertyKind kind)
	{
		return kind == EPropertyKind::Int32 || kind == EPropertyKind::UInt32 || kind == EPropertyKind::Float;
	}

	// Converts a number to another number kind, rounded and clamped to the range of the new kind
	PropertyValue ConvertNumber(const PropertyValue& value, EPropertyKind targetKind)
	{
		double number = 0.0;

		if (const int32_t* asInt32 = std::get_if<int32_t>(&value))
		{
			number = *asInt32;
		}
		else if (const uint32_t* asUInt32 = std::get_if<uint32_t>(&value))
		{
			number = *asUInt32;
		}
		else if (const float* asFloat = std::get_if<float>(&value))
		{
			number = std::isnan(*asFloat) ? 0.0 : *asFloat;
		}

		switch (targetKind)
		{
		case EPropertyKind::Int32:
		{
			constexpr double Min = std::numeric_limits<int32_t>::min();
			constexpr double Max = std::numeric_limits<int32_t>::max();
			return static_cast<int32_t>(std::clamp(std::round(number), Min, Max));
		}

		case EPropertyKind::UInt32:
		{
			constexpr double Max = std::numeric_limits<uint32_t>::max();
			return static_cast<uint32_t>(std::clamp(std::round(number), 0.0, Max));
		}

		case EPropertyKind::Float:
			return static_cast<float>(number);

		default:
			return value;
		}
	}

	// Skips one saved value of a kind this build knows
	bool SkipValue(BinaryReader& reader, EPropertyKind kind)
	{
		if (kind == EPropertyKind::Struct)
		{
			return reader.SkipBlock();
		}

		PropertyValue ignored;
		return ReflectedArchiveSerializer::ReadValue(reader, kind, ignored);
	}

	// Names a property in warnings, e.g. "Nyx::Engine::MeshRendererComponent::Mesh"
	std::string GetPropertyPath(const std::string& objectPath, const PropertyMetadata& property)
	{
		return objectPath + "::" + property.Name;
	}

	bool ReadObject(
		BinaryReader& reader,
		void* object,
		const TypeMetadata& typeMetadata,
		const std::string& objectPath,
		ReadWarnings& warnings);

	// Writes the properties at the indices (into typeMetadata.Properties, each Serialize) as one
	// property block. Returns false if a property has a kind that can't be written.
	bool WriteProperties(
		BinaryWriter& writer,
		const void* object,
		const TypeMetadata& typeMetadata,
		const std::vector<size_t>& propertyIndices)
	{
		// The block starts with its size, which is only known once all properties are written
		BinaryWriter block;
		block.WriteUInt16(static_cast<uint16_t>(propertyIndices.size()));

		for (const size_t propertyIndex : propertyIndices)
		{
			const PropertyMetadata& property = typeMetadata.Properties[propertyIndex];

			block.WriteUInt32(property.NameHash);
			block.WriteUInt8(static_cast<uint8_t>(property.Kind));

			if (property.Kind == EPropertyKind::Struct)
			{
				const TypeMetadata* nestedType = TryGetNestedType(property);
				if (!nestedType || !ReflectedArchiveSerializer::SerializeObject(block, GetPropertyAddress(object, property), *nestedType))
				{
					return false;
				}
			}
			else
			{
				// No value means a kind this code can't write yet; writing nothing would make
				// readers take the next property's bytes as this value.
				const PropertyValue value = GetPropertyValue(object, property);
				if (std::holds_alternative<std::monostate>(value))
				{
					return false;
				}

				ReflectedArchiveSerializer::WriteValue(block, value);
			}
		}

		writer.WriteBlock(block);
		return true;
	}

	// Reads a saved value into the property. If the property's kind changed since the value was
	// saved, the value is converted (between number kinds) or skipped.
	bool ReadSavedValueIntoProperty(
		BinaryReader& reader,
		EPropertyKind savedKind,
		void* object,
		const PropertyMetadata& property,
		const std::string& objectPath,
		ReadWarnings& warnings)
	{
		if (savedKind == property.Kind && property.Kind == EPropertyKind::Struct)
		{
			const TypeMetadata* nestedType = TryGetNestedType(property);
			if (!nestedType)
			{
				warnings.Add(GetPropertyPath(objectPath, property) + ": skipped the saved value, because the property's type isn't reflected");
				return SkipValue(reader, savedKind);
			}

			return ReadObject(reader, GetPropertyAddress(object, property), *nestedType, GetPropertyPath(objectPath, property), warnings);
		}

		if (savedKind == property.Kind)
		{
			PropertyValue value;
			if (!ReflectedArchiveSerializer::ReadValue(reader, savedKind, value))
			{
				return false;
			}

			SetPropertyValue(object, property, value);
			return true;
		}

		if (IsNumberKind(savedKind) && IsNumberKind(property.Kind))
		{
			PropertyValue value;
			if (!ReflectedArchiveSerializer::ReadValue(reader, savedKind, value))
			{
				return false;
			}

			SetPropertyValue(object, property, ConvertNumber(value, property.Kind));

			warnings.Add(GetPropertyPath(objectPath, property) + ": converted the saved " + GetPropertyKindName(savedKind) +
				" to " + GetPropertyKindName(property.Kind));
			return true;
		}

		warnings.Add(GetPropertyPath(objectPath, property) + ": skipped the saved value (saved as " +
			GetPropertyKindName(savedKind) + ", the property is " + GetPropertyKindName(property.Kind) + " now)");

		return SkipValue(reader, savedKind);
	}

	// Reads one property block into the object; see ReflectedArchiveSerializer::DeserializeObject.
	// objectPath names the object in warnings, e.g. "Nyx::Engine::MeshRendererComponent::Mesh".
	bool ReadObject(
		BinaryReader& reader,
		void* object,
		const TypeMetadata& typeMetadata,
		const std::string& objectPath,
		ReadWarnings& warnings)
	{
		BinaryReader block;
		uint16_t propertyCount = 0;

		if (!reader.ReadBlock(block) || !block.ReadUInt16(propertyCount))
		{
			return false;
		}

		for (uint16_t i = 0; i < propertyCount; ++i)
		{
			uint32_t nameHash = 0;
			uint8_t kindNumber = 0;

			if (!block.ReadUInt32(nameHash) || !block.ReadUInt8(kindNumber))
			{
				return false;
			}

			const EPropertyKind savedKind = static_cast<EPropertyKind>(kindNumber);

			// Without knowing the kind, the size of the value is unknown too, so nothing after it
			// can be read. The rest of the block is skipped; those properties keep their values.
			if (!IsKnownPropertyKind(savedKind))
			{
				warnings.Add(objectPath + ": skipped a saved property of a kind this build doesn't know (" +
					std::to_string(kindNumber) + "), and the properties saved after it");
				return true;
			}

			const PropertyMetadata* property = FindPropertyByNameHash(typeMetadata, nameHash);

			if (!property)
			{
				warnings.Add(objectPath + ": skipped a saved property the type doesn't have (name hash " +
					NameHashToText(nameHash) + ", " + GetPropertyKindName(savedKind) + ")");

				if (!SkipValue(block, savedKind))
				{
					return false;
				}

				continue;
			}

			if (!HasFlag(property->Flags, EPropertyFlags::Serialize))
			{
				warnings.Add(GetPropertyPath(objectPath, *property) + ": skipped the saved value, because the property isn't saved anymore");

				if (!SkipValue(block, savedKind))
				{
					return false;
				}

				continue;
			}

			if (!ReadSavedValueIntoProperty(block, savedKind, object, *property, objectPath, warnings))
			{
				return false;
			}
		}

		return true;
	}
}

namespace Nyx::Engine
{
	using namespace Nyx::Reflection;

	void ReadWarnings::Add(const std::string& message)
	{
		++CountByMessage[message];
	}

	void ReadWarnings::Log(std::string_view source) const
	{
		for (const auto& [message, count] : CountByMessage)
		{
			if (count == 1)
			{
				LOG_WARNING("{0}: {1}", source, message);
			}
			else
			{
				LOG_WARNING("{0}: {1} ({2} times)", source, message, count);
			}
		}
	}

	bool ReflectedArchiveSerializer::SerializeObject(
		BinaryWriter& writer,
		const void* object,
		const TypeMetadata& typeMetadata)
	{
		std::vector<size_t> propertyIndices;
		for (size_t i = 0; i < typeMetadata.PropertyCount; ++i)
		{
			if (HasFlag(typeMetadata.Properties[i].Flags, EPropertyFlags::Serialize))
			{
				propertyIndices.push_back(i);
			}
		}

		return WriteProperties(writer, object, typeMetadata, propertyIndices);
	}

	bool ReflectedArchiveSerializer::SerializeProperties(
		BinaryWriter& writer,
		const void* object,
		const TypeMetadata& typeMetadata,
		std::vector<size_t> propertyIndices)
	{
		// In declaration order, each once, and only what scene files would save too
		std::sort(propertyIndices.begin(), propertyIndices.end());
		propertyIndices.erase(std::unique(propertyIndices.begin(), propertyIndices.end()), propertyIndices.end());
		std::erase_if(propertyIndices,
			[&typeMetadata](size_t propertyIndex)
			{
				return propertyIndex >= typeMetadata.PropertyCount ||
					!HasFlag(typeMetadata.Properties[propertyIndex].Flags, EPropertyFlags::Serialize);
			});

		return WriteProperties(writer, object, typeMetadata, propertyIndices);
	}

	bool ReflectedArchiveSerializer::DeserializeObject(
		BinaryReader& reader,
		void* object,
		const TypeMetadata& typeMetadata,
		ReadWarnings& warnings)
	{
		return ReadObject(reader, object, typeMetadata, typeMetadata.Name, warnings);
	}

	bool ReflectedArchiveSerializer::SkipObject(BinaryReader& reader)
	{
		return reader.SkipBlock();
	}

	bool ReflectedArchiveSerializer::DeserializeUntaggedObject(
		BinaryReader& reader,
		void* object,
		const TypeMetadata& typeMetadata)
	{
		for (size_t i = 0; i < typeMetadata.PropertyCount; ++i)
		{
			const PropertyMetadata& property = typeMetadata.Properties[i];

			if (!HasFlag(property.Flags, EPropertyFlags::Serialize))
			{
				continue;
			}

			if (property.Kind == EPropertyKind::Struct)
			{
				const TypeMetadata* nestedType = TryGetNestedType(property);
				if (!nestedType || !DeserializeUntaggedObject(reader, GetPropertyAddress(object, property), *nestedType))
				{
					return false;
				}

				continue;
			}

			// Values were stored the same way as in the tagged format, just without name hash and kind
			PropertyValue value;
			if (!ReadValue(reader, property.Kind, value))
			{
				return false;
			}

			SetPropertyValue(object, property, value);
		}

		return reader.IsValid();
	}

	void ReflectedArchiveSerializer::WriteValue(BinaryWriter& writer, const PropertyValue& value)
	{
		std::visit(
			[&writer](const auto& typedValue)
			{
				using T = std::decay_t<decltype(typedValue)>;

				if constexpr (std::is_same_v<T, bool>)
				{
					writer.WriteBool(typedValue);
				}
				else if constexpr (std::is_same_v<T, int32_t>)
				{
					writer.WriteInt32(typedValue);
				}
				else if constexpr (std::is_same_v<T, uint32_t>)
				{
					writer.WriteUInt32(typedValue);
				}
				else if constexpr (std::is_same_v<T, uint64_t>)
				{
					writer.WriteUInt64(typedValue);
				}
				else if constexpr (std::is_same_v<T, float>)
				{
					writer.WriteFloat(typedValue);
				}
				else if constexpr (std::is_same_v<T, glm::vec2>)
				{
					WriteFloats(writer, { typedValue.x, typedValue.y });
				}
				else if constexpr (std::is_same_v<T, glm::vec3>)
				{
					WriteFloats(writer, { typedValue.x, typedValue.y, typedValue.z });
				}
				else if constexpr (std::is_same_v<T, glm::vec4>)
				{
					WriteFloats(writer, { typedValue.x, typedValue.y, typedValue.z, typedValue.w });
				}
				else if constexpr (std::is_same_v<T, glm::quat>)
				{
					WriteFloats(writer, { typedValue.w, typedValue.x, typedValue.y, typedValue.z });
				}
				else if constexpr (std::is_same_v<T, std::string>)
				{
					writer.WriteString(typedValue);
				}
				else
				{
					// Only "no value" is left, which writes nothing; a new PropertyValue type ends up here
					static_assert(std::is_same_v<T, std::monostate>, "WriteValue doesn't handle this PropertyValue type");
				}
			},
			value);
	}

	bool ReflectedArchiveSerializer::ReadValue(BinaryReader& reader, EPropertyKind kind, PropertyValue& outValue)
	{
		switch (kind)
		{
		case EPropertyKind::Bool:
		{
			bool value = false;
			if (!reader.ReadBool(value))
			{
				return false;
			}

			outValue = value;
			return true;
		}

		case EPropertyKind::Int32:
		{
			int32_t value = 0;
			if (!reader.ReadInt32(value))
			{
				return false;
			}

			outValue = value;
			return true;
		}

		case EPropertyKind::UInt32:
		{
			uint32_t value = 0;
			if (!reader.ReadUInt32(value))
			{
				return false;
			}

			outValue = value;
			return true;
		}

		case EPropertyKind::UInt64:
		{
			uint64_t value = 0;
			if (!reader.ReadUInt64(value))
			{
				return false;
			}

			outValue = value;
			return true;
		}

		case EPropertyKind::Float:
		{
			float value = 0.0f;
			if (!reader.ReadFloat(value))
			{
				return false;
			}

			outValue = value;
			return true;
		}

		case EPropertyKind::Vec2:
		{
			glm::vec2 value{};
			if (!ReadFloats(reader, { &value.x, &value.y }))
			{
				return false;
			}

			outValue = value;
			return true;
		}

		case EPropertyKind::Vec3:
		{
			glm::vec3 value{};
			if (!ReadFloats(reader, { &value.x, &value.y, &value.z }))
			{
				return false;
			}

			outValue = value;
			return true;
		}

		case EPropertyKind::Vec4:
		{
			glm::vec4 value{};
			if (!ReadFloats(reader, { &value.x, &value.y, &value.z, &value.w }))
			{
				return false;
			}

			outValue = value;
			return true;
		}

		case EPropertyKind::Quat:
		{
			glm::quat value{};
			if (!ReadFloats(reader, { &value.w, &value.x, &value.y, &value.z }))
			{
				return false;
			}

			outValue = value;
			return true;
		}

		case EPropertyKind::String:
		{
			std::string value;
			if (!reader.ReadString(value))
			{
				return false;
			}

			outValue = std::move(value);
			return true;
		}

		default:
			return false;
		}
	}
}