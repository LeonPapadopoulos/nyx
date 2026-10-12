#include "SceneFilePopups.h"

#include "AssetDatabase.h"
#include "ImGuiSource.h"
#include "Paths.h"
#include "UIWidgets.h"

#include "imgui.h"

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
		SaveAsName = "UntitledScene";
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
			NYX_UI(Nyx::UI::Separator());

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

				if (NYX_UI(Nyx::UI::Selectable(entry.Name.c_str())))
				{
					request.Load = entry.AbsolutePath;
					ImGui::CloseCurrentPopup();
				}
			}

			ImGui::Spacing();

			if (NYX_UI(Nyx::UI::Button("Cancel", glm::vec2(120.0f, 0.0f))))
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}

		if (Nyx::UI::BeginPopupModal("Save Scene As", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			NYX_UI(ImGui::TextUnformatted("Save scene into Assets/Scenes"));
			NYX_UI(Nyx::UI::Separator());

			ImGui::SetNextItemWidth(320.0f);
			NYX_UI(Nyx::UI::InputText("File Name", &SaveAsName));

			ImGui::Spacing();

			if (NYX_UI(Nyx::UI::Button("Save", glm::vec2(120.0f, 0.0f))))
			{
				std::string fileName = SaveAsName;
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

			if (NYX_UI(Nyx::UI::Button("Cancel",glm::vec2(120.0f, 0.0f))))
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}

		return request;
	}
}
