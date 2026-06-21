#include "Generated/Runtime/Runtime.reflect.init.h"
#include "ReflectedComponentAutoRegistration.h"

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

        Nyx::Editor::RegisterReflectedComponentType<Nyx::Engine::NameComponent>("Name");
    }
}
