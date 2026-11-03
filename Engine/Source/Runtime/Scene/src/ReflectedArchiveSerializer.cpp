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
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace
{
	using namespace Nyx::Reflection;
	using Nyx::BinaryReader;
	using Nyx::BinaryWriter;
	using Nyx::ReadWarnings;
	using Nyx::ReflectedArchiveSerializer;

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
		return kind == EPropertyKind::Int32 || kind == EPropertyKind::UInt32 || kind == EPropertyKind::Float ||
			kind == EPropertyKind::Enum;
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
		case EPropertyKind::Enum:
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
		if (kind == EPropertyKind::Struct || kind == EPropertyKind::Array)
		{
			return reader.SkipBlock();
		}

		PropertyValue ignored;
		return ReflectedArchiveSerializer::ReadValue(reader, kind, ignored);
	}

	// Names a property in warnings, e.g. "Nyx::MeshRendererComponent::Mesh"
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

	// One property of a block as it is stored: its name's hash, and its bytes from there to its value's end
	struct StoredProperty
	{
		uint32_t NameHash = 0;
		std::span<const std::byte> Bytes;
	};

	uint32_t ReadStoredUInt32(std::span<const std::byte> bytes, size_t offset)
	{
		uint32_t value = 0;
		std::memcpy(&value, bytes.data() + offset, sizeof(value));
		return value;
	}

	// The bytes of a value that starts at `offset`; none known for a kind this build hasn't got, or
	// one that goes past the end
	std::optional<size_t> GetStoredValueSize(EPropertyKind kind, std::span<const std::byte> bytes, size_t offset)
	{
		size_t size = 0;
		switch (kind)
		{
		case EPropertyKind::Bool:
			size = 1;
			break;
		case EPropertyKind::Int32:
		case EPropertyKind::UInt32:
		case EPropertyKind::Float:
		case EPropertyKind::Enum:
			size = 4;
			break;
		case EPropertyKind::UInt64:
		case EPropertyKind::Vec2:
			size = 8;
			break;
		case EPropertyKind::Vec3:
			size = 12;
			break;
		case EPropertyKind::Vec4:
		case EPropertyKind::Quat:
			size = 16;
			break;
		case EPropertyKind::String:
		case EPropertyKind::Struct:
		case EPropertyKind::Array:
			if (bytes.size() < offset + sizeof(uint32_t))
			{
				return std::nullopt;
			}
			size = sizeof(uint32_t) + ReadStoredUInt32(bytes, offset);
			break;
		default:
			return std::nullopt;
		}

		if (bytes.size() < offset + size)
		{
			return std::nullopt;
		}
		return size;
	}

	// The properties of a whole property block, its size in front; false if it isn't whole
	bool SplitPropertyBlock(std::span<const std::byte> block, std::vector<StoredProperty>& outProperties)
	{
		constexpr size_t Start = sizeof(uint32_t) + sizeof(uint16_t);
		if (block.size() < Start || ReadStoredUInt32(block, 0) != block.size() - sizeof(uint32_t))
		{
			return false;
		}

		uint16_t count = 0;
		std::memcpy(&count, block.data() + sizeof(uint32_t), sizeof(count));

		outProperties.clear();
		outProperties.reserve(count);
		size_t offset = Start;
		for (uint16_t index = 0; index < count; ++index)
		{
			constexpr size_t Tag = sizeof(uint32_t) + sizeof(uint8_t);
			if (block.size() < offset + Tag)
			{
				return false;
			}
			const EPropertyKind kind = static_cast<EPropertyKind>(std::to_integer<uint8_t>(block[offset + sizeof(uint32_t)]));
			const std::optional<size_t> valueSize = GetStoredValueSize(kind, block, offset + Tag);
			if (!valueSize)
			{
				return false;
			}
			outProperties.push_back(StoredProperty{ ReadStoredUInt32(block, offset), block.subspan(offset, Tag + *valueSize) });
			offset += Tag + *valueSize;
		}
		return offset == block.size();
	}

	// Writes an array property's value: its elements' kind and count, then each element
	bool WriteArray(BinaryWriter& writer, const void* object, const PropertyMetadata& property)
	{
		const ArrayMetadata* array = TryGetArray(property);
		if (!array)
		{
			return false;
		}

		const PropertyMetadata& element = array->Element;
		const void* vector = GetPropertyAddress(object, property);
		const size_t count = array->GetSize(vector);

		BinaryWriter block;
		block.WriteUInt8(static_cast<uint8_t>(element.Kind));
		block.WriteUInt32(static_cast<uint32_t>(count));

		for (size_t index = 0; index < count; ++index)
		{
			const void* elementObject = array->GetConstElement(vector, index);
			if (element.Kind == EPropertyKind::Struct)
			{
				const TypeMetadata* nestedType = TryGetNestedType(element);
				if (!nestedType || !ReflectedArchiveSerializer::SerializeObject(block, elementObject, *nestedType))
				{
					return false;
				}
				continue;
			}

			// The element is a property of itself, at offset 0
			const PropertyValue value = GetPropertyValue(elementObject, element);
			if (std::holds_alternative<std::monostate>(value))
			{
				return false;
			}
			ReflectedArchiveSerializer::WriteValue(block, value);
		}

		writer.WriteBlock(block);
		return true;
	}

	// Sets a read value, unless it is an Enum number no enumerator has
	void SetReadValue(void* object, const PropertyMetadata& property, const PropertyValue& value, const std::string& path, ReadWarnings& warnings)
	{
		if (property.Kind == EPropertyKind::Enum)
		{
			const EnumMetadata* enumMetadata = TryGetEnum(property);
			const int32_t* number = std::get_if<int32_t>(&value);
			if (!enumMetadata || !number || !FindEnumEntry(*enumMetadata, *number))
			{
				warnings.Add(path + ": kept the value, because the saved number (" + (number ? std::to_string(*number) : std::string("?")) +
					") isn't one of the enum's");
				return;
			}
		}

		SetPropertyValue(object, property, value);
	}

	// Reads an array's saved value into the property: as many elements as were saved, each read
	// like a property, converted between number kinds
	bool ReadArray(BinaryReader& reader, void* object, const PropertyMetadata& property, const std::string& path, ReadWarnings& warnings)
	{
		BinaryReader block;
		uint8_t kindNumber = 0;
		uint32_t count = 0;
		if (!reader.ReadBlock(block) || !block.ReadUInt8(kindNumber) || !block.ReadUInt32(count))
		{
			return false;
		}

		// Every element takes at least a byte: more than that is damaged data
		if (count > block.GetRemainingSize())
		{
			return false;
		}

		const ArrayMetadata* array = TryGetArray(property);
		const EPropertyKind savedKind = static_cast<EPropertyKind>(kindNumber);
		if (!array)
		{
			warnings.Add(path + ": skipped the saved array, because the property's elements aren't reflected");
			return true;
		}

		const PropertyMetadata& element = array->Element;
		const bool bConvert = savedKind != element.Kind && IsNumberKind(savedKind) && IsNumberKind(element.Kind);
		if (!IsKnownPropertyKind(savedKind) || savedKind == EPropertyKind::Array || (savedKind != element.Kind && !bConvert))
		{
			warnings.Add(path + ": skipped the saved array (elements saved as " + GetPropertyKindName(savedKind) + ", they are " +
				GetPropertyKindName(element.Kind) + " now)");
			return true;
		}
		if (bConvert)
		{
			warnings.Add(path + ": converted the saved " + GetPropertyKindName(savedKind) + " elements to " + GetPropertyKindName(element.Kind));
		}

		void* vector = GetPropertyAddress(object, property);
		array->Resize(vector, count);
		for (uint32_t index = 0; index < count; ++index)
		{
			void* elementObject = array->GetElement(vector, index);
			const std::string elementPath = path + "[" + std::to_string(index) + "]";

			if (savedKind == EPropertyKind::Struct)
			{
				const TypeMetadata* nestedType = TryGetNestedType(element);
				if (!nestedType || !ReadObject(block, elementObject, *nestedType, elementPath, warnings))
				{
					return false;
				}
				continue;
			}

			PropertyValue value;
			if (!ReflectedArchiveSerializer::ReadValue(block, savedKind, value))
			{
				return false;
			}
			SetReadValue(elementObject, element, bConvert ? ConvertNumber(value, element.Kind) : value, elementPath, warnings);
		}

		return true;
	}

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
			else if (property.Kind == EPropertyKind::Array)
			{
				if (!WriteArray(block, object, property))
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

		if (savedKind == property.Kind && property.Kind == EPropertyKind::Array)
		{
			return ReadArray(reader, object, property, GetPropertyPath(objectPath, property), warnings);
		}

		if (savedKind == property.Kind)
		{
			PropertyValue value;
			if (!ReflectedArchiveSerializer::ReadValue(reader, savedKind, value))
			{
				return false;
			}

			SetReadValue(object, property, value, GetPropertyPath(objectPath, property), warnings);
			return true;
		}

		if (IsNumberKind(savedKind) && IsNumberKind(property.Kind))
		{
			PropertyValue value;
			if (!ReflectedArchiveSerializer::ReadValue(reader, savedKind, value))
			{
				return false;
			}

			SetReadValue(object, property, ConvertNumber(value, property.Kind), GetPropertyPath(objectPath, property), warnings);

			warnings.Add(GetPropertyPath(objectPath, property) + ": converted the saved " + GetPropertyKindName(savedKind) +
				" to " + GetPropertyKindName(property.Kind));
			return true;
		}

		warnings.Add(GetPropertyPath(objectPath, property) + ": skipped the saved value (saved as " +
			GetPropertyKindName(savedKind) + ", the property is " + GetPropertyKindName(property.Kind) + " now)");

		return SkipValue(reader, savedKind);
	}

	// Reads one property block into the object; see ReflectedArchiveSerializer::DeserializeObject.
	// objectPath names the object in warnings, e.g. "Nyx::MeshRendererComponent::Mesh".
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

namespace Nyx
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

	bool ReflectedArchiveSerializer::WriteChangedProperties(
		BinaryWriter& writer,
		std::span<const std::byte> block,
		std::span<const std::byte> base,
		bool& outbChanged)
	{
		outbChanged = false;

		std::vector<StoredProperty> properties;
		std::vector<StoredProperty> baseProperties;
		if (!SplitPropertyBlock(block, properties) || !SplitPropertyBlock(base, baseProperties))
		{
			return false;
		}

		BinaryWriter changed;
		uint16_t changedCount = 0;
		for (size_t index = 0; index < properties.size(); ++index)
		{
			const StoredProperty& property = properties[index];

			// Mostly at the same place, as one build wrote both blocks
			const StoredProperty* other = index < baseProperties.size() && baseProperties[index].NameHash == property.NameHash ? &baseProperties[index] : nullptr;
			if (!other)
			{
				const auto found = std::find_if(baseProperties.begin(), baseProperties.end(),
					[&property](const StoredProperty& candidate)
					{
						return candidate.NameHash == property.NameHash;
					});
				other = found != baseProperties.end() ? &*found : nullptr;
			}

			if (!other || other->Bytes.size() != property.Bytes.size() || std::memcmp(other->Bytes.data(), property.Bytes.data(), property.Bytes.size()) != 0)
			{
				changed.WriteBytes(property.Bytes.data(), property.Bytes.size());
				++changedCount;
			}
		}

		if (changedCount == 0)
		{
			return true;
		}

		BinaryWriter out;
		out.WriteUInt16(changedCount);
		out.WriteBytes(changed.GetBytes().data(), changed.GetBytes().size());
		writer.WriteBlock(out);
		outbChanged = true;
		return true;
	}

	bool ReflectedArchiveSerializer::SkipObject(BinaryReader& reader)
	{
		return reader.SkipBlock();
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
		case EPropertyKind::Enum:
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