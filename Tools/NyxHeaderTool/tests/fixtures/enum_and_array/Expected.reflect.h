#pragma once

#include "ReflectionTypes.h"
#include "ReflectionArrays.h"

namespace Nyx::Reflection
{
    template<>
    const EnumMetadata& GetEnumMetadata<Nyx::EDeadZoneShape>();

    template<>
    const EnumMetadata& GetEnumMetadata<Nyx::ESigned>();

    template<>
    const TypeMetadata& GetTypeMetadata<Nyx::ArrayElement>();

    template<>
    const TypeMetadata& GetTypeMetadata<Nyx::EnumAndArrayTest>();

}

namespace Nyx::Reflection::Generated
{
    inline constexpr EnumEntry EDeadZoneShape_EnumEntries[] =
    {
        { "Axial", 0 },
        { "Radial", 1 },
        { "ScaledRadial", 5 },
        { "Hybrid", 6 },
    };

    inline constexpr EnumMetadata EDeadZoneShape_EnumMetadata
    {
        "Nyx::EDeadZoneShape",
        "Dead Zone Shape",
        EDeadZoneShape_EnumEntries,
        sizeof(EDeadZoneShape_EnumEntries) / sizeof(EDeadZoneShape_EnumEntries[0]),
        sizeof(Nyx::EDeadZoneShape),
        std::is_signed_v<std::underlying_type_t<Nyx::EDeadZoneShape>>
    };

    inline constexpr EnumEntry ESigned_EnumEntries[] =
    {
        { "Below", -2 },
        { "Zero", 0 },
    };

    inline constexpr EnumMetadata ESigned_EnumMetadata
    {
        "Nyx::ESigned",
        "ESigned",
        ESigned_EnumEntries,
        sizeof(ESigned_EnumEntries) / sizeof(ESigned_EnumEntries[0]),
        sizeof(Nyx::ESigned),
        std::is_signed_v<std::underlying_type_t<Nyx::ESigned>>
    };

    inline constexpr PropertyMetadata ArrayElement_Properties[] =
    {
        {
            "Name",
            0x0FE07306,
            "Name",
            EPropertyKind::String,
            EPropertyFlags::Edit | EPropertyFlags::Serialize,
            offsetof(Nyx::ArrayElement, Name),
            nullptr,
            0,
            nullptr
        },
    };

    inline constexpr TypeMetadata ArrayElement_TypeMetadata
    {
        "Nyx::ArrayElement",
        "ArrayElement",
        EReflectedTypeRole::Plain,
        nullptr,
        0,
        ArrayElement_Properties,
        sizeof(ArrayElement_Properties) / sizeof(ArrayElement_Properties[0])
    };

    inline constexpr MetadataEntry EnumAndArrayTest_Shape_MetadataEntries[] =
    {
        { "Category", "Debug" },
    };

    inline const EnumMetadata& EnumAndArrayTest_Shape_EnumResolver()
    {
        return GetEnumMetadata<Nyx::EDeadZoneShape>();
    }

    inline constexpr ArrayMetadata EnumAndArrayTest_Floats_Array
    {
        {
            "Element",
            0,
            "Element",
            EPropertyKind::Float,
            EPropertyFlags::Edit | EPropertyFlags::Undo | EPropertyFlags::Serialize,
            0,
            nullptr,
            0,
            nullptr,
            nullptr,
            nullptr
        },
        &VectorArrayOps<float>::GetSize,
        &VectorArrayOps<float>::Resize,
        &VectorArrayOps<float>::GetElement,
        &VectorArrayOps<float>::GetConstElement,
        &VectorArrayOps<float>::Insert,
        &VectorArrayOps<float>::Erase,
        &VectorArrayOps<float>::Swap
    };

    static_assert(std::is_same_v<decltype(Nyx::EnumAndArrayTest::Floats), std::vector<float>>,
        "EnumAndArrayTest::Floats must be a std::vector<float>");

    inline constexpr ArrayMetadata EnumAndArrayTest_Points_Array
    {
        {
            "Element",
            0,
            "Element",
            EPropertyKind::Vec2,
            EPropertyFlags::Edit | EPropertyFlags::Undo | EPropertyFlags::Serialize,
            0,
            nullptr,
            0,
            nullptr,
            nullptr,
            nullptr
        },
        &VectorArrayOps<glm::vec2>::GetSize,
        &VectorArrayOps<glm::vec2>::Resize,
        &VectorArrayOps<glm::vec2>::GetElement,
        &VectorArrayOps<glm::vec2>::GetConstElement,
        &VectorArrayOps<glm::vec2>::Insert,
        &VectorArrayOps<glm::vec2>::Erase,
        &VectorArrayOps<glm::vec2>::Swap
    };

    static_assert(std::is_same_v<decltype(Nyx::EnumAndArrayTest::Points), std::vector<glm::vec2>>,
        "EnumAndArrayTest::Points must be a std::vector<glm::vec2>");

    inline const EnumMetadata& EnumAndArrayTest_Signs_ElementEnumResolver()
    {
        return GetEnumMetadata<Nyx::ESigned>();
    }

    inline constexpr ArrayMetadata EnumAndArrayTest_Signs_Array
    {
        {
            "Element",
            0,
            "Element",
            EPropertyKind::Enum,
            EPropertyFlags::Edit | EPropertyFlags::Undo | EPropertyFlags::Serialize,
            0,
            nullptr,
            0,
            nullptr,
            &EnumAndArrayTest_Signs_ElementEnumResolver,
            nullptr
        },
        &VectorArrayOps<Nyx::ESigned>::GetSize,
        &VectorArrayOps<Nyx::ESigned>::Resize,
        &VectorArrayOps<Nyx::ESigned>::GetElement,
        &VectorArrayOps<Nyx::ESigned>::GetConstElement,
        &VectorArrayOps<Nyx::ESigned>::Insert,
        &VectorArrayOps<Nyx::ESigned>::Erase,
        &VectorArrayOps<Nyx::ESigned>::Swap
    };

    static_assert(std::is_same_v<decltype(Nyx::EnumAndArrayTest::Signs), std::vector<Nyx::ESigned>>,
        "EnumAndArrayTest::Signs must be a std::vector<Nyx::ESigned>");

    inline const TypeMetadata& EnumAndArrayTest_Elements_ElementTypeResolver()
    {
        return GetTypeMetadata<Nyx::ArrayElement>();
    }

    inline constexpr ArrayMetadata EnumAndArrayTest_Elements_Array
    {
        {
            "Element",
            0,
            "Element",
            EPropertyKind::Struct,
            EPropertyFlags::Edit | EPropertyFlags::Undo | EPropertyFlags::Serialize,
            0,
            nullptr,
            0,
            &EnumAndArrayTest_Elements_ElementTypeResolver,
            nullptr,
            nullptr
        },
        &VectorArrayOps<Nyx::ArrayElement>::GetSize,
        &VectorArrayOps<Nyx::ArrayElement>::Resize,
        &VectorArrayOps<Nyx::ArrayElement>::GetElement,
        &VectorArrayOps<Nyx::ArrayElement>::GetConstElement,
        &VectorArrayOps<Nyx::ArrayElement>::Insert,
        &VectorArrayOps<Nyx::ArrayElement>::Erase,
        &VectorArrayOps<Nyx::ArrayElement>::Swap
    };

    static_assert(std::is_same_v<decltype(Nyx::EnumAndArrayTest::Elements), std::vector<Nyx::ArrayElement>>,
        "EnumAndArrayTest::Elements must be a std::vector<Nyx::ArrayElement>");

    inline constexpr PropertyMetadata EnumAndArrayTest_Properties[] =
    {
        {
            "Shape",
            0x4380DDC6,
            "Shape",
            EPropertyKind::Enum,
            EPropertyFlags::Edit | EPropertyFlags::Undo | EPropertyFlags::Serialize,
            offsetof(Nyx::EnumAndArrayTest, Shape),
            EnumAndArrayTest_Shape_MetadataEntries,
            sizeof(EnumAndArrayTest_Shape_MetadataEntries) / sizeof(EnumAndArrayTest_Shape_MetadataEntries[0]),
            nullptr,
            &EnumAndArrayTest_Shape_EnumResolver
        },
        {
            "Floats",
            0xC5ADCD62,
            "Floats",
            EPropertyKind::Array,
            EPropertyFlags::Edit | EPropertyFlags::Undo | EPropertyFlags::Serialize,
            offsetof(Nyx::EnumAndArrayTest, Floats),
            nullptr,
            0,
            nullptr,
            nullptr,
            &EnumAndArrayTest_Floats_Array
        },
        {
            "Points",
            0xA9F0A4E6,
            "Points",
            EPropertyKind::Array,
            EPropertyFlags::Edit | EPropertyFlags::Undo | EPropertyFlags::Serialize,
            offsetof(Nyx::EnumAndArrayTest, Points),
            nullptr,
            0,
            nullptr,
            nullptr,
            &EnumAndArrayTest_Points_Array
        },
        {
            "Signs",
            0x703EE015,
            "Signs",
            EPropertyKind::Array,
            EPropertyFlags::Edit | EPropertyFlags::Undo | EPropertyFlags::Serialize,
            offsetof(Nyx::EnumAndArrayTest, Signs),
            nullptr,
            0,
            nullptr,
            nullptr,
            &EnumAndArrayTest_Signs_Array
        },
        {
            "Elements",
            0x0236F8EC,
            "Elements",
            EPropertyKind::Array,
            EPropertyFlags::Edit | EPropertyFlags::Undo | EPropertyFlags::Serialize,
            offsetof(Nyx::EnumAndArrayTest, Elements),
            nullptr,
            0,
            nullptr,
            nullptr,
            &EnumAndArrayTest_Elements_Array
        },
    };

    inline constexpr TypeMetadata EnumAndArrayTest_TypeMetadata
    {
        "Nyx::EnumAndArrayTest",
        "EnumAndArrayTest",
        EReflectedTypeRole::Plain,
        nullptr,
        0,
        EnumAndArrayTest_Properties,
        sizeof(EnumAndArrayTest_Properties) / sizeof(EnumAndArrayTest_Properties[0])
    };

}

namespace Nyx::Reflection
{
    template<>
    inline const EnumMetadata& GetEnumMetadata<Nyx::EDeadZoneShape>()
    {
        return Generated::EDeadZoneShape_EnumMetadata;
    }

    template<>
    inline const EnumMetadata& GetEnumMetadata<Nyx::ESigned>()
    {
        return Generated::ESigned_EnumMetadata;
    }

    template<>
    inline const TypeMetadata& GetTypeMetadata<Nyx::ArrayElement>()
    {
        return Generated::ArrayElement_TypeMetadata;
    }

    template<>
    inline const TypeMetadata& GetTypeMetadata<Nyx::EnumAndArrayTest>()
    {
        return Generated::EnumAndArrayTest_TypeMetadata;
    }

}
