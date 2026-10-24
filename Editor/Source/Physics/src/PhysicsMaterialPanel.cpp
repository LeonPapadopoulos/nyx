#include "PhysicsMaterialPanel.h"

#include "ImGuiSource.h"
#include "PhysicsMaterialDocument.h"
#include "ReflectedPropertyDrawer.h"
#include "TransactionSystem.h"
#include "UIWidgets.h"

#include <imgui.h>

#include <filesystem>

namespace Nyx::Editor
{
	void PhysicsMaterialPanel::Draw(PhysicsMaterialDocument& document, TransactionSystem& transactions)
	{
		bFocused = false;
		if (!document.IsOpen())
		{
			DrawContext.CancelPendingEdits();
			return;
		}

		bool bOpen = true;
		ImGui::SetNextWindowSize(ImVec2(380.0f, 420.0f), ImGuiCond_FirstUseEver);
		if (Nyx::UI::Begin("Physics Material", &bOpen))
		{
			bFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
			const std::string name = std::filesystem::path(document.GetPath()).stem().string();
			NYX_UI(Nyx::UI::SectionHeader((name + (document.IsDirty() ? " *" : "")).c_str()));
			ImGui::SetItemTooltip("%s", document.GetFile().string().c_str());
			NYX_UI(ImGui::TextDisabled("%s", document.GetPath().c_str()));

			if (NYX_UI(Nyx::UI::Button("Save")))
			{
				Save(document);
			}
			ImGui::SetItemTooltip("Writes the values to the file (Ctrl+S); running games have them already");
			if (!SaveError.empty())
			{
				NYX_UI(ImGui::TextWrapped("%s", SaveError.c_str()));
			}
			NYX_UI(Nyx::UI::Separator());

			// The values, as the details panel draws a component's, with the editor's undo
			DrawContext.Transactions = &transactions;
			DrawContext.CurrentObjectRef = document.GetRef();
			DrawContext.CurrentLocation = {};
			DrawReflectedTypeTable(&document.GetData(), Nyx::Reflection::GetTypeMetadata<Nyx::PhysicsMaterialData>(), DrawContext);
			// A value being dragged reaches the running games every frame, not only when let go
			DrawContext.PreviewPendingEdits();
		}
		ImGui::End();

		if (!bOpen)
		{
			DrawContext.CancelPendingEdits();
			document.Close();
		}
	}

	bool PhysicsMaterialPanel::HandleHotkeys(PhysicsMaterialDocument& document)
	{
		// Text fields use the key for their text
		if (!bFocused || !document.IsOpen() || ImGui::GetIO().WantTextInput || !ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
		{
			return false;
		}
		Save(document);
		return true;
	}

	void PhysicsMaterialPanel::Save(PhysicsMaterialDocument& document)
	{
		SaveError.clear();
		std::string error;
		if (!document.Save(error))
		{
			SaveError = "Couldn't save: " + error;
		}
	}
}
