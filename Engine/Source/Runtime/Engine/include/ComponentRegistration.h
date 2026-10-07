#pragma once

namespace Nyx::Engine
{
	// Fills the ComponentTypeRegistry with every component type, so components can be
	// saved, loaded and edited. The Application calls this once at startup.
	void RegisterComponentTypes();
}
