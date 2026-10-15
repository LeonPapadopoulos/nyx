#pragma once

#include "PainterCommands.h"

#include <glm/glm.hpp>
#include <imgui.h>

#include <cstdint>

// Between ImGui's types and the painter's and glm's
namespace Nyx::UI
{
	inline ImVec2 ToImVec2(glm::vec2 point)
	{
		return ImVec2(point.x, point.y);
	}

	// `point` moved by `origin`, e.g. from a painter's target into ImGui's screen coordinates
	inline ImVec2 ToImVec2(glm::vec2 point, ImVec2 origin)
	{
		return ImVec2(origin.x + point.x, origin.y + point.y);
	}

	inline ImVec4 ToImVec4(const glm::vec4& color)
	{
		return ImVec4(color.r, color.g, color.b, color.a);
	}

	// The painter stores premultiplied colour; ImGui takes straight colour. Goes through
	// ImGui::GetColorU32(), so the style's alpha applies too.
	inline ImU32 ToImGuiColor(uint32_t premultipliedColor)
	{
		const glm::vec4 color = UnpackPremultipliedColor(premultipliedColor);
		const glm::vec3 straight = color.a > 0.0f ? glm::vec3(color) / color.a : glm::vec3(0.0f);
		return ImGui::GetColorU32(ImVec4(straight.r, straight.g, straight.b, color.a));
	}

	// The rect at `position` of `size`, as ImGui gives windows, dock nodes and viewports
	inline PainterRect RectAt(ImVec2 position, ImVec2 size)
	{
		return PainterRect{ { position.x, position.y }, { position.x + size.x, position.y + size.y } };
	}
}
