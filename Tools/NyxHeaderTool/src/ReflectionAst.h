#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Nyx::HeaderTool
{
	struct ParsedMacroEntry
	{
		std::string Name;
		std::optional<std::string> Value;
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
		String
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
			static_cast<uint32_t>(a) | static_cast<uint32_t>(b)
			);
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
		std::string Type;
		std::string Name;
		std::string DisplayName;

		ParsedMacroArguments RawArguments;

		EParsedPropertyFlags Flags = EParsedPropertyFlags::None;
		EParsedPropertyKind Kind = EParsedPropertyKind::Unknown;

		std::vector<ParsedMacroEntry> EmittedMetadata;
	};

	struct ParsedType
	{
		std::string Name;
		std::string QualifiedName;
		std::string DisplayName;
		EParsedTypeRole Role = EParsedTypeRole::Plain;

		ParsedMacroArguments RawArguments;
		std::vector<ParsedMacroEntry> EmittedMetadata;

		std::vector<ParsedProperty> Properties;
	};

	struct ParsedHeader
	{
		std::vector<ParsedType> Types;
	};

	struct ScannedHeader
	{
		std::filesystem::path HeaderPath;
		std::filesystem::path RelativePath;
		ParsedHeader Parsed;
	};
}