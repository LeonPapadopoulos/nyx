#include "AssetBrowserPanel.h"

#include <imgui.h>
#include <set>

namespace Nyx::Editor
{
	void AssetBrowserPanel::Draw()
	{
		if (!Database)
		{
			return;
		}

		if (!ImGui::Begin("Asset Browser"))
		{
			ImGui::End();
			return;
		}

		if (ImGui::Button("Rescan"))
		{
			Database->Rescan();
		}

		ImGui::Separator();

		if (ImGui::BeginChild("AssetBrowserLeft", ImVec2(220.0f, 0.0f), true))
		{
			DrawDirectoryTree({});
		}
		ImGui::EndChild();

		ImGui::SameLine();

		if (ImGui::BeginChild("AssetBrowserRight", ImVec2(0.0f, 0.0f), true))
		{
			DrawDirectoryContents();
		}
		ImGui::EndChild();

		ImGui::End();
	}

	void AssetBrowserPanel::DrawDirectoryTree(const std::filesystem::path& relativeDirectory)
	{
		if (!Database)
		{
			return;
		}

		const std::string label = relativeDirectory.empty() ? "Assets" : relativeDirectory.filename().string();

		const bool bSelected = (CurrentDirectory == relativeDirectory);
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
		if (bSelected)
		{
			flags |= ImGuiTreeNodeFlags_Selected;
		}

		std::set<std::filesystem::path> childDirectories;
		for (const AssetEntry& entry : Database->GetChildren(relativeDirectory))
		{
			if (entry.bIsDirectory)
			{
				childDirectories.insert(entry.RelativePath);
			}
		}

		if (childDirectories.empty())
		{
			flags |= ImGuiTreeNodeFlags_Leaf;
		}

		const bool bOpen = ImGui::TreeNodeEx(label.c_str(), flags);

		if (ImGui::IsItemClicked())
		{
			CurrentDirectory = relativeDirectory;
		}

		if (bOpen)
		{
			for (const auto& childDir : childDirectories)
			{
				DrawDirectoryTree(childDir);
			}

			ImGui::TreePop();
		}
	}

	void AssetBrowserPanel::DrawDirectoryContents()
	{
		if (!Database)
		{
			return;
		}

		ImGui::Text("Directory: %s", CurrentDirectory.empty() ? "Assets" : CurrentDirectory.string().c_str());
		ImGui::Separator();

		const std::vector<AssetEntry> children = Database->GetChildren(CurrentDirectory);

		for (const AssetEntry& entry : children)
		{
			std::string prefix;

			if (entry.bIsDirectory)
			{
				prefix = "[Dir] ";
			}
			else if (const AssetTypeDescriptor* descriptor = AssetTypeRegistry::Get().FindByTypeId(entry.TypeId))
			{
				prefix = "[" + descriptor->DisplayName + "] ";
			}
			else
			{
				prefix = "[File] ";
			}

			const std::string label = prefix + entry.Name;

			if (ImGui::Selectable(label.c_str(), false))
			{
				if (entry.bIsDirectory)
				{
					CurrentDirectory = entry.RelativePath;
				}
			}

			if (!entry.bIsDirectory &&
				ActivationContext &&
				ImGui::IsItemHovered() &&
				ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				if (const AssetTypeDescriptor* descriptor = AssetTypeRegistry::Get().FindByTypeId(entry.TypeId))
				{
					if (descriptor->bOpenOnDoubleClick && descriptor->Activate)
					{
						const bool bCanActivate =
							!descriptor->CanActivate || descriptor->CanActivate(entry, *ActivationContext);

						if (bCanActivate)
						{
							descriptor->Activate(entry, *ActivationContext);
						}
					}
				}
			}
		}
	}
}