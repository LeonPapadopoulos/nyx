#include "Workspaces.h"

#include "Log.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <fstream>
#include <sstream>
#include <string>

namespace Nyx::Editor
{
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

	Workspaces::Workspaces(std::filesystem::path layoutsFolder)
		: LayoutsFolder(std::move(layoutsFolder))
	{
	}

	std::filesystem::path Workspaces::GetLayoutFile(EWorkspace workspace) const
	{
		return LayoutsFolder / (std::string(GetWorkspaceName(workspace)) + ".ini");
	}

	void Workspaces::SaveCurrent() const
	{
		std::error_code error;
		std::filesystem::create_directories(LayoutsFolder, error);
		size_t size = 0;
		const char* settings = ImGui::SaveIniSettingsToMemory(&size);
		std::ofstream stream(GetLayoutFile(Current), std::ios::binary | std::ios::trunc);
		stream.write(settings, static_cast<std::streamsize>(size));
		if (!stream)
		{
			LOG_WARNING("Couldn't save the {} workspace's layout to {}", GetWorkspaceName(Current), GetLayoutFile(Current).string());
		}
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

		std::ifstream stream(GetLayoutFile(Current), std::ios::binary);
		if (!stream)
		{
			bBuildDefault = true;
			return true;
		}
		std::stringstream settings;
		settings << stream.rdbuf();
		const std::string text = settings.str();
		ImGui::LoadIniSettingsFromMemory(text.c_str(), text.size());
		return true;
	}

	void Workspaces::BuildDefaultLayout(unsigned int dockSpace)
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
			ImGui::DockBuilderDockWindow("Scene", middle);
			ImGui::DockBuilderDockWindow("Scene 2", middle);
		}
		ImGui::DockBuilderFinish(dockSpace);
	}
}
