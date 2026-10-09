// Editor-only declaration locations. Runtime reflection contains no source paths.
#include "ReflectionSourceRegistry.h"
#include "Input.h"

namespace Nyx::Editor
{
    void RegisterRuntimeReflectedSources()
    {
        static bool registered = false;
        if (registered) return;
        registered = true;

        auto& sources = ReflectionSourceRegistry::Get();
        {
            const auto& type = Reflection::GetTypeMetadata<Nyx::Engine::InnerMostReflectedType>();
            sources.Register(type, { "@SOURCE_FILE@", 12 });
            sources.Register(type.Properties[0], { "@SOURCE_FILE@", 15 });
        }
        {
            const auto& type = Reflection::GetTypeMetadata<Nyx::Engine::NestedReflectionTest>();
            sources.Register(type, { "@SOURCE_FILE@", 19 });
            sources.Register(type.Properties[0], { "@SOURCE_FILE@", 22 });
            sources.Register(type.Properties[1], { "@SOURCE_FILE@", 25 });
            sources.Register(type.Properties[2], { "@SOURCE_FILE@", 28 });
        }
        {
            const auto& type = Reflection::GetTypeMetadata<Nyx::Engine::MeshRendererComponent>();
            sources.Register(type, { "@SOURCE_FILE@", 33 });
            sources.Register(type.Properties[0], { "@SOURCE_FILE@", 36 });
            sources.Register(type.Properties[1], { "@SOURCE_FILE@", 39 });
            sources.Register(type.Properties[2], { "@SOURCE_FILE@", 42 });
            sources.Register(type.Properties[3], { "@SOURCE_FILE@", 45 });
        }
    }
}
