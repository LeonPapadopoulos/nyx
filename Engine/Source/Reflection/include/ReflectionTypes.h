#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace Nyx::Reflection
{
	// The numbers are stored in scene files and messages, so they must never change:
	// add new kinds at the end, and update LastPropertyKind. A new kind also needs:
	// - header tool: EParsedPropertyKind, the type table (ReflectionSemantics.cpp),
	//   CodeGenerator::ToGeneratedPropertyKind, and a test fixture
	// - PropertyValue (PropertyValue.h), ArePropertyValuesEqual (PropertyValueUtils.h),
	//   GetPropertyKindName (ReflectionUtils.cpp)
	// - ReflectedArchiveSerializer: GetPropertyValue, SetPropertyValue, ReadValue, WriteValue, the
	//   value sizes in its header, whether IsNumberKind includes it, and BinaryWriter/Reader
	//   functions if the value has a new size
	// - ReflectedArchivePrinter::ValueToText
	// - in the editor: ReflectedPropertyAccess.cpp and a widget in PropertyWidgetRegistry.cpp
	//
	// UInt64 is meant for IDs, such as EntityGuid: it is shown in hex and never converted.
	enum class EPropertyKind : uint8_t
	{
		Unknown = 0,
		Bool = 1,
		Int32 = 2,
		UInt32 = 3,
		Float = 4,
		Vec2 = 5,
		Vec3 = 6,
		Vec4 = 7,
		Quat = 8,
		String = 9,
		Struct = 10,
		UInt64 = 11
	};

	inline constexpr EPropertyKind LastPropertyKind = EPropertyKind::UInt64;

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
			static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
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

	struct TypeMetadata;

	using NestedTypeResolverFn = const TypeMetadata& (*)();

	struct PropertyMetadata
	{
		const char* Name = "";

		// FNV-1a hash of Name, computed by the header tool, which also makes sure it is unique
		// within the type. Scene files and messages store this hash instead of the name.
		uint32_t NameHash = 0;

		const char* DisplayName = "";
		EPropertyKind Kind = EPropertyKind::Unknown;
		EPropertyFlags Flags = EPropertyFlags::None;

		size_t Offset = 0;

		const MetadataEntry* Metadata = nullptr;
		size_t MetadataCount = 0;

		NestedTypeResolverFn NestedTypeResolver = nullptr;
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
