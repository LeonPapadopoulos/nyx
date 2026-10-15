#include "ReflectedArchivePrinter.h"

#include "ReflectedArchiveSerializer.h"
#include "ReflectionUtils.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <charconv>
#include <cstdio>
#include <initializer_list>
#include <type_traits>
#include <variant>

namespace
{
	// The shortest text that reads back as the same float, e.g. 0.1 instead of 0.100000001
	std::string FloatToText(float value)
	{
		char text[32];
		const std::to_chars_result result = std::to_chars(text, text + sizeof(text), value);
		return std::string(text, result.ptr);
	}

	// For example: (0, 2, 6)
	std::string FloatsToText(std::initializer_list<float> values)
	{
		std::string text = "(";
		bool bFirst = true;

		for (const float value : values)
		{
			if (!bFirst)
			{
				text += ", ";
			}

			text += FloatToText(value);
			bFirst = false;
		}

		return text + ")";
	}

	// 64-bit values are IDs, which are easier to compare in hex, e.g. 0x5F3A9C2D1E4B7A10
	std::string UInt64ToText(uint64_t value)
	{
		char text[24];
		std::snprintf(text, sizeof(text), "0x%016llX", static_cast<unsigned long long>(value));
		return text;
	}

	// The string in quotes, with quotes, backslashes and control characters escaped
	std::string QuoteString(const std::string& value)
	{
		std::string text = "\"";

		for (const char c : value)
		{
			switch (c)
			{
			case '"':  text += "\\\""; break;
			case '\\': text += "\\\\"; break;
			case '\n': text += "\\n"; break;
			case '\r': text += "\\r"; break;
			case '\t': text += "\\t"; break;
			default:
				if (static_cast<unsigned char>(c) < 0x20)
				{
					char escaped[8];
					std::snprintf(escaped, sizeof(escaped), "\\x%02X", static_cast<unsigned char>(c));
					text += escaped;
				}
				else
				{
					text += c;
				}
				break;
			}
		}

		return text + "\"";
	}

	// Real data nests a few structs deep at most. The limit keeps damaged data from
	// recursing until the stack overflows.
	constexpr int MaxNestingDepth = 32;

	bool PrintBlock(
		Nyx::Engine::BinaryReader& reader,
		const Nyx::Reflection::TypeMetadata* typeMetadata,
		int indentLevel,
		int nestingDepth,
		std::string& outText)
	{
		using namespace Nyx::Reflection;
		using Nyx::Engine::BinaryReader;
		using Nyx::Engine::ReflectedArchivePrinter;
		using Nyx::Engine::ReflectedArchiveSerializer;

		if (nestingDepth > MaxNestingDepth)
		{
			return false;
		}

		BinaryReader block;
		uint16_t propertyCount = 0;

		if (!reader.ReadBlock(block) || !block.ReadUInt16(propertyCount))
		{
			return false;
		}

		const std::string indent(static_cast<size_t>(indentLevel) * 2, ' ');

		for (uint16_t i = 0; i < propertyCount; ++i)
		{
			uint32_t nameHash = 0;
			uint8_t kindNumber = 0;

			if (!block.ReadUInt32(nameHash) || !block.ReadUInt8(kindNumber))
			{
				return false;
			}

			const EPropertyKind savedKind = static_cast<EPropertyKind>(kindNumber);
			const PropertyMetadata* property = typeMetadata ? FindPropertyByNameHash(*typeMetadata, nameHash) : nullptr;

			// The property's name, or its name hash if the type doesn't have it
			const std::string label = property ? property->Name : NameHashToText(nameHash);

			if (!IsKnownPropertyKind(savedKind))
			{
				outText += indent + label + ": a kind this build doesn't know (" + std::to_string(kindNumber) +
					"), so the rest of this block can't be read\n";
				return true;
			}

			// Points out where the data doesn't match the type in this build
			std::string note;
			if (!property)
			{
				note = typeMetadata
					? std::string(" (") + GetPropertyKindName(savedKind) + ", not a property of this type)"
					: std::string(" (") + GetPropertyKindName(savedKind) + ")";
			}
			else if (property->Kind != savedKind)
			{
				note = std::string(" (saved as ") + GetPropertyKindName(savedKind) + ", the property is " +
					GetPropertyKindName(property->Kind) + " now)";
			}

			if (savedKind == EPropertyKind::Struct)
			{
				outText += indent + label + ":" + note + "\n";

				const TypeMetadata* nestedType =
					(property && property->Kind == EPropertyKind::Struct) ? TryGetNestedType(*property) : nullptr;

				if (!PrintBlock(block, nestedType, indentLevel + 1, nestingDepth + 1, outText))
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

			outText += indent + label + ": " + ReflectedArchivePrinter::ValueToText(value) + note + "\n";
		}

		return true;
	}
}

namespace Nyx::Engine
{
	using namespace Nyx::Reflection;

	bool ReflectedArchivePrinter::PrintObject(
		BinaryReader& reader,
		const TypeMetadata* typeMetadata,
		int indentLevel,
		std::string& outText)
	{
		return PrintBlock(reader, typeMetadata, indentLevel, 0, outText);
	}

	std::string ReflectedArchivePrinter::ValueToText(const PropertyValue& value)
	{
		return std::visit(
			[](const auto& typedValue) -> std::string
			{
				using T = std::decay_t<decltype(typedValue)>;

				if constexpr (std::is_same_v<T, bool>)
				{
					return typedValue ? "true" : "false";
				}
				else if constexpr (std::is_same_v<T, int32_t> || std::is_same_v<T, uint32_t>)
				{
					return std::to_string(typedValue);
				}
				else if constexpr (std::is_same_v<T, uint64_t>)
				{
					return UInt64ToText(typedValue);
				}
				else if constexpr (std::is_same_v<T, float>)
				{
					return FloatToText(typedValue);
				}
				else if constexpr (std::is_same_v<T, glm::vec2>)
				{
					return FloatsToText({ typedValue.x, typedValue.y });
				}
				else if constexpr (std::is_same_v<T, glm::vec3>)
				{
					return FloatsToText({ typedValue.x, typedValue.y, typedValue.z });
				}
				else if constexpr (std::is_same_v<T, glm::vec4>)
				{
					return FloatsToText({ typedValue.x, typedValue.y, typedValue.z, typedValue.w });
				}
				else if constexpr (std::is_same_v<T, glm::quat>)
				{
					return "(w " + FloatToText(typedValue.w) + ", x " + FloatToText(typedValue.x) + ", y " +
						FloatToText(typedValue.y) + ", z " + FloatToText(typedValue.z) + ")";
				}
				else if constexpr (std::is_same_v<T, std::string>)
				{
					return QuoteString(typedValue);
				}
				else
				{
					// Only "no value" is left; a new PropertyValue type ends up here
					static_assert(std::is_same_v<T, std::monostate>, "ValueToText doesn't handle this PropertyValue type");
					return "(no value)";
				}
			},
			value);
	}
}
