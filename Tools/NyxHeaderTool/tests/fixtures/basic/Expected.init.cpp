#include "Generated/Runtime/Runtime.reflect.init.h"
#include "ComponentTypeRegistry.h"

#include "Input.h"

namespace Nyx::Reflection::Generated
{
    void RegisterRuntimeReflectedTypes()
    {
        static bool bRegistered = false;
        if (bRegistered)
        {
            return;
        }
        bRegistered = true;

        Nyx::Engine::ComponentTypeRegistry::Get().Register<Nyx::Engine::NameComponent>();
    }
}
