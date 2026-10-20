#include "Generated/Sample/Sample.reflect.init.h"
#include "ComponentTypeRegistry.h"

#include "Input.h"

namespace Nyx::Reflection::Generated
{
    void RegisterSampleReflectedTypes()
    {
        static bool bRegistered = false;
        if (bRegistered)
        {
            return;
        }
        bRegistered = true;

        Nyx::ComponentTypeRegistry::Get().Register<Sample::LanternComponent>();
    }
}
