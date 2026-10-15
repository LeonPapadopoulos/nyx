#pragma once

#include "LiveEdits.h"

namespace Nyx
{
	class IRenderer;
}

namespace Nyx::Engine
{
	// Applies a ShaderProgram or MaterialValues message to the renderer. A material whose text
	// doesn't parse is ignored.
	ELiveEditResult ApplyRendererEdit(const Net::Message& message, Nyx::IRenderer& renderer);
}
