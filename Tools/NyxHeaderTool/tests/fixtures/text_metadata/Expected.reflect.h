#pragma once

#include "ReflectionTypes.h"
#include "ReflectionArrays.h"

namespace Nyx::Reflection
{
    template<>
    const TypeMetadata& GetTypeMetadata<Nyx::TextRange>();

    template<>
    const TypeMetadata& GetTypeMetadata<Nyx::TextMetadataTest>();

}

namespace Nyx::Reflection::Generated
{
    inline constexpr MetadataEntry TextRange_TypeMetadataEntries[] =
    {
        { "Inline", "" },
    };

    inline constexpr PropertyMetadata TextRange_Properties[] =
    {
        {
            "Name",
            0x0FE07306,
            "Name",
            EPropertyKind::String,
            EPropertyFlags::Edit | EPropertyFlags::Undo | EPropertyFlags::Serialize,
            offsetof(Nyx::TextRange, Name),
            nullptr,
            0,
            nullptr
        },
        {
            "Default",
            0x19DC307E,
            "Default",
            EPropertyKind::Float,
            EPropertyFlags::Edit | EPropertyFlags::Undo | EPropertyFlags::Serialize,
            offsetof(Nyx::TextRange, Default),
            nullptr,
            0,
            nullptr
        },
        {
            "Min",
            0x2E9445F7,
            "Min",
            EPropertyKind::Float,
            EPropertyFlags::Edit | EPropertyFlags::Undo | EPropertyFlags::Serialize,
            offsetof(Nyx::TextRange, Min),
            nullptr,
            0,
            nullptr
        },
    };

    inline constexpr TypeMetadata TextRange_TypeMetadata
    {
        "Nyx::TextRange",
        "TextRange",
        EReflectedTypeRole::Plain,
        TextRange_TypeMetadataEntries,
        sizeof(TextRange_TypeMetadataEntries) / sizeof(TextRange_TypeMetadataEntries[0]),
        TextRange_Properties,
        sizeof(TextRange_Properties) / sizeof(TextRange_Properties[0])
    };

    inline constexpr MetadataEntry TextMetadataTest_Ranges_MetadataEntries[] =
    {
        { "TextName", "Range" },
        { "Tooltip", "Lines named Range.<Name>" },
    };

    inline const TypeMetadata& TextMetadataTest_Ranges_ElementTypeResolver()
    {
        return GetTypeMetadata<Nyx::TextRange>();
    }

    inline constexpr ArrayMetadata TextMetadataTest_Ranges_Array
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
            &TextMetadataTest_Ranges_ElementTypeResolver,
            nullptr,
            nullptr
        },
        &VectorArrayOps<Nyx::TextRange>::GetSize,
        &VectorArrayOps<Nyx::TextRange>::Resize,
        &VectorArrayOps<Nyx::TextRange>::GetElement,
        &VectorArrayOps<Nyx::TextRange>::GetConstElement,
        &VectorArrayOps<Nyx::TextRange>::Insert,
        &VectorArrayOps<Nyx::TextRange>::Erase,
        &VectorArrayOps<Nyx::TextRange>::Swap
    };

    static_assert(std::is_same_v<decltype(Nyx::TextMetadataTest::Ranges), std::vector<Nyx::TextRange>>,
        "TextMetadataTest::Ranges must be a std::vector<Nyx::TextRange>");

    inline constexpr PropertyMetadata TextMetadataTest_Properties[] =
    {
        {
            "Ranges",
            0xB62EA693,
            "Ranges",
            EPropertyKind::Array,
            EPropertyFlags::Edit | EPropertyFlags::Undo | EPropertyFlags::Serialize,
            offsetof(Nyx::TextMetadataTest, Ranges),
            TextMetadataTest_Ranges_MetadataEntries,
            sizeof(TextMetadataTest_Ranges_MetadataEntries) / sizeof(TextMetadataTest_Ranges_MetadataEntries[0]),
            nullptr,
            nullptr,
            &TextMetadataTest_Ranges_Array
        },
    };

    inline constexpr TypeMetadata TextMetadataTest_TypeMetadata
    {
        "Nyx::TextMetadataTest",
        "TextMetadataTest",
        EReflectedTypeRole::Plain,
        nullptr,
        0,
        TextMetadataTest_Properties,
        sizeof(TextMetadataTest_Properties) / sizeof(TextMetadataTest_Properties[0])
    };

}

namespace Nyx::Reflection
{
    template<>
    inline const TypeMetadata& GetTypeMetadata<Nyx::TextRange>()
    {
        return Generated::TextRange_TypeMetadata;
    }

    template<>
    inline const TypeMetadata& GetTypeMetadata<Nyx::TextMetadataTest>()
    {
        return Generated::TextMetadataTest_TypeMetadata;
    }

}
