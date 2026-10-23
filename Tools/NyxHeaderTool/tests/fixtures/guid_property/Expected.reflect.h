#pragma once

#include "ReflectionTypes.h"

namespace Nyx::Reflection
{
    template<>
    const TypeMetadata& GetTypeMetadata<Nyx::GuidPropertyTestComponent>();

}

namespace Nyx::Reflection::Generated
{
    inline constexpr MetadataEntry GuidPropertyTestComponent_Guid_MetadataEntries[] =
    {
        { "UI", "EntityRef" },
    };

    inline constexpr MetadataEntry GuidPropertyTestComponent_QualifiedGuid_MetadataEntries[] =
    {
        { "UI", "EntityRef" },
    };

    inline constexpr MetadataEntry GuidPropertyTestComponent_OwnGuid_MetadataEntries[] =
    {
        { "UI", "Hex" },
        { "Tooltip", "Its own" },
    };

    inline constexpr PropertyMetadata GuidPropertyTestComponent_Properties[] =
    {
        {
            "Guid",
            0xACC7CB2C,
            "Guid",
            EPropertyKind::UInt64,
            EPropertyFlags::Serialize | EPropertyFlags::ReadOnly,
            offsetof(Nyx::GuidPropertyTestComponent, Guid),
            GuidPropertyTestComponent_Guid_MetadataEntries,
            sizeof(GuidPropertyTestComponent_Guid_MetadataEntries) / sizeof(GuidPropertyTestComponent_Guid_MetadataEntries[0]),
            nullptr
        },
        {
            "QualifiedGuid",
            0xB15257D8,
            "QualifiedGuid",
            EPropertyKind::UInt64,
            EPropertyFlags::Serialize | EPropertyFlags::ReadOnly,
            offsetof(Nyx::GuidPropertyTestComponent, QualifiedGuid),
            GuidPropertyTestComponent_QualifiedGuid_MetadataEntries,
            sizeof(GuidPropertyTestComponent_QualifiedGuid_MetadataEntries) / sizeof(GuidPropertyTestComponent_QualifiedGuid_MetadataEntries[0]),
            nullptr
        },
        {
            "OwnGuid",
            0x6659654A,
            "OwnGuid",
            EPropertyKind::UInt64,
            EPropertyFlags::Serialize | EPropertyFlags::ReadOnly,
            offsetof(Nyx::GuidPropertyTestComponent, OwnGuid),
            GuidPropertyTestComponent_OwnGuid_MetadataEntries,
            sizeof(GuidPropertyTestComponent_OwnGuid_MetadataEntries) / sizeof(GuidPropertyTestComponent_OwnGuid_MetadataEntries[0]),
            nullptr
        },
        {
            "Big",
            0xB8D16159,
            "Big",
            EPropertyKind::UInt64,
            EPropertyFlags::Edit | EPropertyFlags::Serialize,
            offsetof(Nyx::GuidPropertyTestComponent, Big),
            nullptr,
            0,
            nullptr
        },
    };

    static_assert(std::is_same_v<decltype(Nyx::GuidPropertyTestComponent::Guid), Nyx::EntityGuid>,
        "GuidPropertyTestComponent::Guid must be a Nyx::EntityGuid");

    static_assert(std::is_same_v<decltype(Nyx::GuidPropertyTestComponent::QualifiedGuid), Nyx::EntityGuid>,
        "GuidPropertyTestComponent::QualifiedGuid must be a Nyx::EntityGuid");

    static_assert(std::is_same_v<decltype(Nyx::GuidPropertyTestComponent::OwnGuid), Nyx::EntityGuid>,
        "GuidPropertyTestComponent::OwnGuid must be a Nyx::EntityGuid");

    inline constexpr TypeMetadata GuidPropertyTestComponent_TypeMetadata
    {
        "Nyx::GuidPropertyTestComponent",
        "Guid Property Test",
        EReflectedTypeRole::Component,
        nullptr,
        0,
        GuidPropertyTestComponent_Properties,
        sizeof(GuidPropertyTestComponent_Properties) / sizeof(GuidPropertyTestComponent_Properties[0])
    };

}

namespace Nyx::Reflection
{
    template<>
    inline const TypeMetadata& GetTypeMetadata<Nyx::GuidPropertyTestComponent>()
    {
        return Generated::GuidPropertyTestComponent_TypeMetadata;
    }

}
