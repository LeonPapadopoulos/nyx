#include "ImGuiSource.h"
#include "AssetBrowserPanel.h"
#include "UIWidgets.h"

#include <imgui.h>

#include <algorithm>
#include <set>
#include <string_view>

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

		// A grid of cards, as many side by side as fit
		const float cardWidth = Nyx::UI::CalcAssetCardSize().x;
		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		const int columns = (std::max)(1, static_cast<int>((ImGui::GetContentRegionAvail().x + spacing) / (cardWidth + spacing)));
		int index = 0;

		for (const AssetEntry& entry : children)
		{
			if (index++ % columns != 0)
			{
				ImGui::SameLine();
			}

			const AssetTypeDescriptor* descriptor = entry.bIsDirectory ? nullptr : AssetTypeRegistry::Get().FindByTypeId(entry.TypeId);
			Nyx::UI::AssetCardOptions card;
			card.bSelected = SelectedAsset == entry.RelativePath;
			card.Icon = entry.bIsDirectory ? Nyx::UI::EAssetCardIcon::Folder : Nyx::UI::EAssetCardIcon::File;
			card.TypeLabel = entry.bIsDirectory ? std::string_view() : descriptor ? std::string_view(descriptor->DisplayName) : std::string_view("File");

			// A file's name without its extension, which its type shows; the whole name is the ID.
			const std::string label = (entry.bIsDirectory ? entry.Name : std::filesystem::path(entry.Name).stem().string()) + "##" + entry.Name;
			const Nyx::UI::AssetCardResult result = NYX_UI(Nyx::UI::AssetCard(label.c_str(), card));
			// The name in full, which the card may cut
			ImGui::SetItemTooltip("%s", entry.Name.c_str());
			if (result.bClicked)
			{
				SelectedAsset = entry.RelativePath;
			}
			if (!result.bOpened)
			{
				continue;
			}

			if (entry.bIsDirectory)
			{
				CurrentDirectory = entry.RelativePath;
			}
			else if (ActivationContext && descriptor && descriptor->bOpenOnDoubleClick && descriptor->Activate)
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