#include "PreferencesWindow.h"

#include "ImGuiSource.h"
#include "Log.h"
#include "SourceNavigation.h"
#include "UISourceInspector.h"
#include "UIWidgets.h"

#include "imgui.h"

#include <cfloat>
#include <chrono>
#include <iterator>
#include <string>
#include <vector>

namespace Nyx::Editor
{
	void PreferencesWindow::OpenSource(SourceLocation location, ESourceEditor editor)
	{
		if (PendingSourceOpen.valid())
		{
			SourceNavigationStatus = "A source editor is already opening. Please wait.";
			return;
		}
		SourceNavigationStatus = "Opening source editor...";
		PendingSourceOpen = std::async(std::launch::async, OpenSourceLocation, location, editor);
	}

	void PreferencesWindow::Draw(EditorPreferences& preferences, const UISourceInspector& sourceInspector)
	{
		if (PendingSourceOpen.valid() && PendingSourceOpen.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
		{
			SourceNavigationStatus = PendingSourceOpen.get();
			if (!SourceNavigationStatus.empty())
			{
				LOG_ERROR("{0}", SourceNavigationStatus);
				bOpen = true;
			}
		}
		if (!bOpen) return;

		// Keep preferences independently resizable, including when an older layout docked it.
		ImGui::SetNextWindowSize(ImVec2(640.0f, 400.0f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSizeConstraints(ImVec2(360.0f, 180.0f), ImVec2(FLT_MAX, FLT_MAX));
		if (Nyx::UI::Begin("Preferences", &bOpen))
		{
			NYX_UI(ImGui::TextUnformatted("Source navigation"));
			int selectedEditor = static_cast<int>(preferences.SourceEditor);
			constexpr const char* SourceEditors[] = { "Visual Studio", "Visual Studio Code" };
			if (NYX_UI(Nyx::UI::Combo("Source editor", &selectedEditor, SourceEditors)))
			{
				preferences.SourceEditor = static_cast<ESourceEditor>(selectedEditor);
				if (!preferences.Save(EditorPreferences::GetUserFile()))
					SourceNavigationStatus = "Could not save user preferences.";
			}
			ImGui::PushTextWrapPos(0.0f);
			NYX_UI(ImGui::TextUnformatted("F8: inspect UI. Click: declaration. Shift-click: widget code. Escape: cancel."));
			NYX_UI(ImGui::TextUnformatted("Open menus before inspecting them. Finish active edits before pressing F8."));
			NYX_UI(ImGui::TextUnformatted("Source locations match the compiled code; rebuild after changing source lines."));
			NYX_UI(ImGui::TextDisabled("Preferences: %s", EditorPreferences::GetUserFile().string().c_str()));
			ImGui::PopTextWrapPos();
			if (!SourceNavigationStatus.empty()) NYX_UI(ImGui::TextWrapped("%s", SourceNavigationStatus.c_str()));
			if (!sourceInspector.GetStatus().empty()) NYX_UI(ImGui::TextWrapped("%s", sourceInspector.GetStatus().c_str()));

			ImGui::Spacing();
			DrawAmbilight(preferences);
			ImGui::Spacing();
			DrawFrameRate(preferences);
			ImGui::Spacing();
			DrawAntiAliasing(preferences);
		}
		ImGui::End();
	}

	void PreferencesWindow::DrawAmbilight(EditorPreferences& preferences)
	{
		NYX_UI(ImGui::TextUnformatted("Ambilight"));
		NYX_UI(ImGui::TextDisabled("The scene views' pictures glow onto the editor around them."));

		// The toggle saves at once; a slider when it is let go, not on every frame of a drag.
		bool bSave = NYX_UI(Nyx::UI::Checkbox("Ambilight", &preferences.bAmbilight));
		ImGui::BeginDisabled(!preferences.bAmbilight);
		NYX_UI(Nyx::UI::SliderFloat("Strength", &preferences.AmbilightStrength, 0.0f, 1.0f, "%.2f"));
		bSave |= ImGui::IsItemDeactivatedAfterEdit();
		NYX_UI(Nyx::UI::SliderFloat("Reach", &preferences.AmbilightReach, 0.0f, EditorPreferences::MaxAmbilightReach, "%.0f px"));
		bSave |= ImGui::IsItemDeactivatedAfterEdit();
		ImGui::EndDisabled();

		if (bSave && !preferences.Save(EditorPreferences::GetUserFile()))
		{
			SourceNavigationStatus = "Could not save user preferences.";
		}
	}

	void PreferencesWindow::DrawAntiAliasing(EditorPreferences& preferences)
	{
		NYX_UI(ImGui::TextUnformatted("Anti-aliasing"));
		ImGui::PushTextWrapPos(0.0f);
		NYX_UI(ImGui::TextDisabled("Samples a pixel the scene views and the games Play starts draw with: more smooth thin things and edges, and cost GPU time and memory."));
		ImGui::PopTextWrapPos();

		// "Off", then each count offered; the count saved is always one of them (FromText())
		constexpr const char* Names[] = { "Off", "2x MSAA", "4x MSAA", "8x MSAA" };
		static_assert(std::size(Names) == std::size(EditorPreferences::MsaaSampleCounts));
		int selected = 0;
		for (int index = 0; index < static_cast<int>(std::size(EditorPreferences::MsaaSampleCounts)); ++index)
		{
			selected = EditorPreferences::MsaaSampleCounts[index] == preferences.MsaaSamples ? index : selected;
		}
		if (NYX_UI(Nyx::UI::Combo("Anti-aliasing", &selected, Names)))
		{
			preferences.MsaaSamples = EditorPreferences::MsaaSampleCounts[selected];
			if (!preferences.Save(EditorPreferences::GetUserFile()))
			{
				SourceNavigationStatus = "Could not save user preferences.";
			}
		}
	}

	void PreferencesWindow::DrawFrameRate(EditorPreferences& preferences)
	{
		NYX_UI(ImGui::TextUnformatted("Frame rate"));
		ImGui::PushTextWrapPos(0.0f);
		NYX_UI(ImGui::TextDisabled("Vsync draws no more frames than the monitor shows. A cap holds the editor to fewer, with or without vsync."));
		ImGui::PopTextWrapPos();

		bool bSave = NYX_UI(Nyx::UI::Checkbox("Vsync", &preferences.bVSync));

		// "None", then each cap offered; the cap saved is always one of them (FromText())
		std::vector<std::string> names{ "None" };
		int selected = 0;
		for (const uint32_t cap : EditorPreferences::FrameRateCaps)
		{
			if (cap == preferences.FrameRateCap)
			{
				selected = static_cast<int>(names.size());
			}
			names.push_back(std::to_string(cap) + " fps");
		}
		std::vector<const char*> items;
		for (const std::string& name : names)
		{
			items.push_back(name.c_str());
		}
		if (NYX_UI(Nyx::UI::Combo("Frame rate cap", &selected, items)))
		{
			preferences.FrameRateCap = selected == 0 ? 0 : EditorPreferences::FrameRateCaps[selected - 1];
			bSave = true;
		}

		if (bSave && !preferences.Save(EditorPreferences::GetUserFile()))
		{
			SourceNavigationStatus = "Could not save user preferences.";
		}
	}
}
