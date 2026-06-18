#include "ComponentInspectorRegistry.h"

namespace Nyx::Editor
{
	ComponentInspectorRegistry& ComponentInspectorRegistry::Get()
	{
		static ComponentInspectorRegistry Instance;
		return Instance;
	}

	void ComponentInspectorRegistry::Register(ComponentInspectorEntry entry)
	{
		if (!entry.TypeMetadata)
		{
			return;
		}

		for (const ComponentInspectorEntry& existing : Entries)
		{
			if (existing.TypeMetadata == entry.TypeMetadata)
			{
				return;
			}
		}

		Entries.push_back(entry);
	}
}