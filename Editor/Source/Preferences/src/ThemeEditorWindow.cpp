#include "ThemeEditorWindow.h"

#include "EditorPreferences.h"
#include "FileDialogs.h"
#include "ImGuiSource.h"
#include "ImGuiTheme.h"
#include "Log.h"
#include "Paths.h"
#include "StringUtil.h"
#include "UIWidgets.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <format>
#include <span>
#include <system_error>

namespace Nyx::Editor
{
	namespace
	{
		using Nyx::UI::EThemeValueKind;
		using Nyx::UI::ThemeValue;

		// How often the file's time is looked at, in seconds
		constexpr double FileCheckInterval = 0.5;

		bool Matches(const ThemeValue& value, const std::string& filter)
		{
			return filter.empty() || ContainsIgnoringCase(value.Name, filter) || ContainsIgnoringCase(value.Group, filter);
		}

		// The format a size is shown in: finer for the values that drag finely
		const char* SizeFormat(float speed)
		{
			return speed < 0.01f ? "%.3f" : "%.2f";
		}

		// A size, dragged within its range; true when it changed
		bool DrawSize(const char* id, float& size, float min, float max, float speed)
		{
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (!NYX_UI(Nyx::UI::DragFloat(id, &size, speed, SizeFormat(speed))))
			{
				return false;
			}
			size = std::clamp(size, min, max);
			return true;
		}

		// A colour: its swatch with the picker, its hex code, and its alpha; true when it changed
		bool DrawColor(glm::vec4& color)
		{
			const ImGuiStyle& style = ImGui::GetStyle();
			const float spacing = style.ItemInnerSpacing.x;
			const float total = ImGui::CalcItemWidth();
			const float swatchWidth = std::floor(ImGui::GetFrameHeight() * 1.6f);
			const float hexWidth = std::floor(ImGui::CalcTextSize("#DDDDDDDD").x + style.FramePadding.x * 2.0f);
			const float alphaWidth = std::max(total - swatchWidth - hexWidth - spacing * 2.0f, ImGui::GetFrameHeight() * 2.0f);
			bool bChanged = false;

			ImGui::SetNextItemWidth(swatchWidth);
			bChanged |= NYX_UI(Nyx::UI::ColorSwatch("##Swatch", &color.r)).bChanged;

			// The hex code, taken while it is typed whenever it is a whole colour. Six digits keep
			// the alpha, so a colour from a mockup can be pasted into a translucent value.
			ImGui::SameLine(0.0f, spacing);
			ImGui::SetNextItemWidth(hexWidth);
			std::string hex = Nyx::UI::FormatThemeColor(color);
			if (NYX_UI(Nyx::UI::InputText("##Hex", &hex)))
			{
				if (const std::optional<glm::vec4> parsed = Nyx::UI::ParseThemeColor(hex))
				{
					const size_t digits = std::count_if(hex.begin(), hex.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; });
					color = glm::vec4(glm::vec3(*parsed), digits == 8 ? parsed->a : color.a);
					bChanged = true;
				}
			}
			ImGui::SetItemTooltip("#RRGGBB keeps the alpha; #RRGGBBAA sets it too");

			ImGui::SameLine(0.0f, spacing);
			ImGui::SetNextItemWidth(alphaWidth);
			if (NYX_UI(Nyx::UI::DragFloat("##Alpha", &color.a, 0.005f, "alpha %.2f")))
			{
				color.a = std::clamp(color.a, 0.0f, 1.0f);
				bChanged = true;
			}
			return bChanged;
		}

		bool IsSameFile(const std::filesystem::path& a, const std::filesystem::path& b)
		{
			std::error_code error;
			const bool bSame = std::filesystem::equivalent(a, b, error);
			return !error ? bSame : a.lexically_normal() == b.lexically_normal();
		}
	}

	ThemeEditorWindow::ThemeEditorWindow(std::filesystem::path themesFolder)
		: ThemesFolder(std::move(themesFolder))
	{
	}

	std::filesystem::path ThemeEditorWindow::GetProjectThemesFolder()
	{
		return Nyx::Paths::GetProjectRoot() / "Editor" / "Themes";
	}

	std::filesystem::path ThemeEditorWindow::GetDefaultThemeFile(const std::filesystem::path& themesFolder)
	{
		return themesFolder / "Default.nyxtheme";
	}

	std::filesystem::path ThemeEditorWindow::ResolveThemeFile(const std::string& preference, const std::filesystem::path& themesFolder)
	{
		if (preference.empty())
		{
			return GetDefaultThemeFile(themesFolder);
		}
		const std::filesystem::path path(preference);
		return path.is_absolute() ? path : themesFolder / path;
	}

	std::string ThemeEditorWindow::GetThemePreference(const std::filesystem::path& file, const std::filesystem::path& themesFolder)
	{
		if (IsSameFile(file, GetDefaultThemeFile(themesFolder)))
		{
			return {};
		}
		if (IsSameFile(file.parent_path(), themesFolder))
		{
			return file.filename().string();
		}
		return file.string();
	}

	std::vector<std::filesystem::path> ThemeEditorWindow::FindThemeFiles(const std::filesystem::path& themesFolder)
	{
		std::vector<std::filesystem::path> files;
		std::error_code error;
		for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(themesFolder, error))
		{
			if (entry.is_regular_file(error) && entry.path().extension() == ".nyxtheme")
			{
				files.push_back(entry.path());
			}
		}
		std::sort(files.begin(), files.end(), [](const std::filesystem::path& a, const std::filesystem::path& b)
			{
				return a.filename().string() < b.filename().string();
			});
		return files;
	}

	void ThemeEditorWindow::LoadAtStartup(const EditorPreferences& preferences)
	{
		const std::filesystem::path defaultFile = GetDefaultThemeFile(ThemesFolder);
		ActiveFile = ResolveThemeFile(preferences.Theme, ThemesFolder);
		std::error_code error;
		if (!std::filesystem::exists(ActiveFile, error) && !IsSameFile(ActiveFile, defaultFile))
		{
			// The preference stays, in case the file comes back (a drive that isn't there today)
			LOG_WARNING("The theme '{0}' isn't there; loading the default theme", ActiveFile.string());
			ActiveFile = defaultFile;
		}
		if (!std::filesystem::exists(ActiveFile, error))
		{
			if (const std::optional<std::string> saveError = Nyx::UI::SaveThemeFile(ActiveFile, Nyx::UI::Theme{}))
			{
				LOG_WARNING("Couldn't write the default theme: {0}", *saveError);
				Saved = Nyx::UI::GetTheme();
				return;
			}
			LOG_INFO("Wrote the default theme to '{0}'", ActiveFile.string());
		}
		ReadFile("at startup");
	}

	void ThemeEditorWindow::ReadFile(const char* why)
	{
		Nyx::UI::Theme theme;
		const std::optional<std::vector<std::string>> problems = Nyx::UI::LoadThemeFile(ActiveFile, theme);
		std::error_code error;
		FileTime = std::filesystem::last_write_time(ActiveFile, error);
		if (!problems)
		{
			Status = std::format("Couldn't read {}", ActiveFile.filename().string());
			LOG_WARNING("Couldn't read the theme file '{0}'", ActiveFile.string());
			return;
		}

		Nyx::UI::GetTheme() = theme;
		Saved = theme;
		Nyx::UI::ApplyThemeToImGui();
		Problems = *problems;
		for (const std::string& problem : Problems)
		{
			LOG_WARNING("Theme file '{0}': {1}", ActiveFile.filename().string(), problem);
		}
		Status = std::format("Read {} {}", ActiveFile.filename().string(), why);
	}

	void ThemeEditorWindow::Load(const std::filesystem::path& file, EditorPreferences& preferences)
	{
		ActiveFile = file;
		ReadFile("");
		Status = std::format("Loaded {}", file.filename().string());
		preferences.Theme = GetThemePreference(file, ThemesFolder);
	}

	void ThemeEditorWindow::ChooseTheme(const std::filesystem::path& file)
	{
		PendingLoad = file;
		bAskToDiscard = !Nyx::UI::AreThemesEqual(Nyx::UI::GetTheme(), Saved);
	}

	void ThemeEditorWindow::Save()
	{
		if (const std::optional<std::string> error = Nyx::UI::SaveThemeFile(ActiveFile, Nyx::UI::GetTheme()))
		{
			Status = *error;
			LOG_ERROR("{0}", *error);
			return;
		}
		// Read back, so the theme is what the file says (colours in 8 bits a channel)
		ReadFile("after saving");
		Status = std::format("Saved {}", ActiveFile.filename().string());
	}

	void ThemeEditorWindow::SaveAs(EditorPreferences& preferences)
	{
		const std::optional<std::filesystem::path> file = ChooseThemeFileToSave(ThemesFolder, ActiveFile.stem().wstring() + L" Copy");
		if (!file)
		{
			return;
		}
		if (const std::optional<std::string> error = Nyx::UI::SaveThemeFile(*file, Nyx::UI::GetTheme()))
		{
			Status = *error;
			LOG_ERROR("{0}", *error);
			return;
		}
		Load(*file, preferences);
		Status = std::format("Saved as {}", file->filename().string());
	}

	void ThemeEditorWindow::ReloadWhenChanged()
	{
		const double now = ImGui::GetTime();
		if (now < NextFileCheck)
		{
			return;
		}
		NextFileCheck = now + FileCheckInterval;

		std::error_code error;
		const std::filesystem::file_time_type time = std::filesystem::last_write_time(ActiveFile, error);
		if (!error && time != FileTime)
		{
			ReadFile("again: it changed on disk");
		}
	}

	void ThemeEditorWindow::DrawThemeChoice(EditorPreferences& preferences)
	{
		ImGui::AlignTextToFramePadding();
		NYX_UI(ImGui::TextUnformatted("Theme"));
		ImGui::SameLine();

		// The themes in the themes folder, and the one loaded if it lies elsewhere
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
		if (Nyx::UI::BeginCombo("##Theme", ActiveFile.stem().string().c_str()))
		{
			std::vector<std::filesystem::path> files = FindThemeFiles(ThemesFolder);
			if (std::none_of(files.begin(), files.end(), [&](const std::filesystem::path& file) { return IsSameFile(file, ActiveFile); }))
			{
				files.insert(files.begin(), ActiveFile);
			}
			for (const std::filesystem::path& file : files)
			{
				const bool bActive = IsSameFile(file, ActiveFile);
				if (NYX_UI(Nyx::UI::Selectable(file.stem().string().c_str(), bActive)) && !bActive)
				{
					ChooseTheme(file);
				}
				ImGui::SetItemTooltip("%s", file.string().c_str());
			}
			Nyx::UI::EndCombo();
		}
		ImGui::SetItemTooltip("%s", ActiveFile.string().c_str());

		ImGui::SameLine();
		if (NYX_UI(Nyx::UI::Button("Open...")))
		{
			if (const std::optional<std::filesystem::path> file = ChooseThemeFileToOpen(ThemesFolder))
			{
				ChooseTheme(*file);
			}
		}
		ImGui::SetItemTooltip("Loads a theme file from anywhere");
		ImGui::SameLine();
		if (NYX_UI(Nyx::UI::Button("Save As...")))
		{
			SaveAs(preferences);
		}
		ImGui::SetItemTooltip("Saves the theme to a new file and loads that; in Editor/Themes, it shows in the list");
	}

	void ThemeEditorWindow::DrawDiscardPopup(EditorPreferences& preferences)
	{
		// A theme chosen without unsaved changes loads at once
		if (!PendingLoad.empty() && !bAskToDiscard && !ImGui::IsPopupOpen("Discard Theme Changes?"))
		{
			Load(PendingLoad, preferences);
			PendingLoad.clear();
		}
		if (bAskToDiscard)
		{
			ImGui::OpenPopup("Discard Theme Changes?");
			bAskToDiscard = false;
		}
		if (!Nyx::UI::BeginPopupModal("Discard Theme Changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			return;
		}
		NYX_UI(ImGui::Text("%s has unsaved changes.", ActiveFile.filename().string().c_str()));
		NYX_UI(ImGui::Text("Load %s and lose them?", PendingLoad.filename().string().c_str()));
		ImGui::Spacing();
		// Enter loads, Escape cancels
		if (NYX_UI(Nyx::UI::Button("Load")) || ImGui::IsKeyPressed(ImGuiKey_Enter, false))
		{
			Load(PendingLoad, preferences);
			PendingLoad.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (NYX_UI(Nyx::UI::Button("Cancel")) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
		{
			PendingLoad.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	bool ThemeEditorWindow::DrawValue(const ThemeValue& value)
	{
		Nyx::UI::Theme& theme = Nyx::UI::GetTheme();
		ImGui::PushID(value.Name);
		bool bChanged = false;
		const std::string name = value.Name;
		switch (value.Kind)
		{
		case EThemeValueKind::Color:
			Nyx::UI::BeginPropertyRow(value.Name);
			bChanged = DrawColor(value.In<glm::vec4>(theme));
			Nyx::UI::EndPropertyRow();
			break;
		case EThemeValueKind::Float:
			Nyx::UI::BeginPropertyRow(value.Name);
			bChanged = DrawSize("##Size", value.In<float>(theme), value.Min, value.Max, value.Speed);
			Nyx::UI::EndPropertyRow();
			break;
		case EThemeValueKind::Shadow:
		{
			UI::PainterShadow& shadow = value.In<UI::PainterShadow>(theme);
			Nyx::UI::BeginPropertyRow(value.Name);
			bChanged |= DrawColor(shadow.Color);
			Nyx::UI::EndPropertyRow();
			Nyx::UI::BeginPropertyRow((name + " offset").c_str());
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (NYX_UI(Nyx::UI::DragVector("##Offset", &shadow.Offset.x, 2, value.Speed, "%.1f")))
			{
				shadow.Offset = glm::clamp(shadow.Offset, glm::vec2(-value.Max), glm::vec2(value.Max));
				bChanged = true;
			}
			Nyx::UI::EndPropertyRow();
			Nyx::UI::BeginPropertyRow((name + " blur").c_str());
			bChanged |= DrawSize("##Blur", shadow.Blur, 0.0f, value.Max, value.Speed);
			Nyx::UI::EndPropertyRow();
			break;
		}
		case EThemeValueKind::Glow:
		{
			UI::PainterGlow& glow = value.In<UI::PainterGlow>(theme);
			Nyx::UI::BeginPropertyRow(value.Name);
			bChanged |= DrawColor(glow.Color);
			Nyx::UI::EndPropertyRow();
			Nyx::UI::BeginPropertyRow((name + " distance").c_str());
			bChanged |= DrawSize("##Distance", glow.Distance, 0.0f, value.Max, value.Speed);
			Nyx::UI::EndPropertyRow();
			Nyx::UI::BeginPropertyRow((name + " strength").c_str());
			bChanged |= DrawSize("##Strength", glow.Strength, 0.0f, 1.0f, 0.005f);
			Nyx::UI::EndPropertyRow();
			break;
		}
		case EThemeValueKind::Glass:
		{
			UI::PainterGlass& glass = value.In<UI::PainterGlass>(theme);
			Nyx::UI::BeginPropertyRow((name + " refraction").c_str());
			bChanged |= DrawSize("##Refraction", glass.Refraction, 0.0f, value.Max, value.Speed);
			Nyx::UI::EndPropertyRow();
			Nyx::UI::BeginPropertyRow((name + " edge width").c_str());
			bChanged |= DrawSize("##EdgeWidth", glass.EdgeWidth, 0.0f, value.Max, value.Speed);
			Nyx::UI::EndPropertyRow();
			Nyx::UI::BeginPropertyRow((name + " rim strength").c_str());
			bChanged |= DrawSize("##RimStrength", glass.RimStrength, 0.0f, 1.0f, 0.005f);
			Nyx::UI::EndPropertyRow();
			break;
		}
		}
		ImGui::PopID();
		return bChanged;
	}

	void ThemeEditorWindow::Draw(EditorPreferences& preferences)
	{
		ReloadWhenChanged();
		if (!bOpen)
		{
			return;
		}

		ImGui::SetNextWindowSize(ImVec2(560.0f, 680.0f), ImGuiCond_FirstUseEver);
		if (!Nyx::UI::Begin("Theme", &bOpen))
		{
			ImGui::End();
			return;
		}

		Nyx::UI::Theme& theme = Nyx::UI::GetTheme();
		const bool bUnsaved = !Nyx::UI::AreThemesEqual(theme, Saved);
		bool bChanged = false;

		DrawThemeChoice(preferences);

		if (NYX_UI(Nyx::UI::Button("Save")))
		{
			Save();
		}
		ImGui::SetItemTooltip("Writes the theme to %s", ActiveFile.string().c_str());
		ImGui::SameLine();
		ImGui::BeginDisabled(!bUnsaved);
		if (NYX_UI(Nyx::UI::Button("Revert")))
		{
			ReadFile("again");
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Back to what the theme file holds");
		ImGui::SameLine();
		if (NYX_UI(Nyx::UI::Button("Reset to Defaults")))
		{
			theme = Nyx::UI::Theme{};
			bChanged = true;
			Status = "Reset to the defaults in UITheme.h; Save keeps them";
		}
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		if (bUnsaved)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(theme.TextInfo.r, theme.TextInfo.g, theme.TextInfo.b, theme.TextInfo.a));
			NYX_UI(ImGui::TextUnformatted("Unsaved changes"));
			ImGui::PopStyleColor();
		}
		else
		{
			NYX_UI(ImGui::TextDisabled("%s", Status.c_str()));
		}

		for (const std::string& problem : Problems)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(theme.StatusError.r, theme.StatusError.g, theme.StatusError.b, theme.StatusError.a));
			NYX_UI(ImGui::TextWrapped("%s", problem.c_str()));
			ImGui::PopStyleColor();
		}

		ImGui::SetNextItemWidth(-FLT_MIN);
		NYX_UI(Nyx::UI::SearchField("##Filter", &Filter, "Find a value or group"));

		if (ImGui::BeginChild("##Values"))
		{
			// The groups in the order of the values. While searching, the matching values show
			// under a heading per group; otherwise each group opens and closes.
			const std::span<const ThemeValue> values = Nyx::UI::GetThemeValues();
			for (size_t first = 0; first < values.size();)
			{
				size_t end = first;
				while (end < values.size() && std::strcmp(values[end].Group, values[first].Group) == 0)
				{
					++end;
				}
				const std::span<const ThemeValue> group = values.subspan(first, end - first);
				first = end;

				if (!Filter.empty())
				{
					if (std::none_of(group.begin(), group.end(), [&](const ThemeValue& value) { return Matches(value, Filter); }))
					{
						continue;
					}
					NYX_UI(Nyx::UI::SectionHeader(group.front().Group));
					for (const ThemeValue& value : group)
					{
						if (Matches(value, Filter))
						{
							bChanged |= DrawValue(value);
						}
					}
				}
				else if (NYX_UI(Nyx::UI::CollapsingHeader(group.front().Group, false)))
				{
					for (const ThemeValue& value : group)
					{
						bChanged |= DrawValue(value);
					}
				}
			}
		}
		ImGui::EndChild();

		DrawDiscardPopup(preferences);

		// ImGui's own colours follow the theme too
		if (bChanged)
		{
			Nyx::UI::ApplyThemeToImGui();
		}

		ImGui::End();
	}
}
