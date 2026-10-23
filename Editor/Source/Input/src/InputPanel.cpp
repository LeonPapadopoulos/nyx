#include "InputPanel.h"

#include "GameInstance.h"
#include "ImGuiSource.h"
#include "InputAssetDocument.h"
#include "InputSystem.h"
#include "PlaySession.h"
#include "UIWidgets.h"

#include <imgui.h>

#include <array>

namespace Nyx::Editor
{
	namespace
	{
		GameInstance* FindGame(PlaySession* games, const std::string& name)
		{
			if (!games || name.empty())
			{
				return nullptr;
			}
			for (GameInstance* game : games->GetGames())
			{
				if (game->GetName() == name)
				{
					return game;
				}
			}
			return nullptr;
		}
	}

	void InputPanel::Record(const Nyx::Input::InputSystem& input)
	{
		History.Record(input);

		// A game that ended can't be shown; the editor's input again
		if (!ShownGame.empty() && !FindGame(Games, ShownGame))
		{
			ShownGame.clear();
		}

		// Only the game shown streams its input, and only while the window is open
		if (Games)
		{
			for (GameInstance* game : Games->GetGames())
			{
				game->SetInputSubscribed(bOpen && game->GetName() == ShownGame);
			}
		}
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
			// Whose input: the editor's, or a running game's
			std::vector<std::string> sources = { "Editor" };
			int selected = 0;
			if (Games)
			{
				for (GameInstance* game : Games->GetGames())
				{
					if (game->GetName() == ShownGame)
					{
						selected = static_cast<int>(sources.size());
					}
					sources.push_back(game->GetName());
				}
			}
			std::vector<const char*> names;
			for (const std::string& source : sources)
			{
				names.push_back(source.c_str());
			}
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
			if (NYX_UI(Nyx::UI::Combo("##InputSource", &selected, names)))
			{
				ShownGame = selected == 0 ? std::string() : sources[static_cast<size_t>(selected)];
			}
			ImGui::SetItemTooltip("Whose input to show: the editor's, or a game's started with Play");

			Nyx::Input::InputSystem* shown = &input;
			Nyx::Input::InputHistory* history = &History;
			if (GameInstance* game = FindGame(Games, ShownGame))
			{
				Nyx::Input::InputReplica* replica = game->GetInputReplica();
				if (!replica || replica->GetFrameCount() == 0)
				{
					ImGui::SameLine();
					ImGui::AlignTextToFramePadding();
					ImGui::TextDisabled("Waiting for %s's input...", ShownGame.c_str());
					ImGui::End();
					return;
				}
				shown = &replica->GetInput();
				history = &replica->GetHistory();
			}
			View.SetCanSendOutput(shown == &input);

			if (Document && Transactions)
			{
				// With what the input shown knows: its stack, the actions its code took
				Document->RefreshChecks(shown);
				const std::array<Nyx::Input::InputDebugView::ExtraTab, 2> tabs = { {
					{ "Tuning",
						[&]()
						{
							Tuning.DrawTuning(*Document, *Transactions, *shown);
						} },
					{ "Checks",
						[&]()
						{
							Tuning.DrawChecks(*Document, *Transactions, *shown);
						} },
				} };
				View.Draw(*shown, *history, tabs);
				Tuning.PreviewPendingEdits();
				if (Tuning.TakeShowTuningRequest())
				{
					View.SelectExtraTab("Tuning");
				}
			}
			else
			{
				View.Draw(*shown, *history);
			}
		}
		ImGui::End();
	}
}
