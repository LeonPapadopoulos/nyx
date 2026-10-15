#pragma once

#include "PainterTiles.h"

#include <glm/glm.hpp>

#include <cstdint>

namespace Nyx::UI
{
	// What the painter shader shows besides the painted shapes
	enum class EPainterDebugView : uint32_t
	{
		None = 0,
		// Tints each tile by how many commands it holds: blue for few, through green, to red for
		// 16 or more. The values match DebugView in Painter.frag.
		Heatmap = 1,
	};

	// How a painter's frame is drawn into its target (VulkanPainter::Draw(), or a renderer's
	// painter canvas)
	struct PainterDrawOptions
	{
		// The target is cleared to this before painting; straight (not premultiplied) RGBA
		glm::vec4 ClearColor{ 0.0f };
		EPainterDebugView DebugView = EPainterDebugView::None;
		PainterBudget Budget;
	};
}
