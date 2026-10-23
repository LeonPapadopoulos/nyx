#include "InputPanel.h"

#include "ImGuiSource.h"
#include "InputAssetDocument.h"
#include "InputSystem.h"

#include <imgui.h>

#include <array>

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
			if (Document && Transactions)
			{
				const std::array<Nyx::Input::InputDebugView::ExtraTab, 2> tabs = { {
					{ "Tuning",
						[&]()
						{
							Tuning.DrawTuning(*Document, *Transactions, input);
						} },
					{ "Checks",
						[&]()
						{
							Tuning.DrawChecks(*Document, *Transactions, input);
						} },
				} };
				View.Draw(input, History, tabs);
				Tuning.PreviewPendingEdits();
				if (Tuning.TakeShowTuningRequest())
				{
					View.SelectExtraTab("Tuning");
				}
			}
			else
			{
				View.Draw(input, History);
			}
		}
		ImGui::End();
	}
}
