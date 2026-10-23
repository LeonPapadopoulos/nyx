#include "InputPanel.h"

#include "ImGuiSource.h"
#include "InputSystem.h"

#include <imgui.h>

namespace Nyx::Editor
{
	void InputPanel::Record(const Nyx::Input::InputSystem& input)
	{
		History.Record(input);
	}

	void InputPanel::Draw(Nyx::Input::InputSystem& input)
	{
		if (!bOpen)
		{
			return;
		}

		ImGui::SetNextWindowSize(ImVec2(720.0f, 640.0f), ImGuiCond_FirstUseEver);
		if (Nyx::UI::Begin("Input", &bOpen))
		{
			View.Draw(input, History);
		}
		ImGui::End();
	}
}
