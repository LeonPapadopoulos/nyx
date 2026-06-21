#pragma once

#include "ReflectionTypes.h"

namespace Nyx::Reflection::Generated
{
    inline constexpr MetadataEntry TransformComponent_Position_MetadataEntries[] =
    {
        { "Category", "Transform" },
        { "DragSpeed", "0.1" },
    };

    inline constexpr MetadataEntry TransformComponent_Rotation_MetadataEntries[] =
    {
        { "Category", "Transform" },
        { "UI", "Degrees" },
    };

    inline constexpr PropertyMetadata TransformComponent_Properties[] =
    {
        {
            "Position",
            "Position",
            EPropertyKind::Vec3,
            EPropertyFlags::Edit | EPropertyFlags::Undo | EPropertyFlags::Serialize,
            offsetof(Nyx::Engine::Gameplay::TransformComponent, Position),
            TransformComponent_Position_MetadataEntries,
            sizeof(TransformComponent_Position_MetadataEntries) / sizeof(TransformComponent_Position_MetadataEntries[0])
        },
        {
            "Rotation",
            "Rotation",
            EPropertyKind::Quat,
            EPropertyFlags::Edit | EPropertyFlags::Undo | EPropertyFlags::Serialize,
            offsetof(Nyx::Engine::Gameplay::TransformComponent, Rotation),
            TransformComponent_Rotation_MetadataEntries,
            sizeof(TransformComponent_Rotation_MetadataEntries) / sizeof(TransformComponent_Rotation_MetadataEntries[0])
        },
    };

    inline constexpr TypeMetadata TransformComponent_TypeMetadata
    {
        "Nyx::Engine::Gameplay::TransformComponent",
        "Transform",
        EReflectedTypeRole::Component,
        nullptr,
        0,
        TransformComponent_Properties,
        sizeof(TransformComponent_Properties) / sizeof(TransformComponent_Properties[0])
    };

}

namespace Nyx::Reflection
{
    template<>
    inline const TypeMetadata& GetTypeMetadata<Nyx::Engine::Gameplay::TransformComponent>()
    {
        return Generated::TransformComponent_TypeMetadata;
    }

}
