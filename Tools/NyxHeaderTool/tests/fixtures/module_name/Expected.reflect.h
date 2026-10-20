#pragma once

#include "ReflectionTypes.h"

namespace Nyx::Reflection
{
    template<>
    const TypeMetadata& GetTypeMetadata<Sample::LanternComponent>();

}

namespace Nyx::Reflection::Generated
{
    inline constexpr MetadataEntry LanternComponent_Brightness_MetadataEntries[] =
    {
        { "Category", "Light" },
        { "DragSpeed", "0.1" },
    };

    inline constexpr PropertyMetadata LanternComponent_Properties[] =
    {
        {
            "Brightness",
            0xF884324A,
            "Brightness",
            EPropertyKind::Float,
            EPropertyFlags::Edit | EPropertyFlags::Undo | EPropertyFlags::Serialize,
            offsetof(Sample::LanternComponent, Brightness),
            LanternComponent_Brightness_MetadataEntries,
            sizeof(LanternComponent_Brightness_MetadataEntries) / sizeof(LanternComponent_Brightness_MetadataEntries[0]),
            nullptr
        },
    };

    inline constexpr TypeMetadata LanternComponent_TypeMetadata
    {
        "Sample::LanternComponent",
        "Lantern",
        EReflectedTypeRole::Component,
        nullptr,
        0,
        LanternComponent_Properties,
        sizeof(LanternComponent_Properties) / sizeof(LanternComponent_Properties[0])
    };

}

namespace Nyx::Reflection
{
    template<>
    inline const TypeMetadata& GetTypeMetadata<Sample::LanternComponent>()
    {
        return Generated::LanternComponent_TypeMetadata;
    }

}
