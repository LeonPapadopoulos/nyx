#include "CrashCardsPanel.h"

#include "ImGuiSource.h"

#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>

namespace Nyx::Editor
{
	void CrashCardsPanel::Add(GameCrash crash, std::string lastLogLines)
	{
		CrashCard& card = Cards.emplace_back();
		card.Id = NextCardId++;
		card.Crash = std::move(crash);
		card.LastLogLines = std::move(lastLogLines);
		bFocus = true;
	}

	void CrashCardsPanel::RemoveCard(uint64_t id)
	{
		std::erase_if(Cards,
			[id](const CrashCard& card)
			{
				return card.Id == id;
			});
	}

	std::optional<GameCrash> CrashCardsPanel::Draw()
	{
		if (Cards.empty())
		{
			return std::nullopt;
		}

		// Comes to the front when a game crashes
		if (bFocus)
		{
			ImGui::SetNextWindowFocus();
			bFocus = false;
		}

		ImGui::SetNextWindowSize(ImVec2(760.0f, 480.0f), ImGuiCond_FirstUseEver);
		if (!Nyx::UI::Begin("Game Crashed"))
		{
			ImGui::End();
			return std::nullopt;
		}

		// Applied after drawing, so the list doesn't change while it is drawn
		std::optional<uint64_t> dismissed;
		std::optional<uint64_t> restarted;

		if (Cards.size() > 1 && NYX_UI(ImGui::Button("Dismiss All")))
		{
			Cards.clear();
			ImGui::End();
			return std::nullopt;
		}

		for (const CrashCard& card : Cards)
		{
			const GameCrash& crash = card.Crash;
			ImGui::PushID(static_cast<int>(card.Id));

			const std::string description =
				crash.Report ? crash.Report->Description : "it sent no crash report (e.g. it crashed before it was linked)";
			char exitCode[16];
			std::snprintf(exitCode, sizeof(exitCode), "0x%08X", crash.ExitCode);

			NYX_UI(ImGui::TextColored(ImVec4(0.95f, 0.40f, 0.40f, 1.0f), "%s crashed", crash.GameName.c_str()));
			ImGui::PushTextWrapPos(0.0f);
			NYX_UI(ImGui::Text("Exit code %s: %s", exitCode, description.c_str()));
			ImGui::PopTextWrapPos();

			if (NYX_UI(ImGui::Button("Restart")))
			{
				restarted = card.Id;
			}
			ImGui::SetItemTooltip("Starts %s again with the same window and settings, running the scene as it is now", crash.GameName.c_str());

			ImGui::SameLine();
			const std::string report = crash.GameName + " crashed, exit code " + exitCode + "\n" +
				(crash.Report ? Nyx::FormatCrashReport(*crash.Report) : description) + "\n\nLast log lines:\n" + card.LastLogLines;
			if (NYX_UI(ImGui::Button("Copy")))
			{
				ImGui::SetClipboardText(report.c_str());
			}
			ImGui::SetItemTooltip("Copies the report, call stack and last log lines as text");

			ImGui::SameLine();
			if (NYX_UI(ImGui::Button("Dismiss")))
			{
				dismissed = card.Id;
			}

			if (crash.Report && !crash.Report->Frames.empty() &&
				NYX_UI(ImGui::TreeNodeEx("Call stack", ImGuiTreeNodeFlags_DefaultOpen)))
			{
				// Read-only, but selectable, so a frame can be copied
				std::string callStack = Nyx::FormatCrashReport(*crash.Report);
				const float height = (std::min)(ImGui::GetTextLineHeightWithSpacing() * static_cast<float>(crash.Report->Frames.size() + 2), 260.0f);
				NYX_UI(ImGui::InputTextMultiline("##CallStack", callStack.data(), callStack.size() + 1, ImVec2(-FLT_MIN, height),
					ImGuiInputTextFlags_ReadOnly));
				ImGui::TreePop();
			}

			if (!card.LastLogLines.empty() && NYX_UI(ImGui::TreeNode("Last log lines")))
			{
				std::string logLines = card.LastLogLines;
				NYX_UI(ImGui::InputTextMultiline("##LogLines", logLines.data(), logLines.size() + 1, ImVec2(-FLT_MIN, 160.0f),
					ImGuiInputTextFlags_ReadOnly));
				ImGui::TreePop();
			}

			NYX_UI(ImGui::Separator());
			ImGui::PopID();
		}

		ImGui::End();

		std::optional<GameCrash> restart;
		if (restarted)
		{
			const auto card = std::find_if(Cards.begin(), Cards.end(),
				[&](const CrashCard& candidate)
				{
					return candidate.Id == *restarted;
				});
			if (card != Cards.end())
			{
				restart = card->Crash;
				RemoveCard(*restarted);
			}
		}

		if (dismissed)
		{
			RemoveCard(*dismissed);
		}

		return restart;
	}
}
