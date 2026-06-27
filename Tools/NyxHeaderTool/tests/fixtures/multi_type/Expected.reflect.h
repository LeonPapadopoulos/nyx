#pragma once

#include "ReflectionTypes.h"

namespace Nyx::Reflection
{
    template<>
    const TypeMetadata& GetTypeMetadata<Nyx::Engine::NameComponent>();

    template<>
    const TypeMetadata& GetTypeMetadata<Nyx::Engine::DummyNameComponent>();

}

namespace Nyx::Reflection::Generated
{
    inline constexpr PropertyMetadata NameComponent_Properties[] =
    {
        {
            "Name",
            "Name",
            EPropertyKind::String,
            EPropertyFlags::Edit | EPropertyFlags::Undo | EPropertyFlags::Serialize,
            offsetof(Nyx::Engine::NameComponent, Name),
            nullptr,
            0,
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

    inline constexpr PropertyMetadata DummyNameComponent_Properties[] =
    {
        {
            "DummyName",
            "Dummy Name",
            EPropertyKind::String,
            EPropertyFlags::Edit | EPropertyFlags::Undo | EPropertyFlags::Serialize,
            offsetof(Nyx::Engine::DummyNameComponent, DummyName),
            nullptr,
            0,
            nullptr
        },
    };

    inline constexpr TypeMetadata DummyNameComponent_TypeMetadata
    {
        "Nyx::Engine::DummyNameComponent",
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
    inline const TypeMetadata& GetTypeMetadata<Nyx::Engine::NameComponent>()
    {
        return Generated::NameComponent_TypeMetadata;
    }

    template<>
    inline const TypeMetadata& GetTypeMetadata<Nyx::Engine::DummyNameComponent>()
    {
        return Generated::DummyNameComponent_TypeMetadata;
    }

}
