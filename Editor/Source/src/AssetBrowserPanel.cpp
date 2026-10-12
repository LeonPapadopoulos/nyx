#include "ImGuiSource.h"
#include "AssetBrowserPanel.h"
#include "UIWidgets.h"

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

		if (!Nyx::UI::Begin("Asset Browser"))
		{
			ImGui::End();
			return;
		}

		if (NYX_UI(Nyx::UI::Button("Rescan")))
		{
			Database->Rescan();
		}

		NYX_UI(Nyx::UI::Separator());

		if (Nyx::UI::BeginChild("AssetBrowserLeft", ImVec2(220.0f, 0.0f), true))
		{
			DrawDirectoryTree({});
		}
		ImGui::EndChild();

		ImGui::SameLine();

		if (Nyx::UI::BeginChild("AssetBrowserRight", ImVec2(0.0f, 0.0f), true))
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

		Nyx::UI::TreeRowOptions row;
		row.bSelected = (CurrentDirectory == relativeDirectory);
		row.Icon = Nyx::UI::ETreeRowIcon::Folder;
		// The root, the only folder at the top, shows its folders from the start.
		row.bDefaultOpen = relativeDirectory.empty();

		std::set<std::filesystem::path> childDirectories;
		for (const AssetEntry& entry : Database->GetChildren(relativeDirectory))
		{
			if (entry.bIsDirectory)
			{
				childDirectories.insert(entry.RelativePath);
			}
		}

		row.bLeaf = childDirectories.empty();

		const Nyx::UI::TreeRowResult result = NYX_UI(Nyx::UI::TreeRow(label.c_str(), row));
		if (result.bClicked)
		{
			CurrentDirectory = relativeDirectory;
		}

		if (result.bOpen)
		{
			for (const auto& childDir : childDirectories)
			{
				DrawDirectoryTree(childDir);
			}

			Nyx::UI::TreeRowEnd();
		}
	}

	void AssetBrowserPanel::DrawDirectoryContents()
	{
		if (!Database)
		{
			return;
		}

		NYX_UI(ImGui::Text("Directory: %s", CurrentDirectory.empty() ? "Assets" : CurrentDirectory.string().c_str()));
		NYX_UI(Nyx::UI::Separator());

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

			if (NYX_UI(Nyx::UI::Selectable(label.c_str(), false)))
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