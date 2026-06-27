#pragma once

#include "ReflectionTypes.h"

namespace Nyx::Reflection
{
    template<>
    const TypeMetadata& GetTypeMetadata<Nyx::Engine::NameComponent>();

}

namespace Nyx::Reflection::Generated
{
    inline constexpr MetadataEntry NameComponent_Name_MetadataEntries[] =
    {
        { "Category", "Identity" },
        { "Tooltip", "Entity display name" },
    };

    inline constexpr PropertyMetadata NameComponent_Properties[] =
    {
        {
            "Name",
            "Name",
            EPropertyKind::String,
            EPropertyFlags::Edit | EPropertyFlags::Undo | EPropertyFlags::Serialize,
            offsetof(Nyx::Engine::NameComponent, Name),
            NameComponent_Name_MetadataEntries,
            sizeof(NameComponent_Name_MetadataEntries) / sizeof(NameComponent_Name_MetadataEntries[0]),
            nullptr
        },
    };

    inline constexpr TypeMetadata NameComponent_TypeMetadata
    {
        "Nyx::Engine::NameComponent",
        "Name",
        EReflectedTypeRole::Component,
        nullptr,
        0,
        NameComponent_Properties,
        sizeof(NameComponent_Properties) / sizeof(NameComponent_Properties[0])
    };

}

namespace Nyx::Reflection
{
    template<>
    inline const TypeMetadata& GetTypeMetadata<Nyx::Engine::NameComponent>()
    {
        return Generated::NameComponent_TypeMetadata;
    }

}
