#include "SceneFilePopups.h"

#include "AssetDatabase.h"
#include "ImGuiSource.h"
#include "Paths.h"

#include "imgui.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace Nyx::Editor
{
	void SceneFilePopups::RequestLoad()
	{
		bOpenLoadPopup = true;
	}

	void SceneFilePopups::RequestSaveAs()
	{
		bOpenSaveAsPopup = true;

		const std::string defaultName = "UntitledScene";
		std::fill(SaveAsBuffer.begin(), SaveAsBuffer.end(), '\0');
		std::memcpy(
			SaveAsBuffer.data(),
			defaultName.c_str(),
			(std::min)(defaultName.size(), SaveAsBuffer.size() - 1));
	}

	SceneFileRequest SceneFilePopups::Draw(const AssetDatabase& assets)
	{
		SceneFileRequest request;

		if (bOpenLoadPopup)
		{
			ImGui::OpenPopup("Load Scene");
			bOpenLoadPopup = false;
		}

		if (bOpenSaveAsPopup)
		{
			ImGui::OpenPopup("Save Scene As");
			bOpenSaveAsPopup = false;
		}

		if (Nyx::UI::BeginPopupModal("Load Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			NYX_UI(ImGui::TextUnformatted("Scenes"));
			NYX_UI(ImGui::Separator());

			const std::vector<AssetEntry> sceneEntries = assets.GetChildren(std::filesystem::path("Scenes"));

			for (const AssetEntry& entry : sceneEntries)
			{
				if (entry.bIsDirectory)
				{
					continue;
				}

				if (entry.TypeId != "Scene")
				{
					continue;
				}

				if (NYX_UI(ImGui::Selectable(entry.Name.c_str())))
				{
					request.Load = entry.AbsolutePath;
					ImGui::CloseCurrentPopup();
				}
			}

			ImGui::Spacing();

			if (NYX_UI(ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))))
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}

		if (Nyx::UI::BeginPopupModal("Save Scene As", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			NYX_UI(ImGui::TextUnformatted("Save scene into Assets/Scenes"));
			NYX_UI(ImGui::Separator());

			ImGui::SetNextItemWidth(320.0f);
			NYX_UI(ImGui::InputText("File Name", SaveAsBuffer.data(), SaveAsBuffer.size()));

			ImGui::Spacing();

			if (NYX_UI(ImGui::Button("Save", ImVec2(120.0f, 0.0f))))
			{
				std::string fileName = SaveAsBuffer.data();
				if (!fileName.empty())
				{
					if (std::filesystem::path(fileName).extension() != ".nyxscene")
					{
						fileName += ".nyxscene";
					}

					request.SaveAs = Nyx::Paths::GetScenesDir() / fileName;
					ImGui::CloseCurrentPopup();
				}
			}

			ImGui::SameLine();

			if (NYX_UI(ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))))
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}

		return request;
	}
}
