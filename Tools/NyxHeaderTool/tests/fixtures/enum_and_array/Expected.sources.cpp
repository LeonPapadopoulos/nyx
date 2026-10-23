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
            const auto& type = Reflection::GetTypeMetadata<Nyx::ArrayElement>();
            sources.Register(type, { "@SOURCE_FILE@", 31 });
            sources.Register(type.Properties[0], { "@SOURCE_FILE@", 34 });
        }
        {
            const auto& type = Reflection::GetTypeMetadata<Nyx::EnumAndArrayTest>();
            sources.Register(type, { "@SOURCE_FILE@", 38 });
            sources.Register(type.Properties[0], { "@SOURCE_FILE@", 41 });
            sources.Register(type.Properties[1], { "@SOURCE_FILE@", 44 });
            sources.Register(type.Properties[2], { "@SOURCE_FILE@", 47 });
            sources.Register(type.Properties[3], { "@SOURCE_FILE@", 50 });
            sources.Register(type.Properties[4], { "@SOURCE_FILE@", 53 });
        }
    }
}
