#include "ReflectedAssetText.h"

#include "AssetReference.h"
#include "MaterialAsset.h"
#include "ReflectionUtils.h"
#include "StringUtil.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <optional>
#include <span>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace Nyx
{
	namespace
	{
		using Reflection::ArrayMetadata;
		using Reflection::EnumMetadata;
		using Reflection::EPropertyKind;
		using Reflection::PropertyMetadata;
		using Reflection::TypeMetadata;

		// An index past this isn't read: a slip of the finger in a line shouldn't make a huge array
		constexpr size_t MaxElementIndex = 100000;

		bool IsSerialized(const PropertyMetadata& property)
		{
			return Reflection::HasFlag(property.Flags, Reflection::EPropertyFlags::Serialize);
		}

		// The property's name in the text: its TextName, else its own
		std::string_view GetTextName(const PropertyMetadata& property)
		{
			const char* name = Reflection::FindMetadataValue(property, "TextName");
			return name && name[0] ? std::string_view(name) : std::string_view(property.Name);
		}

		bool IsAssetReference(const PropertyMetadata& property)
		{
			return Reflection::IsStructProperty(property) && Reflection::TryGetNestedType(property) == &Reflection::GetTypeMetadata<AssetReference>();
		}

		// A struct whose values have lines of their own: any but an asset reference, which is a value
		const TypeMetadata* GetStructOfValues(const PropertyMetadata& property)
		{
			return Reflection::IsStructProperty(property) && !IsAssetReference(property) ? Reflection::TryGetNestedType(property) : nullptr;
		}

		// The struct an array's elements are, when they have lines of their own
		const TypeMetadata* GetElementStruct(const ArrayMetadata& array)
		{
			return GetStructOfValues(array.Element);
		}

		// The struct's Serialize properties but `skip` (an entry's name)
		std::vector<const PropertyMetadata*> GetValueProperties(const TypeMetadata& type, const PropertyMetadata* skip)
		{
			std::vector<const PropertyMetadata*> properties;
			for (size_t i = 0; i < type.PropertyCount; ++i)
			{
				if (&type.Properties[i] != skip && IsSerialized(type.Properties[i]))
				{
					properties.push_back(&type.Properties[i]);
				}
			}
			return properties;
		}

		const PropertyMetadata* FindByTextName(const TypeMetadata& type, std::string_view name, const PropertyMetadata* skip)
		{
			for (const PropertyMetadata* property : GetValueProperties(type, skip))
			{
				if (GetTextName(*property) == name)
				{
					return property;
				}
			}
			return nullptr;
		}

		// How many numbers or words a value of the kind is on a line; 0 for a kind an Inline struct's
		// line can't hold
		size_t CountWords(EPropertyKind kind)
		{
			switch (kind)
			{
			case EPropertyKind::Bool:
			case EPropertyKind::Int32:
			case EPropertyKind::UInt32:
			case EPropertyKind::UInt64:
			case EPropertyKind::Float:
			case EPropertyKind::Enum:
				return 1;
			case EPropertyKind::Vec2:
				return 2;
			case EPropertyKind::Vec3:
				return 3;
			case EPropertyKind::Vec4:
			case EPropertyKind::Quat:
				return 4;
			default:
				return 0;
			}
		}

		// Whether the struct is written on one line: Inline, with values that each fit on it
		bool IsInline(const TypeMetadata& type, const PropertyMetadata* skip)
		{
			if (!Reflection::HasMetadata(type, "Inline"))
			{
				return false;
			}
			const std::vector<const PropertyMetadata*> values = GetValueProperties(type, skip);
			return !values.empty() && std::all_of(values.begin(), values.end(), [](const PropertyMetadata* value)
				{
					return CountWords(value->Kind) > 0;
				});
		}

		// Arrays of structs are what the text lists: none until a line names one
		void ClearStructArrays(void* object, const TypeMetadata& type)
		{
			for (const PropertyMetadata* property : GetValueProperties(type, nullptr))
			{
				void* address = Reflection::GetPropertyAddress(object, *property);
				if (const ArrayMetadata* array = Reflection::TryGetArray(*property))
				{
					if (GetElementStruct(*array))
					{
						array->Resize(address, 0);
					}
				}
				else if (const TypeMetadata* nested = GetStructOfValues(*property))
				{
					ClearStructArrays(address, *nested);
				}
			}
		}

		std::string Quoted(std::string_view text)
		{
			return "\"" + std::string(text) + "\"";
		}

		// Strings

		bool NeedsQuotes(std::string_view text, bool bInList)
		{
			if (text.empty())
			{
				return bInList;
			}
			const auto isSpace = [](char c)
			{
				return c == ' ' || c == '\t';
			};
			return isSpace(text.front()) || isSpace(text.back()) || text.front() == '"' || text.find_first_of("\r\n") != std::string_view::npos ||
				(bInList && text.find_first_of(",\"") != std::string_view::npos);
		}

		std::string FormatString(std::string_view text, bool bInList)
		{
			if (!NeedsQuotes(text, bInList))
			{
				return std::string(text);
			}
			std::string quoted = "\"";
			for (const char c : text)
			{
				switch (c)
				{
				case '"':  quoted += "\\\""; break;
				case '\\': quoted += "\\\\"; break;
				case '\n': quoted += "\\n"; break;
				case '\r': quoted += "\\r"; break;
				case '\t': quoted += "\\t"; break;
				default:   quoted += c; break;
				}
			}
			return quoted + "\"";
		}

		// A string as written: in quotes, with \" \\ \n \r \t, or as it is. Nullopt for a quote that
		// isn't closed, or text after it.
		std::optional<std::string> ParseString(std::string_view text)
		{
			if (text.empty() || text.front() != '"')
			{
				return std::string(text);
			}
			std::string value;
			for (size_t i = 1; i < text.size(); ++i)
			{
				const char c = text[i];
				if (c == '"')
				{
					return i + 1 == text.size() ? std::optional<std::string>(std::move(value)) : std::nullopt;
				}
				if (c == '\\' && i + 1 < text.size())
				{
					const char escaped = text[++i];
					value += escaped == 'n' ? '\n' : escaped == 'r' ? '\r' : escaped == 't' ? '\t' : escaped;
					continue;
				}
				value += c;
			}
			return std::nullopt;
		}

		// A list's items: apart by commas outside quotes, without the spaces at their ends; none for
		// an empty text
		std::vector<std::string_view> SplitList(std::string_view text)
		{
			std::vector<std::string_view> items;
			if (Trim(text).empty())
			{
				return items;
			}
			bool bQuoted = false;
			size_t start = 0;
			for (size_t i = 0; i < text.size(); ++i)
			{
				if (bQuoted && text[i] == '\\')
				{
					++i;
				}
				else if (text[i] == '"')
				{
					bQuoted = !bQuoted;
				}
				else if (text[i] == ',' && !bQuoted)
				{
					items.push_back(Trim(text.substr(start, i - start)));
					start = i + 1;
				}
			}
			items.push_back(Trim(text.substr(start)));
			return items;
		}

		// Values

		std::string FormatFloats(const float* values, size_t count)
		{
			return FormatMaterialNumbers(std::span<const float>(values, count));
		}

		// The value at `address`, `value` saying what it is (a property, or an array's element);
		// empty for a kind the text can't hold
		std::string FormatValue(const void* address, const PropertyMetadata& value, bool bInList)
		{
			switch (value.Kind)
			{
			case EPropertyKind::Bool:
				return Reflection::AccessByOffset<bool>(address, 0) ? "true" : "false";
			case EPropertyKind::Int32:
				return std::to_string(Reflection::AccessByOffset<int32_t>(address, 0));
			case EPropertyKind::UInt32:
				return std::to_string(Reflection::AccessByOffset<uint32_t>(address, 0));
			case EPropertyKind::UInt64:
				return std::to_string(Reflection::AccessByOffset<uint64_t>(address, 0));
			case EPropertyKind::Float:
				return FormatFloats(&Reflection::AccessByOffset<float>(address, 0), 1);
			case EPropertyKind::Vec2:
				return FormatFloats(&Reflection::AccessByOffset<glm::vec2>(address, 0).x, 2);
			case EPropertyKind::Vec3:
				return FormatFloats(&Reflection::AccessByOffset<glm::vec3>(address, 0).x, 3);
			case EPropertyKind::Vec4:
				return FormatFloats(&Reflection::AccessByOffset<glm::vec4>(address, 0).x, 4);
			case EPropertyKind::Quat:
			{
				const glm::quat& rotation = Reflection::AccessByOffset<glm::quat>(address, 0);
				const float xyzw[4] = { rotation.x, rotation.y, rotation.z, rotation.w };
				return FormatFloats(xyzw, 4);
			}
			case EPropertyKind::String:
				return FormatString(Reflection::AccessByOffset<std::string>(address, 0), bInList);
			case EPropertyKind::Enum:
			{
				const EnumMetadata* enumMetadata = Reflection::TryGetEnum(value);
				return enumMetadata ? Reflection::EnumValueToText(*enumMetadata, Reflection::ReadEnumValue(address, *enumMetadata)) : std::string();
			}
			case EPropertyKind::Struct:
				return IsAssetReference(value) ? FormatString(Reflection::AccessByOffset<AssetReference>(address, 0).Path, bInList) : std::string();
			default:
				return {};
			}
		}

		template <typename T>
		bool ParseWhole(std::string_view text, T& outValue)
		{
			int base = 10;
			if constexpr (std::is_same_v<T, uint64_t>)
			{
				if (text.starts_with("0x") || text.starts_with("0X"))
				{
					text.remove_prefix(2);
					base = 16;
				}
			}
			const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), outValue, base);
			return !text.empty() && error == std::errc() && end == text.data() + text.size();
		}

		// Reads `text` into the value at `address`; without an address, only whether it can. False,
		// with what is wrong, when it can't, leaving the value as it was. `owner`: the property whose
		// metadata the value has (an array's, for its elements).
		bool ParseValue(void* address, const PropertyMetadata& value, const PropertyMetadata& owner, std::string_view text, std::string& outError)
		{
			switch (value.Kind)
			{
			case EPropertyKind::Bool:
			{
				const bool bTrue = text == "true" || text == "1";
				if (!bTrue && text != "false" && text != "0")
				{
					outError = Quoted(text) + " isn't true or false";
					return false;
				}
				if (address)
				{
					Reflection::AccessByOffset<bool>(address, 0) = bTrue;
				}
				return true;
			}
			case EPropertyKind::Int32:
			{
				int32_t number = 0;
				if (!ParseWhole(text, number))
				{
					outError = Quoted(text) + " isn't a whole number";
					return false;
				}
				if (address)
				{
					Reflection::AccessByOffset<int32_t>(address, 0) = number;
				}
				return true;
			}
			case EPropertyKind::UInt32:
			{
				uint32_t number = 0;
				if (!ParseWhole(text, number))
				{
					outError = Quoted(text) + " isn't a whole number of 0 or more";
					return false;
				}
				if (address)
				{
					Reflection::AccessByOffset<uint32_t>(address, 0) = number;
				}
				return true;
			}
			case EPropertyKind::UInt64:
			{
				uint64_t number = 0;
				if (!ParseWhole(text, number))
				{
					outError = Quoted(text) + " isn't a whole number of 0 or more";
					return false;
				}
				if (address)
				{
					Reflection::AccessByOffset<uint64_t>(address, 0) = number;
				}
				return true;
			}
			case EPropertyKind::Float:
			case EPropertyKind::Vec2:
			case EPropertyKind::Vec3:
			case EPropertyKind::Vec4:
			case EPropertyKind::Quat:
			{
				const size_t count = CountWords(value.Kind);
				const std::optional<std::vector<double>> numbers = ParseMaterialNumbers(text);
				if (!numbers || numbers->size() != count)
				{
					outError = Quoted(text) + (count == 1 ? " isn't a number" : " isn't " + std::to_string(count) + " numbers");
					return false;
				}
				if (!address)
				{
					return true;
				}
				float floats[4] = {};
				std::transform(numbers->begin(), numbers->end(), floats, [](double number)
					{
						return static_cast<float>(number);
					});
				switch (value.Kind)
				{
				case EPropertyKind::Float: Reflection::AccessByOffset<float>(address, 0) = floats[0]; break;
				case EPropertyKind::Vec2:  Reflection::AccessByOffset<glm::vec2>(address, 0) = glm::vec2(floats[0], floats[1]); break;
				case EPropertyKind::Vec3:  Reflection::AccessByOffset<glm::vec3>(address, 0) = glm::vec3(floats[0], floats[1], floats[2]); break;
				case EPropertyKind::Vec4:  Reflection::AccessByOffset<glm::vec4>(address, 0) = glm::vec4(floats[0], floats[1], floats[2], floats[3]); break;
				default:                   Reflection::AccessByOffset<glm::quat>(address, 0) = glm::quat(floats[3], floats[0], floats[1], floats[2]); break;
				}
				return true;
			}
			case EPropertyKind::String:
			{
				std::optional<std::string> parsed = ParseString(text);
				if (!parsed)
				{
					outError = Quoted(text) + " has a quote that isn't closed, or text after it";
					return false;
				}
				if (address)
				{
					Reflection::AccessByOffset<std::string>(address, 0) = std::move(*parsed);
				}
				return true;
			}
			case EPropertyKind::Enum:
			{
				const EnumMetadata* enumMetadata = Reflection::TryGetEnum(value);
				if (!enumMetadata)
				{
					outError = "has no enumerators to read";
					return false;
				}
				int64_t number = 0;
				if (const Reflection::EnumEntry* entry = Reflection::FindEnumEntryByName(*enumMetadata, text))
				{
					number = entry->Value;
				}
				else if (!ParseWhole(text, number))
				{
					std::string names;
					for (size_t i = 0; i < enumMetadata->EntryCount; ++i)
					{
						names += (i > 0 ? ", " : "") + std::string(enumMetadata->Entries[i].Name);
					}
					outError = Quoted(text) + " isn't one of " + names;
					return false;
				}
				if (address)
				{
					Reflection::WriteEnumValue(address, *enumMetadata, number);
				}
				return true;
			}
			case EPropertyKind::Struct:
				if (IsAssetReference(value))
				{
					std::optional<std::string> path = ParseString(text);
					if (!path)
					{
						outError = Quoted(text) + " has a quote that isn't closed, or text after it";
						return false;
					}
					if (address)
					{
						AssetReference& reference = Reflection::AccessByOffset<AssetReference>(address, 0);
						const char* assetType = Reflection::FindMetadataValue(owner, "AssetType");
						reference.Path = std::move(*path);
						reference.Type = reference.Path.empty() ? std::string() : assetType ? std::string(assetType) : reference.Type;
					}
					return true;
				}
				break;
			default:
				break;
			}
			outError = "can't be read from text";
			return false;
		}

		// An Inline struct's values on one line, apart by spaces; `skip`: its entry name
		std::string FormatInline(const void* object, const TypeMetadata& type, const PropertyMetadata* skip)
		{
			std::string text;
			for (const PropertyMetadata* value : GetValueProperties(type, skip))
			{
				text += (text.empty() ? "" : " ") + FormatValue(Reflection::GetPropertyAddress(object, *value), *value, false);
			}
			return text;
		}

		bool ParseInline(void* object, const TypeMetadata& type, const PropertyMetadata* skip, std::string_view text, std::string& outError)
		{
			std::vector<std::string_view> words;
			for (size_t start = text.find_first_not_of(" \t"); start != std::string_view::npos; start = text.find_first_not_of(" \t", start))
			{
				const size_t end = std::min(text.find_first_of(" \t", start), text.size());
				words.push_back(text.substr(start, end - start));
				start = end;
			}

			const std::vector<const PropertyMetadata*> values = GetValueProperties(type, skip);
			size_t most = 0;
			for (const PropertyMetadata* value : values)
			{
				most += CountWords(value->Kind);
			}
			if (words.size() > most)
			{
				outError = Quoted(text) + " has " + std::to_string(words.size()) + " values, " + std::to_string(most) + " at most";
				return false;
			}

			// Checked first, then set, so a value that can't be read changes nothing; the values
			// missing at the end keep theirs
			for (const bool bSet : { false, true })
			{
				size_t next = 0;
				for (const PropertyMetadata* value : values)
				{
					const size_t count = CountWords(value->Kind);
					if (next >= words.size())
					{
						break;
					}
					if (next + count > words.size())
					{
						outError = std::string(value->Name) + ": needs " + std::to_string(count) + " numbers";
						return false;
					}
					const std::string_view part(words[next].data(), static_cast<size_t>(words[next + count - 1].data() + words[next + count - 1].size() - words[next].data()));
					std::string error;
					if (!ParseValue(bSet ? Reflection::GetPropertyAddress(object, *value) : nullptr, *value, *value, part, error))
					{
						outError = std::string(value->Name) + ": " + error;
						return false;
					}
					next += count;
				}
			}
			return true;
		}

		// An array of values, on one line apart by commas
		std::string FormatList(const void* array, const ArrayMetadata& metadata)
		{
			std::string text;
			for (size_t i = 0; i < metadata.GetSize(array); ++i)
			{
				text += (i > 0 ? ", " : "") + FormatValue(metadata.GetConstElement(array, i), metadata.Element, true);
			}
			return text;
		}

		bool ParseList(void* array, const ArrayMetadata& metadata, const PropertyMetadata& owner, std::string_view text, std::string& outError)
		{
			const std::vector<std::string_view> items = SplitList(text);
			for (size_t i = 0; i < items.size(); ++i)
			{
				std::string error;
				if (!ParseValue(nullptr, metadata.Element, owner, items[i], error))
				{
					outError = "item " + std::to_string(i + 1) + ": " + error;
					return false;
				}
			}
			metadata.Resize(array, items.size());
			for (size_t i = 0; i < items.size(); ++i)
			{
				std::string error;
				ParseValue(metadata.GetElement(array, i), metadata.Element, owner, items[i], error);
			}
			return true;
		}

		// Names in a line's name

		struct KeySegment
		{
			// Empty for an index
			std::string_view Name;
			std::optional<size_t> Index;
		};

		// "Moves.Bite.Damage": Moves, Bite, Damage; "Points[2].X": Points, [2], X. False for a name
		// of another shape.
		bool SplitKey(std::string_view key, std::vector<KeySegment>& outSegments)
		{
			size_t i = 0;
			while (i < key.size())
			{
				if (key[i] == '[')
				{
					const size_t close = key.find(']', i);
					size_t index = 0;
					if (close == std::string_view::npos || !ParseWhole(key.substr(i + 1, close - i - 1), index))
					{
						return false;
					}
					outSegments.push_back(KeySegment{ .Index = index });
					i = close + 1;
				}
				else
				{
					const size_t end = std::min(key.find_first_of(".[", i), key.size());
					if (end == i)
					{
						return false;
					}
					outSegments.push_back(KeySegment{ .Name = key.substr(i, end - i) });
					i = end;
				}
				if (i < key.size() && key[i] == '.')
				{
					if (++i == key.size())
					{
						return false;
					}
				}
				else if (i < key.size() && key[i] != '[')
				{
					return false;
				}
			}
			return !outSegments.empty() && !outSegments.front().Name.empty();
		}

		// The name before the first '.' or '['
		std::string_view GetTopName(std::string_view key)
		{
			return key.substr(0, key.find_first_of(".["));
		}

		// What a line's name names: the value its value goes into
		struct Target
		{
			void* Address = nullptr;
			// What is at Address; null for an entry with nothing but its name
			const PropertyMetadata* Value = nullptr;
			// Whose metadata the value has
			const PropertyMetadata* Owner = nullptr;
			// An Inline struct, and its entry name, which its line leaves out
			const TypeMetadata* InlineType = nullptr;
			const PropertyMetadata* InlineSkip = nullptr;
		};

		// Follows a line's name through an object, making the entries and elements it names that
		// aren't there yet, and keeps how far it came (Path) and why it stopped (Error)
		struct KeyResolver
		{
			struct Grown
			{
				void* Array = nullptr;
				const ArrayMetadata* Metadata = nullptr;
				size_t Size = 0;
			};

			std::string Path;
			std::string Error;
			std::vector<Grown> Grew;

			bool InStruct(void* object, const TypeMetadata& type, const PropertyMetadata* skip, std::span<const KeySegment> segments, Target& outTarget)
			{
				if (segments.empty() || segments.front().Name.empty())
				{
					Error = "is a struct: a line for each of its values";
					return false;
				}
				const PropertyMetadata* property = FindByTextName(type, segments.front().Name, skip);
				if (!property)
				{
					Error = "has no " + Quoted(segments.front().Name);
					return false;
				}
				Path += (Path.empty() ? "" : ".") + std::string(property->Name);
				return InValue(Reflection::GetPropertyAddress(object, *property), *property, *property, segments.subspan(1), outTarget);
			}

			bool InValue(void* address, const PropertyMetadata& value, const PropertyMetadata& owner, std::span<const KeySegment> segments, Target& outTarget)
			{
				if (const ArrayMetadata* array = Reflection::TryGetArray(value))
				{
					const TypeMetadata* elementType = GetElementStruct(*array);
					if (!elementType)
					{
						if (!segments.empty())
						{
							Error = "is a list: one line, its items apart by commas";
							return false;
						}
						outTarget = Target{ .Address = address, .Value = &value, .Owner = &owner };
						return true;
					}
					if (segments.empty())
					{
						Error = "needs a line for each element's values";
						return false;
					}
					return InArray(address, *array, *elementType, segments, outTarget);
				}

				if (const TypeMetadata* type = GetStructOfValues(value))
				{
					if (IsInline(*type, nullptr))
					{
						if (!segments.empty())
						{
							Error = "is one line: its values apart by spaces";
							return false;
						}
						outTarget = Target{ .Address = address, .Value = &value, .Owner = &owner, .InlineType = type };
						return true;
					}
					return InStruct(address, *type, nullptr, segments, outTarget);
				}

				if (!segments.empty())
				{
					Error = "has no " + Quoted(segments.front().Name.empty() ? "[" + std::to_string(*segments.front().Index) + "]" : std::string(segments.front().Name));
					return false;
				}
				outTarget = Target{ .Address = address, .Value = &value, .Owner = &owner };
				return true;
			}

			bool InArray(void* address, const ArrayMetadata& array, const TypeMetadata& elementType, std::span<const KeySegment> segments, Target& outTarget)
			{
				const PropertyMetadata* nameProperty = FindEntryNameProperty(elementType);
				const KeySegment& segment = segments.front();
				const size_t size = array.GetSize(address);
				size_t index = size;
				if (nameProperty)
				{
					if (segment.Name.empty() || !IsEntryName(segment.Name))
					{
						Error = "has entries by name, of letters, digits and _";
						return false;
					}
					for (size_t i = 0; i < size; ++i)
					{
						if (Reflection::AccessProperty<std::string>(array.GetConstElement(address, i), *nameProperty) == segment.Name)
						{
							index = i;
							break;
						}
					}
					if (index == size)
					{
						Grew.push_back(Grown{ address, &array, size });
						array.Insert(address, size);
						void* entry = array.GetElement(address, size);
						ClearStructArrays(entry, elementType);
						Reflection::AccessProperty<std::string>(entry, *nameProperty) = std::string(segment.Name);
					}
					Path += "." + std::string(segment.Name);
				}
				else
				{
					if (!segment.Index || *segment.Index > MaxElementIndex)
					{
						Error = segment.Index ? "has no element as far as [" + std::to_string(*segment.Index) + "]" : "has elements by index: [0], [1], ...";
						return false;
					}
					index = *segment.Index;
					if (index >= size)
					{
						Grew.push_back(Grown{ address, &array, size });
						array.Resize(address, index + 1);
						for (size_t i = size; i <= index; ++i)
						{
							ClearStructArrays(array.GetElement(address, i), elementType);
						}
					}
					Path += "[" + std::to_string(index) + "]";
				}
				return InElement(array.GetElement(address, index), elementType, nameProperty, array.Element, segments.subspan(1), outTarget);
			}

			bool InElement(void* element, const TypeMetadata& type, const PropertyMetadata* nameProperty, const PropertyMetadata& elementValue,
				std::span<const KeySegment> segments, Target& outTarget)
			{
				const std::vector<const PropertyMetadata*> values = GetValueProperties(type, nameProperty);
				// An entry with one value besides its name is its line; with none, its line has no value
				if (values.size() == 1)
				{
					Path += "." + std::string(values.front()->Name);
					return InValue(Reflection::GetPropertyAddress(element, *values.front()), *values.front(), *values.front(), segments, outTarget);
				}
				if (values.empty() || IsInline(type, nameProperty))
				{
					if (!segments.empty())
					{
						Error = values.empty() ? "is a name alone: one line" : "is one line: its values apart by spaces";
						return false;
					}
					outTarget = Target{ .Address = element, .Value = values.empty() ? nullptr : &elementValue, .Owner = &elementValue, .InlineType = values.empty() ? nullptr : &type, .InlineSkip = nameProperty };
					return true;
				}
				return InStruct(element, type, nameProperty, segments, outTarget);
			}

			// The arrays grown on the way, as they were
			void Undo()
			{
				for (auto grown = Grew.rbegin(); grown != Grew.rend(); ++grown)
				{
					grown->Metadata->Resize(grown->Array, grown->Size);
				}
				Grew.clear();
			}
		};

		bool Assign(const Target& target, std::string_view text, std::string& outError)
		{
			if (target.InlineType)
			{
				return ParseInline(target.Address, *target.InlineType, target.InlineSkip, text, outError);
			}
			if (!target.Value)
			{
				if (!text.empty())
				{
					outError = "is a name alone, without a value";
					return false;
				}
				return true;
			}
			if (const ArrayMetadata* array = Reflection::TryGetArray(*target.Value))
			{
				return ParseList(target.Address, *array, *target.Owner, text, outError);
			}
			return ParseValue(target.Address, *target.Value, *target.Owner, text, outError);
		}

		// Finds what a line's name names in `object`, making what it names on the way
		bool Resolve(void* object, const TypeMetadata& type, std::string_view key, KeyResolver& resolver, Target& outTarget)
		{
			std::vector<KeySegment> segments;
			if (!SplitKey(key, segments))
			{
				resolver.Error = Quoted(key) + " isn't a name of a value";
				return false;
			}
			return resolver.InStruct(object, type, nullptr, segments, outTarget);
		}

		// Whether the type knows the line's name at all, by its first name
		bool IsKnownName(const TypeMetadata& type, std::string_view key)
		{
			return FindByTextName(type, GetTopName(key), nullptr) != nullptr;
		}

		// A value as the text writes it
		struct Leaf
		{
			std::string Key;
			std::string Text;
			// The innermost entry or element it is in, by its key; empty for none
			std::string Element;
		};

		// Every value of an object as its lines would have it, in order, and its elements' names
		struct LeafCollector
		{
			std::vector<Leaf> Leaves;
			std::vector<std::string> Elements;

			void Struct(const void* object, const TypeMetadata& type, const PropertyMetadata* skip, const std::string& prefix, const std::string& element)
			{
				for (const PropertyMetadata* property : GetValueProperties(type, skip))
				{
					Value(Reflection::GetPropertyAddress(object, *property), *property, prefix + std::string(GetTextName(*property)), element);
				}
			}

			void Value(const void* address, const PropertyMetadata& value, const std::string& key, const std::string& element)
			{
				if (const ArrayMetadata* array = Reflection::TryGetArray(value))
				{
					const TypeMetadata* elementType = GetElementStruct(*array);
					if (!elementType)
					{
						Leaves.push_back(Leaf{ key, FormatList(address, *array), element });
						return;
					}
					const PropertyMetadata* nameProperty = FindEntryNameProperty(*elementType);
					std::unordered_set<std::string> names;
					for (size_t i = 0; i < array->GetSize(address); ++i)
					{
						const void* element = array->GetConstElement(address, i);
						std::string elementKey;
						if (nameProperty)
						{
							const std::string& name = Reflection::AccessProperty<std::string>(element, *nameProperty);
							// Left out: an entry whose name can't be written, and a second of a name
							if (!IsEntryName(name) || !names.insert(name).second)
							{
								continue;
							}
							elementKey = key + "." + name;
						}
						else
						{
							elementKey = key + "[" + std::to_string(i) + "]";
						}
						Elements.push_back(elementKey);
						Element(element, *elementType, nameProperty, elementKey);
					}
					return;
				}

				if (const TypeMetadata* type = GetStructOfValues(value))
				{
					if (IsInline(*type, nullptr))
					{
						Leaves.push_back(Leaf{ key, FormatInline(address, *type, nullptr), element });
					}
					else
					{
						Struct(address, *type, nullptr, key + ".", element);
					}
					return;
				}
				Leaves.push_back(Leaf{ key, FormatValue(address, value, false), element });
			}

			void Element(const void* element, const TypeMetadata& type, const PropertyMetadata* nameProperty, const std::string& key)
			{
				const std::vector<const PropertyMetadata*> values = GetValueProperties(type, nameProperty);
				if (values.size() == 1)
				{
					Value(Reflection::GetPropertyAddress(element, *values.front()), *values.front(), key, key);
				}
				else if (values.empty())
				{
					Leaves.push_back(Leaf{ key, std::string(), key });
				}
				else if (IsInline(type, nameProperty))
				{
					Leaves.push_back(Leaf{ key, FormatInline(element, type, nameProperty), key });
				}
				else
				{
					Struct(element, type, nameProperty, key + ".", key);
				}
			}
		};

		// The text a value has in a new entry or element, by its key, read from `probe`, an object
		// whose entries and elements the keys make with their defaults
		std::string FormatDefault(void* probe, const TypeMetadata& type, std::string_view key)
		{
			KeyResolver resolver;
			Target target;
			if (!Resolve(probe, type, key, resolver, target) || !target.Value)
			{
				return {};
			}
			if (target.InlineType)
			{
				return FormatInline(target.Address, *target.InlineType, target.InlineSkip);
			}
			if (const ArrayMetadata* array = Reflection::TryGetArray(*target.Value))
			{
				return FormatList(target.Address, *array);
			}
			return FormatValue(target.Address, *target.Value, false);
		}

		// Whether `key` is in the entry or element `element`, or is it
		bool IsIn(std::string_view key, std::string_view element)
		{
			return key.starts_with(element) && (key.size() == element.size() || key[element.size()] == '.' || key[element.size()] == '[');
		}

		void CheckNames(const void* object, const TypeMetadata& type, const std::string& prefix, std::vector<PropertyProblem>& outProblems)
		{
			for (const PropertyMetadata* property : GetValueProperties(type, nullptr))
			{
				const void* address = Reflection::GetPropertyAddress(object, *property);
				const std::string path = prefix + property->Name;
				if (const ArrayMetadata* array = Reflection::TryGetArray(*property))
				{
					const TypeMetadata* elementType = GetElementStruct(*array);
					if (!elementType)
					{
						continue;
					}
					const PropertyMetadata* nameProperty = FindEntryNameProperty(*elementType);
					std::unordered_set<std::string> names;
					for (size_t i = 0; i < array->GetSize(address); ++i)
					{
						const void* element = array->GetConstElement(address, i);
						const std::string elementPath = GetElementPath(path, element, elementType, i);
						if (nameProperty)
						{
							const std::string& name = Reflection::AccessProperty<std::string>(element, *nameProperty);
							const std::string namePath = elementPath + "." + nameProperty->Name;
							if (name.empty())
							{
								outProblems.push_back({ namePath, "needs a name" });
							}
							else if (!IsEntryName(name))
							{
								outProblems.push_back({ namePath, Quoted(name) + " can't be a name: letters, digits and _ only" });
							}
							else if (!names.insert(name).second)
							{
								outProblems.push_back({ namePath, "another entry above has the name " + Quoted(name) });
							}
						}
						CheckNames(element, *elementType, elementPath + ".", outProblems);
					}
				}
				else if (const TypeMetadata* nested = GetStructOfValues(*property))
				{
					CheckNames(address, *nested, path + ".", outProblems);
				}
			}
		}
	}

	ReflectedObjectPtr MakeReflectedObject(const ReflectedStruct& type)
	{
		return ReflectedObjectPtr(type.Create(), type.Destroy);
	}

	bool IsEntryName(std::string_view name)
	{
		return !name.empty() && std::all_of(name.begin(), name.end(), [](char c)
			{
				return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
			});
	}

	const PropertyMetadata* FindEntryNameProperty(const TypeMetadata& type)
	{
		for (size_t i = 0; i < type.PropertyCount; ++i)
		{
			const PropertyMetadata& property = type.Properties[i];
			if (property.Kind == EPropertyKind::String && IsSerialized(property) && std::string_view(property.Name) == "Name")
			{
				return &property;
			}
		}
		return nullptr;
	}

	const PropertyMetadata* FindEntryNameProperty(const ArrayMetadata& array)
	{
		const TypeMetadata* elementType = GetElementStruct(array);
		return elementType ? FindEntryNameProperty(*elementType) : nullptr;
	}

	std::string MakeEntryName(const void* array, const ArrayMetadata& arrayMetadata, std::string_view base)
	{
		const PropertyMetadata* nameProperty = FindEntryNameProperty(arrayMetadata);
		const auto isTaken = [&](const std::string& name)
		{
			for (size_t i = 0; nameProperty && i < arrayMetadata.GetSize(array); ++i)
			{
				if (Reflection::AccessProperty<std::string>(arrayMetadata.GetConstElement(array, i), *nameProperty) == name)
				{
					return true;
				}
			}
			return false;
		};
		std::string name(base);
		for (int number = 2; isTaken(name); ++number)
		{
			name = std::string(base) + std::to_string(number);
		}
		return name;
	}

	std::string GetElementPath(std::string_view arrayPath, const void* element, const TypeMetadata* elementType, size_t index)
	{
		if (const PropertyMetadata* nameProperty = elementType ? FindEntryNameProperty(*elementType) : nullptr)
		{
			const std::string& name = Reflection::AccessProperty<std::string>(element, *nameProperty);
			if (IsEntryName(name))
			{
				return std::string(arrayPath) + "." + name;
			}
		}
		return std::string(arrayPath) + "[" + std::to_string(index) + "]";
	}

	void CheckEntryNames(const void* object, const TypeMetadata& type, std::vector<PropertyProblem>& outProblems)
	{
		CheckNames(object, type, "", outProblems);
	}

	bool ReflectedAssetText::Parse(std::string_view text, std::string& outError)
	{
		std::vector<Line> lines;
		size_t number = 0;
		while (!text.empty())
		{
			++number;
			const size_t end = text.find('\n');
			std::string_view raw = text.substr(0, end);
			text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
			if (!raw.empty() && raw.back() == '\r')
			{
				raw.remove_suffix(1);
			}

			const std::string_view trimmed = Trim(raw);
			if (trimmed.empty() || trimmed.front() == '#')
			{
				lines.push_back(Line{ .Text = std::string(raw) });
			}
			else
			{
				const size_t equals = trimmed.find('=');
				const std::string_view name = equals == std::string_view::npos ? std::string_view() : Trim(trimmed.substr(0, equals));
				if (name.empty())
				{
					outError = "line " + std::to_string(number) + ": expected \"Name = value\", found \"" + std::string(trimmed) + "\"";
					return false;
				}
				lines.push_back(Line{ .Name = std::string(name), .Value = std::string(Trim(trimmed.substr(equals + 1))), .Text = std::string(raw) });
			}
		}
		Lines = std::move(lines);
		return true;
	}

	std::string ReflectedAssetText::ToText() const
	{
		std::string text;
		for (const Line& line : Lines)
		{
			if (line.Name.empty() || !line.Text.empty())
			{
				text += line.Text;
			}
			else
			{
				text += line.Value.empty() ? line.Name + " =" : line.Name + " = " + line.Value;
			}
			text += '\n';
		}
		return text;
	}

	bool ReflectedAssetText::Load(const std::filesystem::path& file, std::string& outError)
	{
		std::ifstream stream(file, std::ios::binary);
		if (!stream)
		{
			outError = "can't open " + file.string();
			return false;
		}
		std::stringstream text;
		text << stream.rdbuf();
		if (!Parse(text.str(), outError))
		{
			outError = file.string() + ", " + outError;
			return false;
		}
		return true;
	}

	bool ReflectedAssetText::Save(const std::filesystem::path& file, std::string& outError) const
	{
		std::ofstream stream(file, std::ios::binary | std::ios::trunc);
		stream << ToText();
		if (!stream)
		{
			outError = "can't write " + file.string();
			return false;
		}
		return true;
	}

	void ReflectedAssetText::Read(void* object, const TypeMetadata& type, std::vector<PropertyProblem>* outProblems) const
	{
		ClearStructArrays(object, type);
		for (const Line& line : Lines)
		{
			// A name the type doesn't know at all is kept for a later Nyx or a game's own tools
			if (line.Name.empty() || !IsKnownName(type, line.Name))
			{
				continue;
			}
			KeyResolver resolver;
			Target target;
			std::string error;
			if (Resolve(object, type, line.Name, resolver, target) && Assign(target, line.Value, error))
			{
				continue;
			}
			resolver.Undo();
			if (outProblems)
			{
				outProblems->push_back(PropertyProblem{ resolver.Path.empty() ? line.Name : resolver.Path, resolver.Error.empty() ? error : resolver.Error });
			}
		}
	}

	void ReflectedAssetText::Write(const void* object, const ReflectedStruct& type)
	{
		Write(object, type, false);
	}

	void ReflectedAssetText::WriteAll(const void* object, const ReflectedStruct& type)
	{
		Write(object, type, true);
	}

	void ReflectedAssetText::Write(const void* object, const ReflectedStruct& type, bool bAll)
	{
		const TypeMetadata& metadata = *type.Metadata;
		LeafCollector wanted;
		wanted.Struct(object, metadata, nullptr, "", "");
		std::unordered_set<std::string> wantedKeys;
		for (const Leaf& leaf : wanted.Leaves)
		{
			wantedKeys.insert(leaf.Key);
		}

		// What the lines say now: a value that reads the same keeps its line as written; a value
		// without a line reads as its default, or not at all in an entry the lines don't have yet
		const ReflectedObjectPtr current = MakeReflectedObject(type);
		Read(current.get(), metadata);
		LeafCollector now;
		now.Struct(current.get(), metadata, nullptr, "", "");
		std::unordered_map<std::string, std::string> nowTexts;
		for (Leaf& leaf : now.Leaves)
		{
			nowTexts.emplace(std::move(leaf.Key), std::move(leaf.Text));
		}

		// The lines of values the object no longer has: entries taken away, elements past its end
		const ReflectedObjectPtr probe = MakeReflectedObject(type);
		ClearStructArrays(probe.get(), metadata);
		std::erase_if(Lines, [&](const Line& line)
			{
				if (line.Name.empty() || wantedKeys.contains(line.Name) || !IsKnownName(metadata, line.Name))
				{
					return false;
				}
				KeyResolver resolver;
				Target target;
				return Resolve(probe.get(), metadata, line.Name, resolver, target);
			});

		// A new entry or element writes the values it doesn't have at their defaults, or its first value
		// when it has all, unless an entry or element inside it has lines
		std::unordered_set<std::string> defaults;
		if (!bAll)
		{
			std::unordered_map<std::string, std::vector<const Leaf*>> newElements;
			for (const Leaf& leaf : wanted.Leaves)
			{
				if (!leaf.Element.empty() && !nowTexts.contains(leaf.Key))
				{
					newElements[leaf.Element].push_back(&leaf);
				}
			}
			for (const auto& [element, leaves] : newElements)
			{
				std::vector<const Leaf*> atDefault;
				for (const Leaf* leaf : leaves)
				{
					if (leaf->Text == FormatDefault(probe.get(), metadata, leaf->Key))
					{
						atDefault.push_back(leaf);
					}
				}
				const bool bHasInner = std::any_of(wanted.Elements.begin(), wanted.Elements.end(), [&element](const std::string& other)
					{
						return other.size() > element.size() && IsIn(other, element);
					});
				if (atDefault.size() == leaves.size() && !bHasInner)
				{
					atDefault.erase(atDefault.begin());
				}
				for (const Leaf* leaf : atDefault)
				{
					defaults.insert(leaf->Key);
				}
			}
		}

		// Each value: its line changed where it reads differently, a line added where it has none
		for (const Leaf& leaf : wanted.Leaves)
		{
			const auto written = nowTexts.find(leaf.Key);
			if (!bAll && ((written != nowTexts.end() && written->second == leaf.Text) || defaults.contains(leaf.Key)))
			{
				continue;
			}
			const auto line = std::find_if(Lines.begin(), Lines.end(), [&leaf](const Line& candidate)
				{
					return candidate.Name == leaf.Key;
				});
			if (line == Lines.end())
			{
				Lines.insert(Lines.begin() + static_cast<std::ptrdiff_t>(FindInsertPosition(leaf.Key)), Line{ .Name = leaf.Key, .Value = leaf.Text });
				continue;
			}
			line->Value = leaf.Text;
			line->Text.clear();
			// One line a name: the first takes the value, later ones go
			const std::ptrdiff_t first = line - Lines.begin();
			Lines.erase(std::remove_if(Lines.begin() + first + 1, Lines.end(), [&leaf](const Line& candidate)
							{
								return candidate.Name == leaf.Key;
							}),
				Lines.end());
		}

		// Elements moved, or put in above others: their array's lines in the object's order, where the
		// array's first line was; other lines between them stay where they are
		const ReflectedObjectPtr written = MakeReflectedObject(type);
		Read(written.get(), metadata);
		LeafCollector after;
		after.Struct(written.get(), metadata, nullptr, "", "");
		if (after.Elements == wanted.Elements)
		{
			return;
		}
		for (const PropertyMetadata* property : GetValueProperties(metadata, nullptr))
		{
			const std::string_view name = GetTextName(*property);
			const auto ofProperty = [name](const std::vector<std::string>& elements)
			{
				std::vector<std::string> mine;
				std::copy_if(elements.begin(), elements.end(), std::back_inserter(mine), [name](const std::string& element)
					{
						return GetTopName(element) == name;
					});
				return mine;
			};
			if (ofProperty(after.Elements) == ofProperty(wanted.Elements))
			{
				continue;
			}

			std::unordered_map<std::string, Line> moving;
			std::vector<Line> lines;
			std::optional<size_t> at;
			for (Line& line : Lines)
			{
				if (line.Name.empty() || GetTopName(line.Name) != name || !wantedKeys.contains(line.Name))
				{
					lines.push_back(std::move(line));
					continue;
				}
				if (!at)
				{
					at = lines.size();
				}
				moving[line.Name] = std::move(line);
			}
			std::vector<Line> ordered;
			for (const Leaf& leaf : wanted.Leaves)
			{
				if (const auto found = moving.find(leaf.Key); found != moving.end())
				{
					ordered.push_back(std::move(found->second));
				}
			}
			lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(at.value_or(lines.size())), std::make_move_iterator(ordered.begin()),
				std::make_move_iterator(ordered.end()));
			Lines = std::move(lines);
		}
	}

	size_t ReflectedAssetText::FindInsertPosition(std::string_view key) const
	{
		// The key's struct, entry or element, then theirs, nearest first: "Moves.Bite.Damage" goes after
		// Moves.Bite's lines, or else after Moves' lines
		for (size_t end = key.find_last_of(".["); end != std::string_view::npos && end > 0; end = key.find_last_of(".[", end - 1))
		{
			const std::string_view parent = key.substr(0, end);
			for (size_t i = Lines.size(); i-- > 0;)
			{
				const std::string& name = Lines[i].Name;
				if (name.size() > parent.size() && name.starts_with(parent) && (name[parent.size()] == '.' || name[parent.size()] == '['))
				{
					return i + 1;
				}
			}
		}
		return Lines.size();
	}

	const std::string* ReflectedAssetText::FindValue(std::string_view name) const
	{
		for (const Line& line : Lines)
		{
			if (!line.Name.empty() && line.Name == name)
			{
				return &line.Value;
			}
		}
		return nullptr;
	}

	void ReflectedAssetText::SetValue(std::string_view name, std::string value)
	{
		for (Line& line : Lines)
		{
			if (!line.Name.empty() && line.Name == name)
			{
				line.Value = std::move(value);
				line.Text.clear();
				return;
			}
		}
		Lines.push_back(Line{ .Name = std::string(name), .Value = std::move(value) });
	}

	bool ReflectedAssetText::RemoveValue(std::string_view name)
	{
		const auto found = std::find_if(Lines.begin(), Lines.end(), [name](const Line& line)
			{
				return !line.Name.empty() && line.Name == name;
			});
		if (found == Lines.end())
		{
			return false;
		}
		Lines.erase(found);
		return true;
	}

	void ReflectedAssetText::AddComment(std::string_view comment)
	{
		Lines.push_back(Line{ .Text = "# " + std::string(comment) });
	}
}
