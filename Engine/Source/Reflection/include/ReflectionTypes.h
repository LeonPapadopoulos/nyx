#pragma once

#include <cstddef>
#include <cstdint>

namespace Nyx::Reflection
{
	enum class EPropertyKind : uint8_t
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

	enum class EPropertyFlags : uint32_t
	{
		None = 0,
		Edit = 1 << 0,
		Undo = 1 << 1,
		Serialize = 1 << 2,
		Hidden = 1 << 3,
		ReadOnly = 1 << 4
	};

	inline constexpr EPropertyFlags operator|(EPropertyFlags a, EPropertyFlags b)
	{
		return static_cast<EPropertyFlags>(
			static_cast<uint32_t>(a) | static_cast<uint32_t>(b)
			);
	}

	inline constexpr bool HasFlag(EPropertyFlags value, EPropertyFlags flag)
	{
		return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
	}

	enum class EReflectedTypeRole : uint8_t
	{
		Plain = 0,
		Component
	};

	struct MetadataEntry
	{
		const char* Key = "";
		const char* Value = "";
	};

	struct PropertyMetadata
	{
		const char* Name = "";
		const char* DisplayName = "";
		EPropertyKind Kind = EPropertyKind::Unknown;
		EPropertyFlags Flags = EPropertyFlags::None;

		size_t Offset = 0;

		const MetadataEntry* Metadata = nullptr;
		size_t MetadataCount = 0;
	};

	struct TypeMetadata
	{
		const char* Name = "";
		const char* DisplayName = "";
		EReflectedTypeRole Role = EReflectedTypeRole::Plain;

		const MetadataEntry* Metadata = nullptr;
		size_t MetadataCount = 0;

		const PropertyMetadata* Properties = nullptr;
		size_t PropertyCount = 0;
	};

	template<typename T>
	const TypeMetadata& GetTypeMetadata();
}