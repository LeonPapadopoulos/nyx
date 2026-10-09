#include "ImGuiDebugTools.h"
#include "ImGuiSource.h"

#include <imgui_internal.h>
#include <Windows.h>

namespace Nyx::Editor
{
	void ImGuiDebugTools::BeforeFrame()
	{
		ImGuiContext* context = ImGui::GetCurrentContext();
		if (!context) return;

		context->IO.ConfigDebugIsDebuggerPresent = ::IsDebuggerPresent() != FALSE;
#ifndef IMGUI_DISABLE_DEBUG_TOOLS
		if (!context->IO.ConfigDebugIsDebuggerPresent)
		{
			ImGui::DebugBreakClearData();
			context->DebugBreakInLocateId = false;
		}
		// Also covers the picker launched from ImGui's own Metrics or Demo window.
		if (!context->IO.ConfigDebugIsDebuggerPresent || UI::IsInspectingSources())
		{
			context->DebugItemPickerActive = false;
			context->DebugItemPickerBreakId = 0;
		}
#endif
	}

	void ImGuiDebugTools::DrawMenu()
	{
		NYX_UI(ImGui::TextDisabled("Dear ImGui"));
#ifndef IMGUI_DISABLE_DEBUG_TOOLS
		NYX_UI(ImGui::MenuItem("Metrics / Debugger", nullptr, &bShowMetrics));
		NYX_UI(ImGui::MenuItem("Debug Log", nullptr, &bShowDebugLog));
		NYX_UI(ImGui::MenuItem("ID Stack Tool", nullptr, &bShowIDStack));

		const bool bCanPick = ImGui::GetIO().ConfigDebugIsDebuggerPresent && !UI::IsInspectingSources();
		if (NYX_UI(ImGui::MenuItem("Item Picker (break in debugger)", nullptr, false, bCanPick)))
		{
			ImGui::DebugStartItemPicker();
		}
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		{
			const char* hint = "Click a widget to pause in its call stack. Escape cancels.";
			if (!ImGui::GetIO().ConfigDebugIsDebuggerPresent)
			{
				hint = "Attach a debugger to NyxEditor to use Item Picker.";
			}
			else if (UI::IsInspectingSources())
			{
				hint = "Finish Nyx source inspection before using Item Picker.";
			}
			ImGui::BeginTooltip();
			ImGui::TextUnformatted(hint);
			ImGui::EndTooltip();
		}
		NYX_UI(ImGui::MenuItem("Highlight ID conflicts", nullptr, &ImGui::GetIO().ConfigDebugHighlightIdConflicts));
#else
		NYX_UI(ImGui::TextDisabled("ImGui debug tools are disabled in this build."));
#endif
		NYX_UI(ImGui::Separator());
		NYX_UI(ImGui::MenuItem("Style Editor", nullptr, &bShowStyleEditor));
		NYX_UI(ImGui::MenuItem("Demo", nullptr, &bShowDemo));
	}

	void ImGuiDebugTools::DrawWindows()
	{
		if (bShowMetrics) ImGui::ShowMetricsWindow(&bShowMetrics);
		if (bShowDebugLog) ImGui::ShowDebugLogWindow(&bShowDebugLog);
		if (bShowIDStack) ImGui::ShowIDStackToolWindow(&bShowIDStack);
		if (bShowDemo) ImGui::ShowDemoWindow(&bShowDemo);
		if (bShowStyleEditor)
		{
			if (UI::Begin("ImGui Style Editor", &bShowStyleEditor))
			{
				ImGui::ShowStyleEditor();
			}
			ImGui::End();
		}
	}
}
