#include "NyxPCH.h"
#include "EditorRenderer.h"
#include "VulkanRenderer.h"

namespace Nyx
{
	std::unique_ptr<IEditorRenderer> CreateRenderer()
	{
		return std::make_unique<VulkanRenderer>();
	}
}