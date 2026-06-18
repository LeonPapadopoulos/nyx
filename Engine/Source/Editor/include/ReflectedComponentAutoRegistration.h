#pragma once

#include "ComponentInspectorRegistry.h"
#include "ComponentTypeRegistry.h"

namespace Nyx::Editor
{
	template<typename TComponent>
	void RegisterReflectedComponentType(const char* displayName)
	{
		ComponentTypeRegistry::Get().Register(
			MakeComponentTypeOps<TComponent>()
		);

		ComponentInspectorRegistry::Get().Register(
			MakeReflectedComponentInspectorEntry<TComponent>(displayName)
		);
	}
}