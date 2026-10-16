#include "PlayControls.h"

#include "EditorPreferences.h"
#include "ImGuiSource.h"
#include "Log.h"
#include "PlaySession.h"
#include "PlayWall.h"
#include "UITheme.h"
#include "UIWidgets.h"
#include "WindowPainter.h"

#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace Nyx::Editor
{
	namespace
	{
		// Saves the preferences, logging if that fails
		void SavePreferences(const EditorPreferences& preferences)
		{
			if (!preferences.Save(EditorPreferences::GetUserFile()))
			{
				LOG_WARNING("Couldn't save the editor preferences to '{0}'", EditorPreferences::GetUserFile().string());
			}
		}

		// The monitor the editor is on, which Play All tiles the games on
		PlayWallScreen GetEditorScreen()
		{
			return GetPlayWallScreen(ImGui::GetMainViewport()->PlatformHandleRaw);
		}
	}

	float PlayControls::GetButtonsWidth(glm::vec2 buttonSize, float spacing)
	{
		return buttonSize.x * 4.0f + spacing * 3.0f;
	}

	void PlayControls::DrawButtons(PlaySession& session, EditorPreferences& preferences, glm::vec2 buttonSize, float spacing)
	{
		// Play / Stop: Play while stopped, and a red Stop while any game runs. While games were
		// asked to quit, its tooltip says so, and a click ends them at once. Play has its own ID,
		// so a click that starts on Stop can't end on Play when the games exit meanwhile.
		const bool bPlaying = session.IsRunning();
		const bool bQuitting = session.IsQuitting();
		const bool bPlayClicked = bPlaying ? NYX_UI(Nyx::UI::IconButton("##Stop", Nyx::UI::EIcon::Stop, buttonSize, Nyx::UI::EButtonTone::Stop))
										   : NYX_UI(Nyx::UI::IconButton("##Play", Nyx::UI::EIcon::Play, buttonSize));
		if (bPlayClicked)
		{
			if (bPlaying)
			{
				session.Stop();
			}
			else
			{
				session.Start(preferences, false, GetEditorScreen());
			}
		}

		const PlaySetup& firstSetup = preferences.PlaySetups.front();
		if (bPlaying && bQuitting)
		{
			ImGui::SetItemTooltip("Stopping: the games were asked to quit. Click to end them at once.\nEditor link:%s", session.GetLinkStatus().c_str());
		}
		else if (bPlaying)
		{
			ImGui::SetItemTooltip("Stop: asks the games to quit, or ends those that aren't linked.\nEditor link:%s", session.GetLinkStatus().c_str());
		}
		else
		{
			ImGui::SetItemTooltip("Play: runs the open scene in NyxGame, unsaved changes included, as %s (%ux%u).\nRight-click for options.",
				firstSetup.Name.c_str(), firstSetup.Width, firstSetup.Height);
		}

		if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
		{
			ImGui::OpenPopup("##PlayOptions");
		}

		// Play All, next to Play. Disabled while games run rather than hidden, so the buttons stay in place.
		ImGui::SameLine(0.0f, spacing);
		const size_t playAllCount = static_cast<size_t>(std::count_if(preferences.PlaySetups.begin(), preferences.PlaySetups.end(),
			[](const PlaySetup& setup)
			{
				return setup.bInPlayAll;
			}));

		ImGui::BeginDisabled(bPlaying);
		if (NYX_UI(Nyx::UI::IconButton("##PlayAll", Nyx::UI::EIcon::PlayAll, buttonSize)))
		{
			session.Start(preferences, true, GetEditorScreen());
		}
		ImGui::EndDisabled();

		if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
		{
			ImGui::SetTooltip("Play All: runs the open scene in %zu games side by side, one per play setup marked for Play All.\n"
							  "Every edit reaches all of them. Right-click for options.",
				playAllCount);
		}

		if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
		{
			ImGui::OpenPopup("##PlayOptions");
		}

		// Pause and Step, disabled until games can pause
		ImGui::BeginDisabled();
		for (const auto& [id, icon, tooltip] : { std::tuple{ "##Pause", Nyx::UI::EIcon::Pause, "Pause: not in Nyx yet" },
				 std::tuple{ "##Step", Nyx::UI::EIcon::Step, "Step one frame: not in Nyx yet" } })
		{
			ImGui::SameLine(0.0f, spacing);
			NYX_UI(Nyx::UI::IconButton(id, icon, buttonSize));
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
			{
				ImGui::SetTooltip("%s", tooltip);
			}
		}
		ImGui::EndDisabled();

		DrawOptionsMenu(session, preferences);
	}

	void PlayControls::DrawOptionsMenu(PlaySession& session, EditorPreferences& preferences)
	{
		if (!Nyx::UI::BeginPopup("##PlayOptions"))
		{
			return;
		}

		if (NYX_UI(Nyx::UI::MenuItem("Play All", nullptr, false, !session.IsRunning())))
		{
			session.Start(preferences, true, GetEditorScreen());
		}

		// Checkboxes rather than menu items, so the menu stays open while choosing several
		bool bChanged = false;
		NYX_UI(Nyx::UI::Separator());
		NYX_UI(ImGui::TextDisabled("In Play All"));
		for (size_t i = 0; i < preferences.PlaySetups.size(); ++i)
		{
			PlaySetup& setup = preferences.PlaySetups[i];
			ImGui::PushID(static_cast<int>(i));
			const std::string label = setup.Name + " (" + std::to_string(setup.Width) + "x" + std::to_string(setup.Height) + ")";
			bChanged |= NYX_UI(Nyx::UI::Checkbox(label.c_str(), &setup.bInPlayAll));
			if (i == 0)
			{
				ImGui::SameLine();
				ImGui::TextDisabled("Play");
			}
			ImGui::PopID();
		}

		if (NYX_UI(Nyx::UI::MenuItem("Play Setups...")))
		{
			bShowSetups = true;
		}

		NYX_UI(Nyx::UI::Separator());
		bChanged |= NYX_UI(Nyx::UI::Checkbox("Console Windows in Play All", &preferences.bConsoleWindowsInPlayAll));
		ImGui::SetItemTooltip("Play always shows the game's console. Their log reaches the Game Link window either way.");
		NYX_UI(Nyx::UI::MenuItem("Game Waits for Debugger", nullptr, &session.bGameWaitsForDebugger));

		if (bChanged)
		{
			SavePreferences(preferences);
		}

		ImGui::EndPopup();
	}

	void PlayControls::DrawSetupsWindow(EditorPreferences& preferences)
	{
		if (!bShowSetups)
		{
			return;
		}

		ImGui::SetNextWindowSize(ImVec2(560.0f, 420.0f), ImGuiCond_FirstUseEver);
		if (!Nyx::UI::Begin("Play Setups", &bShowSetups))
		{
			ImGui::End();
			return;
		}

		ImGui::PushTextWrapPos(0.0f);
		NYX_UI(ImGui::TextUnformatted("Play uses the first setup. Play All starts a game for each setup marked in the first column, "
									  "side by side on the monitor the editor is on. Sizes are the game's picture, without titlebar and borders."));
		ImGui::PopTextWrapPos();

		std::vector<PlaySetup>& setups = preferences.PlaySetups;
		bool bChanged = false;

		// Applied after the table, so the rows don't change while it is drawn
		std::optional<size_t> moveUp;
		std::optional<size_t> remove;

		const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
		if (Nyx::UI::BeginTable("##PlaySetups", 5, flags))
		{
			ImGui::TableSetupColumn("Play All", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Width", ImGuiTableColumnFlags_WidthFixed, 90.0f);
			ImGui::TableSetupColumn("Height", ImGuiTableColumnFlags_WidthFixed, 90.0f);
			ImGui::TableSetupColumn("##Actions", ImGuiTableColumnFlags_WidthFixed);
			Nyx::UI::TableHeadersRow();

			for (size_t i = 0; i < setups.size(); ++i)
			{
				PlaySetup& setup = setups[i];
				ImGui::PushID(static_cast<int>(i));
				Nyx::UI::TableNextRow();

				ImGui::TableNextColumn();
				bChanged |= NYX_UI(Nyx::UI::Checkbox("##InPlayAll", &setup.bInPlayAll));

				// Saved when the field is left, not with every key
				ImGui::TableNextColumn();
				ImGui::SetNextItemWidth(-FLT_MIN);
				NYX_UI(Nyx::UI::InputText("##Name", &setup.Name));
				bChanged |= ImGui::IsItemDeactivatedAfterEdit();

				for (uint32_t* side : { &setup.Width, &setup.Height })
				{
					ImGui::TableNextColumn();
					ImGui::PushID(side == &setup.Width ? "Width" : "Height");
					int value = static_cast<int>(*side);
					ImGui::SetNextItemWidth(-FLT_MIN);
					if (NYX_UI(Nyx::UI::InputScalar("##Side", ImGuiDataType_S32, &value, "%d")))
					{
						*side = static_cast<uint32_t>(std::clamp(value, 1, 16384));
					}
					bChanged |= ImGui::IsItemDeactivatedAfterEdit();
					ImGui::PopID();
				}

				ImGui::TableNextColumn();
				ImGui::BeginDisabled(i == 0);
				if (NYX_UI(Nyx::UI::Button("Up")))
				{
					moveUp = i;
				}
				ImGui::EndDisabled();
				ImGui::SameLine();
				ImGui::BeginDisabled(setups.size() == 1);
				if (NYX_UI(Nyx::UI::Button("Delete")))
				{
					remove = i;
				}
				ImGui::EndDisabled();

				ImGui::PopID();
			}

			Nyx::UI::EndTable();
		}

		if (moveUp)
		{
			std::swap(setups[*moveUp], setups[*moveUp - 1]);
			bChanged = true;
		}

		// Play needs a setup, so the last one stays
		if (remove && setups.size() > 1)
		{
			setups.erase(setups.begin() + static_cast<std::ptrdiff_t>(*remove));
			bChanged = true;
		}

		if (NYX_UI(Nyx::UI::Button("Add Setup")))
		{
			PlaySetup setup = setups.back();
			setup.Name = "New Setup";
			setups.push_back(setup);
			bChanged = true;
		}

		ImGui::SameLine();
		if (NYX_UI(Nyx::UI::Button("Reset to Defaults")))
		{
			setups = GetDefaultPlaySetups();
			bChanged = true;
		}

		if (bChanged)
		{
			SavePreferences(preferences);
		}

		// What Play All would do now: the monitor's work area, and the games' windows on it
		NYX_UI(Nyx::UI::SectionHeader("Play All on this monitor"));
		const PlayWallScreen screen = GetEditorScreen();
		const GamePlan plan = PlanGames(preferences, true, screen);

		if (plan.Games.empty())
		{
			NYX_UI(ImGui::TextDisabled("No setup is marked for Play All."));
		}
		else if (!plan.bFits)
		{
			NYX_UI(ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 1.0f), "%zu games don't fit, even at %d%%.", plan.Games.size(),
				static_cast<int>(MinPlayWallScale * 100.0f)));
		}
		else if (plan.Scale < 1.0f)
		{
			NYX_UI(ImGui::Text("%zu games, shown at %d%% of their sizes to fit.", plan.Games.size(), static_cast<int>(plan.Scale * 100.0f)));
		}
		else
		{
			NYX_UI(ImGui::Text("%zu games, at their full sizes.", plan.Games.size()));
		}

		const ScreenRect& area = screen.WorkArea;
		const ImVec2 available = ImGui::GetContentRegionAvail();
		if (area.Width > 0 && area.Height > 0 && available.x > 0.0f && available.y > 20.0f)
		{
			const float scale = (std::min)(available.x / static_cast<float>(area.Width), available.y / static_cast<float>(area.Height));
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			const auto toPreview = [&](int x, int y)
			{
				return ImVec2(origin.x + static_cast<float>(x - area.X) * scale, origin.y + static_cast<float>(y - area.Y) * scale);
			};

			// The work area as a field, and the windows as buttons on it. Their names are ImGui's
			// text, drawn after the painting, so over it.
			const Nyx::UI::Theme& theme = Nyx::UI::GetTheme();
			Nyx::UI::WindowPainter& painter = Nyx::UI::GetWindowPainter();
			const auto toPainterRect = [](const ImVec2& min, const ImVec2& max)
			{
				return Nyx::UI::PainterRect{ { min.x, min.y }, { max.x, max.y } };
			};
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			painter.AddRoundedRect(toPainterRect(toPreview(area.X, area.Y), toPreview(area.X + area.Width, area.Y + area.Height)), theme.FrameRounding,
				Nyx::UI::PainterStyle::Filled(theme.Field));

			// The visible windows, as they will sit on the screen
			for (const PlannedGame& game : plan.Games)
			{
				const Nyx::GameWindowPosition position = *game.Options.WindowPosition;
				const Nyx::GameWindowSize size = *game.Options.WindowSize;
				const int visibleX = position.X + screen.Frame.InvisibleLeft;
				const int visibleY = position.Y + screen.Frame.InvisibleTop;
				const int visibleWidth = static_cast<int>(size.Width) + screen.Frame.Left + screen.Frame.Right;
				const int visibleHeight = static_cast<int>(size.Height) + screen.Frame.Top + screen.Frame.Bottom;

				const ImVec2 min = toPreview(visibleX, visibleY);
				const ImVec2 max = toPreview(visibleX + visibleWidth, visibleY + visibleHeight);
				const Nyx::UI::PainterRect window = toPainterRect(min, max);
				painter.AddRoundedRect(window, theme.FrameRounding * 0.5f, Nyx::UI::PainterStyle::Filled(theme.Frame));
				painter.AddRoundedRect(window, theme.FrameRounding * 0.5f, Nyx::UI::PainterStyle::Outlined(theme.FrameBorder, theme.FrameBorderWidth));
				drawList->PushClipRect(min, max, true);
				drawList->AddText(ImVec2(min.x + 4.0f, min.y + 2.0f), ImGui::GetColorU32(ImGuiCol_Text), game.Name.c_str());
				drawList->PopClipRect();
			}

			ImGui::Dummy(ImVec2(static_cast<float>(area.Width) * scale, static_cast<float>(area.Height) * scale));
		}

		ImGui::End();
	}
}
