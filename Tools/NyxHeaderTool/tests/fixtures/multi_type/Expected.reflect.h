#pragma once

#include "ReflectionTypes.h"

namespace Nyx::Reflection
{
    template<>
    const TypeMetadata& GetTypeMetadata<Nyx::NameComponent>();

    template<>
    const TypeMetadata& GetTypeMetadata<Nyx::DummyNameComponent>();

}

namespace Nyx::Reflection::Generated
{
    inline constexpr PropertyMetadata NameComponent_Properties[] =
    {
        {
            "Name",
            0x0FE07306,
            "Name",
            EPropertyKind::String,
            EPropertyFlags::Edit | EPropertyFlags::Undo | EPropertyFlags::Serialize,
            offsetof(Nyx::NameComponent, Name),
            nullptr,
            0,
            nullptr
        },
    };

    inline constexpr TypeMetadata NameComponent_TypeMetadata
    {
        "Nyx::NameComponent",
        "Name",
        EReflectedTypeRole::Component,
        nullptr,
        0,
        NameComponent_Properties,
        sizeof(NameComponent_Properties) / sizeof(NameComponent_Properties[0])
    };

    inline constexpr PropertyMetadata DummyNameComponent_Properties[] =
    {
        {
            "DummyName",
            0x9D567AC0,
            "Dummy Name",
            EPropertyKind::String,
            EPropertyFlags::Edit | EPropertyFlags::Undo | EPropertyFlags::Serialize,
            offsetof(Nyx::DummyNameComponent, DummyName),
            nullptr,
            0,
            nullptr
        },
    };

    inline constexpr TypeMetadata DummyNameComponent_TypeMetadata
    {
        "Nyx::DummyNameComponent",
        "Dummy Name",
        EReflectedTypeRole::Component,
        nullptr,
        0,
        DummyNameComponent_Properties,
        sizeof(DummyNameComponent_Properties) / sizeof(DummyNameComponent_Properties[0])
    };

}

namespace Nyx::Reflection
{
    template<>
    inline const TypeMetadata& GetTypeMetadata<Nyx::NameComponent>()
    {
        return Generated::NameComponent_TypeMetadata;
    }

    template<>
    inline const TypeMetadata& GetTypeMetadata<Nyx::DummyNameComponent>()
    {
        return Generated::DummyNameComponent_TypeMetadata;
    }

}
