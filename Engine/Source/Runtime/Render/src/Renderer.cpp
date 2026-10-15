#include "NyxPCH.h"
#include "Renderer.h"
#include "VulkanRenderer.h"

namespace Nyx
{
	std::unique_ptr<IRenderer> CreateRenderer()
	{
		return std::make_unique<VulkanRenderer>();
	}
}