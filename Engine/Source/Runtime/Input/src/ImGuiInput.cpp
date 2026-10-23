#include "ImGuiInput.h"

#include <imgui.h>

namespace Nyx::Input
{
	namespace
	{
		bool bViewHovered = false;
	}

	void MarkAsView()
	{
		bViewHovered = bViewHovered || ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
	}

	ImGuiInputState ReadImGuiState()
	{
		const ImGuiIO& io = ImGui::GetIO();

		ImGuiInputState state;
		state.bTypingText = io.WantTextInput;
		state.bMouseOverUI = io.WantCaptureMouse && !bViewHovered;

		bViewHovered = false;
		return state;
	}
}
