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
            const auto& type = Reflection::GetTypeMetadata<Nyx::TextRange>();
            sources.Register(type, { "@SOURCE_FILE@", 12 });
            sources.Register(type.Properties[0], { "@SOURCE_FILE@", 15 });
            sources.Register(type.Properties[1], { "@SOURCE_FILE@", 18 });
            sources.Register(type.Properties[2], { "@SOURCE_FILE@", 21 });
        }
        {
            const auto& type = Reflection::GetTypeMetadata<Nyx::TextMetadataTest>();
            sources.Register(type, { "@SOURCE_FILE@", 25 });
            sources.Register(type.Properties[0], { "@SOURCE_FILE@", 28 });
        }
    }
}
