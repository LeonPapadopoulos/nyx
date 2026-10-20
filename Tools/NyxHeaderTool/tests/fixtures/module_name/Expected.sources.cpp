// Editor-only declaration locations. Runtime reflection contains no source paths.
#include "ReflectionSourceRegistry.h"
#include "Input.h"

namespace Nyx::Editor
{
    void RegisterSampleReflectedSources()
    {
        static bool registered = false;
        if (registered) return;
        registered = true;

        auto& sources = ReflectionSourceRegistry::Get();
        {
            const auto& type = Reflection::GetTypeMetadata<Sample::LanternComponent>();
            sources.Register(type, { "@SOURCE_FILE@", 8 });
            sources.Register(type.Properties[0], { "@SOURCE_FILE@", 11 });
        }
    }
}
