#include "GameModule.h"

namespace Nyx
{
	// A program built without a game module runs the engine's component types only
	const GameModule& GetLinkedGameModule()
	{
		static const GameModule NoModule{};
		return NoModule;
	}
}
