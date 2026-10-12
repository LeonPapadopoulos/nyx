#include "PreferencesWindow.h"

#include "ImGuiSource.h"
#include "Log.h"
#include "SourceNavigation.h"
#include "UISourceInspector.h"

#include "imgui.h"

#include <cfloat>
#include <chrono>

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
			if (NYX_UI(ImGui::Combo("Source editor", &selectedEditor, "Visual Studio\0Visual Studio Code\0")))
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
		}
		ImGui::End();
	}

	void PreferencesWindow::DrawAmbilight(EditorPreferences& preferences)
	{
		NYX_UI(ImGui::TextUnformatted("Ambilight"));
		NYX_UI(ImGui::TextDisabled("The scene views' pictures glow onto the editor around them."));

		// The toggle saves at once; a slider when it is let go, not on every frame of a drag.
		bool bSave = NYX_UI(ImGui::Checkbox("Ambilight", &preferences.bAmbilight));
		ImGui::BeginDisabled(!preferences.bAmbilight);
		NYX_UI(ImGui::SliderFloat("Strength", &preferences.AmbilightStrength, 0.0f, 1.0f, "%.2f"));
		bSave |= ImGui::IsItemDeactivatedAfterEdit();
		NYX_UI(ImGui::SliderFloat("Reach", &preferences.AmbilightReach, 0.0f, EditorPreferences::MaxAmbilightReach, "%.0f px"));
		bSave |= ImGui::IsItemDeactivatedAfterEdit();
		ImGui::EndDisabled();

		if (bSave && !preferences.Save(EditorPreferences::GetUserFile()))
		{
			SourceNavigationStatus = "Could not save user preferences.";
		}
	}
}
