#include "Workspaces.h"

#include "Log.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

namespace Nyx::Editor
{
	namespace
	{
		// The type of the editor's sections in the layout files: "[Nyx][<name>]"
		constexpr const char* SectionType = "Nyx";
	}

	const char* GetWorkspaceName(EWorkspace workspace)
	{
		return workspace == EWorkspace::Materials ? "Materials" : "Scene";
	}

	std::optional<EWorkspace> FindWorkspace(std::string_view name)
	{
		for (const EWorkspace workspace : { EWorkspace::Scene, EWorkspace::Materials })
		{
			if (name == GetWorkspaceName(workspace))
			{
				return workspace;
			}
		}
		return std::nullopt;
	}

	std::vector<std::string> WriteWindowPlacement(const Nyx::WindowPlacement& placement)
	{
		const Nyx::ScreenRect& rect = placement.Rect;
		return {
			"Pos=" + std::to_string(rect.X) + "," + std::to_string(rect.Y),
			"Size=" + std::to_string(rect.Width) + "," + std::to_string(rect.Height),
			std::string("Maximized=") + (placement.bMaximized ? "1" : "0"),
		};
	}

	std::optional<Nyx::WindowPlacement> ReadWindowPlacement(const std::vector<std::string>& lines)
	{
		Nyx::WindowPlacement placement;
		bool bHasPos = false;
		bool bHasSize = false;
		for (const std::string& line : lines)
		{
			int x = 0;
			int y = 0;
			int maximized = 0;
			if (std::sscanf(line.c_str(), "Pos=%d,%d", &x, &y) == 2)
			{
				placement.Rect.X = x;
				placement.Rect.Y = y;
				bHasPos = true;
			}
			else if (std::sscanf(line.c_str(), "Size=%d,%d", &x, &y) == 2 && x > 0 && y > 0)
			{
				placement.Rect.Width = x;
				placement.Rect.Height = y;
				bHasSize = true;
			}
			else if (std::sscanf(line.c_str(), "Maximized=%d", &maximized) == 1)
			{
				placement.bMaximized = maximized != 0;
			}
		}
		if (!bHasPos || !bHasSize)
		{
			return std::nullopt;
		}
		return placement;
	}

	std::string DockOnlyIntoLeafNodes(const std::string& layout)
	{
		std::vector<std::string_view> lines;
		for (size_t start = 0; start < layout.size();)
		{
			const size_t end = std::min(layout.find('\n', start), layout.size());
			lines.push_back(std::string_view(layout).substr(start, end + 1 - start));
			start = end + 1;
		}
		const auto hexAfter = [](std::string_view line, std::string_view key) -> std::optional<unsigned int>
		{
			const size_t at = line.find(key);
			if (at == std::string_view::npos)
			{
				return std::nullopt;
			}
			const char* first = line.data() + at + key.size();
			unsigned int id = 0;
			const auto [end, error] = std::from_chars(first, line.data() + line.size(), id, 16);
			return error == std::errc() && end != first ? std::optional<unsigned int>(id) : std::nullopt;
		};

		// The dock tree's leaves: "DockSpace ID=0x..." and "DockNode ID=0x..." without "Split="
		std::set<unsigned int> leaves;
		bool bInDocking = false;
		for (const std::string_view line : lines)
		{
			if (line.starts_with('['))
			{
				bInDocking = line.starts_with("[Docking][Data]");
			}
			else if (const std::optional<unsigned int> id = hexAfter(line, " ID=0x"); bInDocking && id && line.find(" Split=") == std::string_view::npos)
			{
				leaves.insert(*id);
			}
		}

		// The windows' "DockId=0x...,<order>" naming anything else go; such a window is docked
		// nowhere, as ImGui would undock it when drawn
		std::string kept;
		bool bInWindow = false;
		for (const std::string_view line : lines)
		{
			if (line.starts_with('['))
			{
				bInWindow = line.starts_with("[Window][");
			}
			else if (bInWindow && line.starts_with("DockId="))
			{
				const std::optional<unsigned int> id = hexAfter(line, "DockId=0x");
				if (!id || !leaves.contains(*id))
				{
					continue;
				}
			}
			kept += line;
		}
		return kept;
	}

	Workspaces::Workspaces(std::filesystem::path layoutsFolder)
		: LayoutsFolder(std::move(layoutsFolder))
	{
	}

	std::filesystem::path Workspaces::GetLayoutFile(EWorkspace workspace) const
	{
		return LayoutsFolder / (std::string(GetWorkspaceName(workspace)) + ".ini");
	}

	void Workspaces::AddSection(std::string name, std::function<std::vector<std::string>()> write)
	{
		Sections.push_back(Section{ std::move(name), std::move(write) });
	}

	const std::vector<std::string>* Workspaces::GetLoadedSection(std::string_view name) const
	{
		const auto found = LoadedSections.find(name);
		return found != LoadedSections.end() ? &found->second : nullptr;
	}

	void Workspaces::Start(EWorkspace workspace)
	{
		Current = workspace;

		// ImGui would load imgui.ini from the working directory at the first frame and save it
		// there; the layout files do that instead
		ImGui::GetIO().IniFilename = nullptr;
		if (!bStarted)
		{
			ImGuiSettingsHandler handler;
			handler.TypeName = SectionType;
			handler.TypeHash = ImHashStr(SectionType);
			handler.ReadInitFn = &Workspaces::ReadInit;
			handler.ReadOpenFn = &Workspaces::ReadOpen;
			handler.ReadLineFn = &Workspaces::ReadLine;
			handler.WriteAllFn = &Workspaces::WriteAll;
			handler.UserData = this;
			ImGui::AddSettingsHandler(&handler);
			bStarted = true;
		}

		bBuildDefault = !LoadCurrentFile();
	}

	void Workspaces::Stop()
	{
		if (!bStarted)
		{
			return;
		}
		SaveCurrent();
		ImGui::RemoveSettingsHandler(SectionType);
		bStarted = false;
	}

	void Workspaces::SaveCurrent()
	{
		std::error_code error;
		std::filesystem::create_directories(LayoutsFolder, error);
		size_t size = 0;
		const char* settings = ImGui::SaveIniSettingsToMemory(&size);
		ImGui::GetIO().WantSaveIniSettings = false;
		std::ofstream stream(GetLayoutFile(Current), std::ios::binary | std::ios::trunc);
		stream.write(settings, static_cast<std::streamsize>(size));
		if (!stream)
		{
			LOG_WARNING("Couldn't save the {} workspace's layout to {}", GetWorkspaceName(Current), GetLayoutFile(Current).string());
		}
	}

	void Workspaces::SaveIfChanged()
	{
		// ImGui counts down from the first change, so a section changing every frame, e.g. a camera
		// flying, is still saved every few seconds
		if (bStarted && WriteSections() != SavedSections)
		{
			ImGui::MarkIniSettingsDirty();
		}
		if (ImGui::GetIO().WantSaveIniSettings)
		{
			SaveCurrent();
		}
	}

	std::string Workspaces::WriteSections() const
	{
		std::string text;
		for (const Section& section : Sections)
		{
			text += std::string("[") + SectionType + "][" + section.Name + "]\n";
			for (const std::string& line : section.Write())
			{
				text += line + "\n";
			}
			text += "\n";
		}
		return text;
	}

	void Workspaces::ReadInit(ImGuiContext*, ImGuiSettingsHandler* handler)
	{
		static_cast<Workspaces*>(handler->UserData)->LoadedSections.clear();
	}

	void* Workspaces::ReadOpen(ImGuiContext*, ImGuiSettingsHandler* handler, const char* name)
	{
		return &static_cast<Workspaces*>(handler->UserData)->LoadedSections[name];
	}

	void Workspaces::ReadLine(ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* line)
	{
		static_cast<std::vector<std::string>*>(entry)->emplace_back(line);
	}

	void Workspaces::WriteAll(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buffer)
	{
		Workspaces& workspaces = *static_cast<Workspaces*>(handler->UserData);
		workspaces.SavedSections = workspaces.WriteSections();
		buffer->append(workspaces.SavedSections.c_str());
	}

	bool Workspaces::ApplyPending()
	{
		if (!Requested || *Requested == Current)
		{
			Requested.reset();
			return false;
		}
		SaveCurrent();
		Current = *Requested;
		Requested.reset();

		bBuildDefault = !LoadCurrentFile();
		return true;
	}

	bool Workspaces::LoadCurrentFile()
	{
		std::ifstream stream(GetLayoutFile(Current), std::ios::binary);
		if (!stream)
		{
			return false;
		}
		std::stringstream settings;
		settings << stream.rdbuf();
		const std::string text = DockOnlyIntoLeafNodes(settings.str());
		ImGui::LoadIniSettingsFromMemory(text.c_str(), text.size());
		return true;
	}

	void Workspaces::BuildDefaultLayout(unsigned int dockSpace, const std::vector<std::string>& sceneViews)
	{
		if (!bBuildDefault)
		{
			return;
		}
		bBuildDefault = false;

		const ImVec2 size = ImGui::GetMainViewport()->WorkSize;
		ImGui::DockBuilderRemoveNodeChildNodes(dockSpace);
		ImGui::DockBuilderSetNodeSize(dockSpace, size);
		ImGuiID middle = dockSpace;
		if (Current == EWorkspace::Materials)
		{
			// The preview large on the left, the parameters on the right, the Shader Log and the
			// asset browser under the preview
			const ImGuiID right = ImGui::DockBuilderSplitNode(middle, ImGuiDir_Right, 0.34f, nullptr, &middle);
			const ImGuiID bottom = ImGui::DockBuilderSplitNode(middle, ImGuiDir_Down, 0.32f, nullptr, &middle);
			ImGui::DockBuilderDockWindow("Preview", middle);
			ImGui::DockBuilderDockWindow("Parameters", right);
			ImGui::DockBuilderDockWindow("###ShaderLog", bottom);
			ImGui::DockBuilderDockWindow("Asset Browser", bottom);
		}
		else
		{
			// The outliner on the left, the details on the right, the asset browser and the game
			// link at the bottom, the scene views in the middle
			const ImGuiID left = ImGui::DockBuilderSplitNode(middle, ImGuiDir_Left, 0.18f, nullptr, &middle);
			const ImGuiID right = ImGui::DockBuilderSplitNode(middle, ImGuiDir_Right, 0.26f, nullptr, &middle);
			const ImGuiID bottom = ImGui::DockBuilderSplitNode(middle, ImGuiDir_Down, 0.28f, nullptr, &middle);
			ImGui::DockBuilderDockWindow("Scene Outliner", left);
			ImGui::DockBuilderDockWindow("Details", right);
			ImGui::DockBuilderDockWindow("Asset Browser", bottom);
			ImGui::DockBuilderDockWindow("Game Link", bottom);
			for (const std::string& sceneView : sceneViews)
			{
				ImGui::DockBuilderDockWindow(sceneView.c_str(), middle);
			}
		}
		ImGui::DockBuilderFinish(dockSpace);
	}
}
