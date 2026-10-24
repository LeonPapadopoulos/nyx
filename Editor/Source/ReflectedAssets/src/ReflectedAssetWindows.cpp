#include "ReflectedAssetWindows.h"

#include "ImGuiSource.h"
#include "ReflectedAssetEditors.h"
#include "ReflectedPropertyDrawer.h"
#include "TransactionSystem.h"
#include "UITheme.h"
#include "UIWidgets.h"

#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <vector>

namespace Nyx::Editor
{
	namespace
	{
		void TextInErrorColour(const std::string& text)
		{
			const glm::vec4& error = Nyx::UI::GetTheme().StatusError;
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(error.r, error.g, error.b, error.a));
			NYX_UI(ImGui::TextWrapped("%s", text.c_str()));
			ImGui::PopStyleColor();
		}
	}

	void ReflectedAssetWindows::Draw(ReflectedAssetEditors& editors, TransactionSystem& transactions, const AssetDatabase* assets)
	{
		FocusedPath.clear();
		// The windows of assets closed elsewhere go
		std::erase_if(Windows, [&editors](auto& window)
			{
				return editors.Find(window.first) == nullptr;
			});

		// Closed, or read again, once every window is drawn: either replaces the asset
		std::vector<std::string> closed;
		std::vector<std::string> reloaded;
		for (const std::unique_ptr<ReflectedAssetDocument>& open : editors.GetDocuments())
		{
			ReflectedAssetDocument& document = *open;
			Window& window = Windows[document.GetPath()];
			if (window.Asset != document.GetRef())
			{
				window.DrawContext.CancelPendingEdits();
				window.Asset = document.GetRef();
			}

			const std::string name = std::filesystem::path(document.GetPath()).stem().string();
			const std::string title = document.GetType().DisplayName + ": " + name + "###ReflectedAsset:" + document.GetPath();
			bool bOpen = true;
			ImGui::SetNextWindowSize(ImVec2(380.0f, 460.0f), ImGuiCond_FirstUseEver);
			if (Nyx::UI::Begin(title.c_str(), &bOpen))
			{
				if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
				{
					FocusedPath = document.GetPath();
				}
				NYX_UI(Nyx::UI::SectionHeader((name + (document.IsDirty() ? " *" : "")).c_str()));
				ImGui::SetItemTooltip("%s", document.GetFile().string().c_str());
				NYX_UI(ImGui::TextDisabled("%s", document.GetPath().c_str()));

				if (NYX_UI(Nyx::UI::Button("Save")))
				{
					Save(document, window);
				}
				ImGui::SetItemTooltip("Writes the values to the file (Ctrl+S); running games have them already");
				if (document.IsChangedOnDisk())
				{
					ImGui::SameLine();
					if (NYX_UI(Nyx::UI::Button("Reload")))
					{
						reloaded.push_back(document.GetPath());
					}
					ImGui::SetItemTooltip("Reads the file again, dropping the changes not saved");
					NYX_UI(ImGui::TextWrapped("The file changed on disk. Save writes these values over it; Reload takes the file's."));
				}
				if (!window.Error.empty())
				{
					TextInErrorColour(window.Error);
				}
				const std::vector<Nyx::PropertyProblem>& problems = document.GetProblems();
				if (!problems.empty())
				{
					TextInErrorColour(std::to_string(problems.size()) + (problems.size() == 1 ? " problem" : " problems") + ", under the values below");
				}
				NYX_UI(Nyx::UI::Separator());

				// The values, as the details panel draws a component's, with the editor's undo; each
				// problem under its value
				InspectorDrawContext& context = window.DrawContext;
				context.Transactions = &transactions;
				context.Assets = assets;
				context.CurrentObjectRef = document.GetRef();
				context.CurrentLocation = {};
				context.CurrentPath.clear();
				context.Problems = &problems;
				context.ShownProblems.clear();
				context.bNameNewEntries = true;
				DrawReflectedTypeTable(document.GetData(), *document.GetType().Struct.Metadata, context);
				// A value being dragged reaches the running games every frame, not only when let go
				context.PreviewPendingEdits();

				// Those about no value drawn, e.g. a line naming an entry that isn't there
				for (size_t index = 0; index < problems.size(); ++index)
				{
					if (std::find(context.ShownProblems.begin(), context.ShownProblems.end(), index) == context.ShownProblems.end())
					{
						TextInErrorColour(problems[index].Path + ": " + problems[index].Message);
					}
				}
			}
			ImGui::End();

			if (!bOpen)
			{
				closed.push_back(document.GetPath());
			}
		}

		for (const std::string& path : reloaded)
		{
			Window& window = Windows[path];
			window.Error.clear();
			if (std::string error; !editors.Reload(path, error))
			{
				window.Error = "Couldn't read it again: " + error;
			}
		}
		for (const std::string& path : closed)
		{
			Windows[path].DrawContext.CancelPendingEdits();
			Windows.erase(path);
			editors.Close(path);
		}
	}

	bool ReflectedAssetWindows::HandleHotkeys(ReflectedAssetEditors& editors)
	{
		// Text fields use the key for their text
		if (FocusedPath.empty() || ImGui::GetIO().WantTextInput || !ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
		{
			return false;
		}
		ReflectedAssetDocument* document = editors.Find(FocusedPath);
		if (!document)
		{
			return false;
		}
		Save(*document, Windows[FocusedPath]);
		return true;
	}

	void ReflectedAssetWindows::Save(ReflectedAssetDocument& document, Window& window)
	{
		window.Error.clear();
		if (std::string error; !document.Save(error))
		{
			window.Error = "Couldn't save: " + error;
		}
	}
}
