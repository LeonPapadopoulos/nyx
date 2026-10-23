#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Nyx::HeaderTool
{
	enum class EMacroValueKind : uint8_t
	{
		None = 0,
		String,
		Identifier,
		Number
	};

	struct ParsedValue
	{
		EMacroValueKind Kind = EMacroValueKind::None;

		// For String / Identifier / Number textual round-tripping.
		std::string Text;

		// Only meaningful when Kind == Number.
		double Number = 0.0;
	};

	struct ParsedMacroEntry
	{
		std::string Name;
		std::optional<ParsedValue> Value;
	};

	struct ParsedMacroArguments
	{
		std::vector<ParsedMacroEntry> Specifiers;
		std::vector<ParsedMacroEntry> Metadata;
	};

	enum class EParsedTypeRole : uint8_t
	{
		Plain = 0,
		Component
	};

	enum class EParsedPropertyKind : uint8_t
	{
		Unknown = 0,
		Bool,
		Int32,
		UInt32,
		Float,
		Vec2,
		Vec3,
		Vec4,
		Quat,
		String,
		Struct,
		UInt64,
		Enum,
		Array
	};

	enum class EParsedPropertyFlags : uint32_t
	{
		None = 0,
		Edit = 1 << 0,
		Undo = 1 << 1,
		Serialize = 1 << 2,
		Hidden = 1 << 3,
		ReadOnly = 1 << 4
	};

	inline constexpr EParsedPropertyFlags operator|(EParsedPropertyFlags a, EParsedPropertyFlags b)
	{
		return static_cast<EParsedPropertyFlags>(
			static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
	}

	inline constexpr EParsedPropertyFlags& operator|=(EParsedPropertyFlags& a, EParsedPropertyFlags b)
	{
		a = a | b;
		return a;
	}

	inline constexpr bool HasFlag(EParsedPropertyFlags value, EParsedPropertyFlags flag)
	{
		return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
	}

	struct ParsedProperty
	{
		uint32_t SourceLine = 0;
		std::string Type;
		std::string Name;
		std::string DisplayName;

		// FNV-1a hash of Name, set by ReflectionSemantics
		uint32_t NameHash = 0;

		ParsedMacroArguments RawArguments;

		EParsedPropertyFlags Flags = EParsedPropertyFlags::None;
		EParsedPropertyKind Kind = EParsedPropertyKind::Unknown;

		std::string StructQualifiedTypeName;

		// Enum: the NYX_ENUM's qualified name
		std::string EnumQualifiedTypeName;

		// Array (std::vector<T>): what T is, as for a property of its own, and T as C++ names it
		EParsedPropertyKind ElementKind = EParsedPropertyKind::Unknown;
		std::string ElementCppType;
		std::string ElementStructQualifiedTypeName;
		std::string ElementEnumQualifiedTypeName;

		// For engine types the tool recognises by name (see ReflectionSemantics), the C++ type the
		// member must really have. The generated code checks it.
		std::string RequiredCppType;

		// For those types too: the UI metadata the property gets unless it names its own, e.g.
		// EntityRef for an EntityGuid, which the editor draws as an entity picker
		std::string DefaultUI;

		std::vector<ParsedMacroEntry> EmittedMetadata;
	};

	struct ParsedType
	{
		uint32_t SourceLine = 0;
		std::string Name;
		std::string QualifiedName;
		std::string DisplayName;
		EParsedTypeRole Role = EParsedTypeRole::Plain;

		ParsedMacroArguments RawArguments;
		std::vector<ParsedMacroEntry> EmittedMetadata;

		std::vector<ParsedProperty> Properties;
	};

	struct ParsedEnumEntry
	{
		std::string Name;
		int64_t Value = 0;
	};

	// An enum class declared with NYX_ENUM
	struct ParsedEnum
	{
		uint32_t SourceLine = 0;
		std::string Name;
		std::string QualifiedName;
		std::string DisplayName;
		ParsedMacroArguments RawArguments;
		std::vector<ParsedEnumEntry> Entries;
	};

	struct ParsedHeader
	{
		std::string SourceFile;
		std::vector<ParsedType> Types;
		std::vector<ParsedEnum> Enums;
	};

	struct ScannedHeader
	{
		std::filesystem::path HeaderPath;
		std::filesystem::path RelativePath;
		ParsedHeader Parsed;
	};
}
