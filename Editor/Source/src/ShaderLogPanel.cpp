#include "ShaderLogPanel.h"

#include "ImGuiConversions.h"
#include "ImGuiSource.h"
#include "ShaderLog.h"
#include "UITheme.h"
#include "UIWidgets.h"

#include <imgui.h>

#include <ctime>
#include <string>

namespace Nyx::Editor
{
	namespace
	{
		using Nyx::UI::ToImVec4;

		// "14:03:27", in local time
		std::string TimeOfDay(std::chrono::system_clock::time_point when)
		{
			const std::time_t time = std::chrono::system_clock::to_time_t(when);
			std::tm local{};
			localtime_s(&local, &time);
			char text[16];
			std::strftime(text, sizeof(text), "%H:%M:%S", &local);
			return text;
		}

		// "1.2 KB", "840 B"
		std::string Size(size_t bytes)
		{
			if (bytes < 1024)
			{
				return std::to_string(bytes) + " B";
			}
			char text[32];
			std::snprintf(text, sizeof(text), "%.1f KB", static_cast<double>(bytes) / 1024.0);
			return text;
		}
	}

	bool DiagnosticLine(const Nyx::Engine::ShaderDiagnostic& diagnostic, const OpenFileAtLine& open)
	{
		const Nyx::UI::Theme& theme = Nyx::UI::GetTheme();
		const glm::vec4 color = diagnostic.Severity == Nyx::Engine::EShaderDiagnosticSeverity::Error ? theme.StatusError
			: diagnostic.Severity == Nyx::Engine::EShaderDiagnosticSeverity::Warning                ? theme.StatusWarning
																									 : theme.TextMuted;
		// A selectable without a label, for the hover pill and the click, and the text over it in its
		// colour: painted selectables draw their label in the theme's text colour
		const std::string text = diagnostic.ToString();
		const ImVec2 at = ImGui::GetCursorScreenPos();
		const bool bClicked = NYX_UI(Nyx::UI::Selectable(("##" + text).c_str(), false, 0, glm::vec2(0.0f, ImGui::GetTextLineHeight())));
		ImGui::GetWindowDrawList()->AddText(at, ImGui::GetColorU32(ToImVec4(color)), text.c_str());
		if (!diagnostic.File.empty())
		{
			ImGui::SetItemTooltip("Opens %s at line %u", diagnostic.File.string().c_str(), diagnostic.Line);
		}
		if (bClicked && open && !diagnostic.File.empty())
		{
			open(diagnostic.File, diagnostic.Line);
		}
		return bClicked;
	}

	void ShaderLogPanel::Draw(const Nyx::Engine::ShaderLog& log)
	{
		if (!bOpen)
		{
			return;
		}
		// The count in the title; the ID after ### keeps the window's place when it changes
		const size_t failures = log.CountFailures();
		const std::string title = failures > 0 ? "Shader Log (" + std::to_string(failures) + " failing)###ShaderLog" : "Shader Log###ShaderLog";
		ImGui::SetNextWindowSize(ImVec2(640.0f, 320.0f), ImGuiCond_FirstUseEver);
		if (!Nyx::UI::Begin(title.c_str(), &bOpen))
		{
			ImGui::End();
			return;
		}

		NYX_UI(Nyx::UI::Checkbox("Failing only", &bFailingOnly));
		NYX_UI(Nyx::UI::Separator());
		if (log.GetEntries().empty())
		{
			NYX_UI(ImGui::TextWrapped("Nothing has compiled since the editor started. Saving a shader compiles it, and so does a material's first use."));
			ImGui::End();
			return;
		}

		const Nyx::UI::Theme& theme = Nyx::UI::GetTheme();
		const auto& entries = log.GetEntries();
		for (auto entry = entries.rbegin(); entry != entries.rend(); ++entry)
		{
			if (bFailingOnly && entry->bSucceeded)
			{
				continue;
			}
			ImGui::PushID(entry->Name.c_str());
			ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(entry->bSucceeded ? theme.StatusOk : theme.StatusError));
			NYX_UI(ImGui::TextUnformatted(entry->bSucceeded ? "Compiled" : "Failed"));
			ImGui::PopStyleColor();
			ImGui::SameLine();
			NYX_UI(ImGui::TextUnformatted(entry->Name.c_str()));
			ImGui::SameLine();
			const std::string summary = entry->bSucceeded
				? TimeOfDay(entry->When) + "   " + std::to_string(static_cast<int>(entry->Milliseconds + 0.5)) + " ms   " + std::to_string(entry->EntryPoints) +
					(entry->EntryPoints == 1 ? " entry point   " : " entry points   ") + Size(entry->SpirvBytes) + " of SPIR-V"
				: TimeOfDay(entry->When) + "   keeps its last good code";
			NYX_UI(ImGui::TextDisabled("%s", summary.c_str()));

			ImGui::Indent();
			for (const Nyx::Engine::ShaderDiagnostic& diagnostic : entry->Diagnostics)
			{
				ImGui::PushID(&diagnostic);
				DiagnosticLine(diagnostic, Open);
				ImGui::PopID();
			}
			ImGui::Unindent();
			ImGui::PopID();
		}
		ImGui::End();
	}
}
