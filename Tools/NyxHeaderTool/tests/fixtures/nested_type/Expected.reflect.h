#pragma once

#include "ReflectionTypes.h"

namespace Nyx::Reflection
{
    template<>
    const TypeMetadata& GetTypeMetadata<Nyx::Engine::InnerMostReflectedType>();

    template<>
    const TypeMetadata& GetTypeMetadata<Nyx::Engine::NestedReflectionTest>();

    template<>
    const TypeMetadata& GetTypeMetadata<Nyx::Engine::MeshRendererComponent>();

}

namespace Nyx::Reflection::Generated
{
    inline constexpr MetadataEntry InnerMostReflectedType_testProperty_MetadataEntries[] =
    {
        { "Category", "Debug" },
    };

    inline constexpr PropertyMetadata InnerMostReflectedType_Properties[] =
    {
        {
            "testProperty",
            "testProperty",
            EPropertyKind::String,
            EPropertyFlags::Edit | EPropertyFlags::Undo | EPropertyFlags::Serialize,
            offsetof(Nyx::Engine::InnerMostReflectedType, testProperty),
            InnerMostReflectedType_testProperty_MetadataEntries,
            sizeof(InnerMostReflectedType_testProperty_MetadataEntries) / sizeof(InnerMostReflectedType_testProperty_MetadataEntries[0]),
            nullptr
        },
    };

    inline constexpr TypeMetadata InnerMostReflectedType_TypeMetadata
    {
        "Nyx::Engine::InnerMostReflectedType",
        "InnerMostReflectedType",
        EReflectedTypeRole::Plain,
        nullptr,
        0,
        InnerMostReflectedType_Properties,
        sizeof(InnerMostReflectedType_Properties) / sizeof(InnerMostReflectedType_Properties[0])
    };

    inline constexpr MetadataEntry NestedReflectionTest_bBoolProperty_MetadataEntries[] =
    {
        { "Category", "Debug" },
    };

    inline constexpr MetadataEntry NestedReflectionTest_floatProperty_MetadataEntries[] =
    {
        { "Category", "Debug" },
    };

    inline constexpr MetadataEntry NestedReflectionTest_InnerType_MetadataEntries[] =
    {
        { "Category", "Debug" },
    };

    inline const TypeMetadata& NestedReflectionTest_InnerType_NestedTypeResolver()
    {
        return GetTypeMetadata<Nyx::Engine::InnerMostReflectedType>();
    }

    inline constexpr PropertyMetadata NestedReflectionTest_Properties[] =
    {
        {
            "bBoolProperty",
            "bBoolProperty",
            EPropertyKind::Bool,
            EPropertyFlags::Edit | EPropertyFlags::Undo | EPropertyFlags::Serialize,
            offsetof(Nyx::Engine::NestedReflectionTest, bBoolProperty),
            NestedReflectionTest_bBoolProperty_MetadataEntries,
            sizeof(NestedReflectionTest_bBoolProperty_MetadataEntries) / sizeof(NestedReflectionTest_bBoolProperty_MetadataEntries[0]),
            nullptr
        },
        {
            "floatProperty",
            "floatProperty",
            EPropertyKind::Float,
            EPropertyFlags::Edit | EPropertyFlags::Undo | EPropertyFlags::Serialize,
            offsetof(Nyx::Engine::NestedReflectionTest, floatProperty),
            NestedReflectionTest_floatProperty_MetadataEntries,
            sizeof(NestedReflectionTest_floatProperty_MetadataEntries) / sizeof(NestedReflectionTest_floatProperty_MetadataEntries[0]),
            nullptr
        },
        {
            "InnerType",
            "InnerType",
            EPropertyKind::Struct,
            EPropertyFlags::Edit | EPropertyFlags::Undo | EPropertyFlags::Serialize,
            offsetof(Nyx::Engine::NestedReflectionTest, InnerType),
            NestedReflectionTest_InnerType_MetadataEntries,
            sizeof(NestedReflectionTest_InnerType_MetadataEntries) / sizeof(NestedReflectionTest_InnerType_MetadataEntries[0]),
            &NestedReflectionTest_InnerType_NestedTypeResolver
        },
    };

    inline constexpr TypeMetadata NestedReflectionTest_TypeMetadata
    {
        "Nyx::Engine::NestedReflectionTest",
        "NestedReflectionTest",
        EReflectedTypeRole::Plain,
        nullptr,
        0,
        NestedReflectionTest_Properties,
        sizeof(NestedReflectionTest_Properties) / sizeof(NestedReflectionTest_Properties[0])
    };

    inline constexpr MetadataEntry MeshRendererComponent_MeshId_MetadataEntries[] =
    {
        { "Category", "Rendering" },
        { "Tooltip", "Debug Info" },
    };

    inline constexpr MetadataEntry MeshRendererComponent_MaterialId_MetadataEntries[] =
    {
        { "Category", "Rendering" },
        { "Tooltip", "Debug Info" },
    };

    inline constexpr MetadataEntry MeshRendererComponent_bVisible_MetadataEntries[] =
    {
        { "Category", "Rendering" },
        { "Tooltip", "Whether the mesh is rendered" },
    };

    inline constexpr MetadataEntry MeshRendererComponent_Test_MetadataEntries[] =
    {
        { "Category", "Debug" },
    };

    inline const TypeMetadata& MeshRendererComponent_Test_NestedTypeResolver()
    {
        return GetTypeMetadata<Nyx::Engine::NestedReflectionTest>();
    }

    inline constexpr PropertyMetadata MeshRendererComponent_Properties[] =
    {
        {
            "MeshId",
            "MeshId",
            EPropertyKind::String,
            EPropertyFlags::Edit | EPropertyFlags::Undo | EPropertyFlags::Serialize | EPropertyFlags::ReadOnly,
            offsetof(Nyx::Engine::MeshRendererComponent, MeshId),
            MeshRendererComponent_MeshId_MetadataEntries,
            sizeof(MeshRendererComponent_MeshId_MetadataEntries) / sizeof(MeshRendererComponent_MeshId_MetadataEntries[0]),
            nullptr
        },
        {
            "MaterialId",
            "MaterialId",
            EPropertyKind::String,
            EPropertyFlags::Edit | EPropertyFlags::Undo | EPropertyFlags::Serialize | EPropertyFlags::ReadOnly,
            offsetof(Nyx::Engine::MeshRendererComponent, MaterialId),
            MeshRendererComponent_MaterialId_MetadataEntries,
            sizeof(MeshRendererComponent_MaterialId_MetadataEntries) / sizeof(MeshRendererComponent_MaterialId_MetadataEntries[0]),
            nullptr
        },
        {
            "bVisible",
            "bVisible",
            EPropertyKind::Bool,
            EPropertyFlags::Edit | EPropertyFlags::Undo | EPropertyFlags::Serialize,
            offsetof(Nyx::Engine::MeshRendererComponent, bVisible),
            MeshRendererComponent_bVisible_MetadataEntries,
            sizeof(MeshRendererComponent_bVisible_MetadataEntries) / sizeof(MeshRendererComponent_bVisible_MetadataEntries[0]),
            nullptr
        },
        {
            "Test",
            "Test",
            EPropertyKind::Struct,
            EPropertyFlags::Edit | EPropertyFlags::Undo | EPropertyFlags::Serialize,
            offsetof(Nyx::Engine::MeshRendererComponent, Test),
            MeshRendererComponent_Test_MetadataEntries,
            sizeof(MeshRendererComponent_Test_MetadataEntries) / sizeof(MeshRendererComponent_Test_MetadataEntries[0]),
            &MeshRendererComponent_Test_NestedTypeResolver
        },
    };

    inline constexpr TypeMetadata MeshRendererComponent_TypeMetadata
    {
        "Nyx::Engine::MeshRendererComponent",
        "Mesh Renderer Component",
        EReflectedTypeRole::Component,
        nullptr,
        0,
        MeshRendererComponent_Properties,
        sizeof(MeshRendererComponent_Properties) / sizeof(MeshRendererComponent_Properties[0])
    };

}

namespace Nyx::Reflection
{
    template<>
    inline const TypeMetadata& GetTypeMetadata<Nyx::Engine::InnerMostReflectedType>()
    {
        return Generated::InnerMostReflectedType_TypeMetadata;
    }

    template<>
    inline const TypeMetadata& GetTypeMetadata<Nyx::Engine::NestedReflectionTest>()
    {
        return Generated::NestedReflectionTest_TypeMetadata;
    }

    template<>
    inline const TypeMetadata& GetTypeMetadata<Nyx::Engine::MeshRendererComponent>()
    {
        return Generated::MeshRendererComponent_TypeMetadata;
    }

}
