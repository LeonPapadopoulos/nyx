#include "SceneOutlinerPanel.h"

#include "EntityDragDrop.h"
#include "ImGuiSource.h"
#include "NameComponent.h"
#include "SceneEditor.h"
#include "UITheme.h"
#include "UIWidgets.h"
#include "WindowPainter.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace
{
	using namespace Nyx::Editor;

	// Folders dragged in the outliner: the payload is the folder's id
	constexpr const char* FolderDragPayload = "NYX_OUTLINER_FOLDER";

	// What is being dragged, if the outliner can take it
	OutlineItems GetDraggedItems()
	{
		OutlineItems items;
		const ImGuiPayload* payload = ImGui::GetDragDropPayload();
		if (!payload || payload->DataSize != sizeof(uint64_t))
		{
			return items;
		}

		uint64_t value = 0;
		std::memcpy(&value, payload->Data, sizeof(value));
		if (payload->IsDataType(EntityDragPayload))
		{
			items.Entities.push_back(Nyx::EntityGuid{ value });
		}
		else if (payload->IsDataType(FolderDragPayload))
		{
			items.Folders.push_back(value);
		}
		return items;
	}

	std::string Trim(const std::string& text)
	{
		const size_t first = text.find_first_not_of(" \t");
		const size_t last = text.find_last_not_of(" \t");
		return first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
	}
}

namespace Nyx::Editor
{
	void SceneOutlinerPanel::Draw(SceneEditor& editing)
	{
		if (!bOpen)
		{
			return;
		}

		if (!Nyx::UI::Begin("Scene Outliner", &bOpen))
		{
			ImGui::End();
			return;
		}

		std::optional<Nyx::Entity>& selection = editing.Scene.GetSelection();
		const Nyx::SceneOutline& outline = editing.Scene.GetOutline();

		// A folder is selected only while no entity is, and while it is there: undo may take it away
		if (selection || !FindOutlineFolder(outline, SelectedFolder))
		{
			SelectedFolder = 0;
		}
		if (!FindOutlineFolder(outline, RenamingFolder))
		{
			RenamingFolder = 0;
		}

		// An entity selected elsewhere, e.g. in a scene view, shows in its folder
		if (selection != ShownSelection)
		{
			ShownSelection = selection;
			if (selection)
			{
				Reveal(editing, GetOutlineFolderOf(editing.Scene, editing.Scene.GetGuid(*selection)));
			}
		}

		// While the outliner has the focus: Ctrl+F reaches the search, F2 renames the selected folder
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
		{
			if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F))
			{
				bFocusSearch = true;
			}
			else if (SelectedFolder != 0 && !ImGui::GetIO().WantTextInput && ImGui::IsKeyChordPressed(ImGuiKey_F2))
			{
				StartRename(editing, SelectedFolder);
			}
		}

		DrawSearch();
		DrawToolbar(editing);
		NYX_UI(Nyx::UI::Separator());

		// The rows scroll under the search and the buttons
		if (Nyx::UI::BeginChild("##Rows"))
		{
			const bool bFiltering = !Filter.empty();
			const std::vector<OutlineNode> tree = BuildOutlineTree(editing.Scene);
			const std::vector<OutlineNode> rows = bFiltering ? FilterOutlineTree(tree, editing.Scene, Filter) : tree;

			DrawnRows.clear();
			Hint.reset();
			// Opened from rows inside folders, whose IDs are inside the folders'
			EntityMenu = ImGui::GetID("##EntityMenu");
			FolderMenu = ImGui::GetID("##FolderMenu");
			DrawRows(editing, rows, bFiltering);
			if (bFiltering && rows.empty())
			{
				NYX_UI(ImGui::TextDisabled("No entity's name has \"%s\"", Filter.c_str()));
			}
			DropBelowRows(editing);

			// Where a drop would go, over the rows
			if (Hint)
			{
				const Nyx::UI::Theme& theme = Nyx::UI::GetTheme();
				Nyx::UI::WindowPainter& painter = Nyx::UI::GetWindowPainter();
				const Nyx::UI::PainterStyle accent = Nyx::UI::PainterStyle::Filled(theme.Accent);
				if (Hint->bBox)
				{
					painter.AddRoundedRect(Nyx::UI::PainterRect{ { Hint->Min.x, Hint->Min.y }, { Hint->Max.x, Hint->Max.y } }, theme.FrameRounding,
						Nyx::UI::PainterStyle::Outlined(theme.Accent, 1.5f));
				}
				else
				{
					// A line with a dot at its start, where the row it goes before or after begins
					const float y = std::floor(Hint->Min.y) + 0.5f;
					painter.AddLine({ Hint->Min.x, y }, { Hint->Max.x, y }, 2.0f, Nyx::UI::EPainterLineCap::Round, accent);
					painter.AddCircle({ Hint->Min.x, y }, 3.0f, accent);
				}
			}

			// A press that became a drag selects nothing
			if ((PressedEntity || PressedFolder != 0) && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
			{
				PressedEntity.reset();
				PressedFolder = 0;
			}
			if ((PressedEntity || PressedFolder != 0) && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
			{
				if (PressedEntity && editing.Scene.GetRegistry().IsAlive(*PressedEntity))
				{
					selection = *PressedEntity;
				}
				else if (PressedFolder != 0)
				{
					SelectFolder(editing, PressedFolder);
				}
				PressedEntity.reset();
				PressedFolder = 0;
			}

			// A click below the rows selects nothing; a right-click there offers to add
			if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered())
			{
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
				{
					selection.reset();
					SelectedFolder = 0;
				}
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
				{
					ImGui::OpenPopup("##OutlinerMenu");
				}
			}

			DrawMenus(editing);
		}
		ImGui::EndChild();

		ImGui::End();
	}

	void SceneOutlinerPanel::DrawSearch()
	{
		const float clearSize = ImGui::GetFrameHeight();
		if (bFocusSearch)
		{
			ImGui::SetKeyboardFocusHere();
			bFocusSearch = false;
		}
		ImGui::SetNextItemWidth(std::max(1.0f, ImGui::GetContentRegionAvail().x - clearSize - ImGui::GetStyle().ItemSpacing.x));
		NYX_UI(Nyx::UI::SearchField("##Search", &Filter, "Search"));
		ImGui::SetItemTooltip("Shows the entities whose names have this in them (Ctrl+F)");

		ImGui::SameLine();
		ImGui::BeginDisabled(Filter.empty());
		if (NYX_UI(Nyx::UI::IconButton("##ClearSearch", Nyx::UI::EIcon::Clear, glm::vec2(clearSize))))
		{
			Filter.clear();
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Clear the search");
	}

	void SceneOutlinerPanel::DrawToolbar(SceneEditor& editing)
	{
		const std::optional<Nyx::Entity>& selection = editing.Scene.GetSelection();

		if (NYX_UI(Nyx::UI::Button("Add Entity")))
		{
			// After the selected entity, or into the selected folder
			if (selection)
			{
				Reveal(editing, GetOutlineFolderOf(editing.Scene, editing.Scene.GetGuid(*selection)));
			}
			Reveal(editing, SelectedFolder);
			editing.CreateEntity(SelectedFolder);
		}

		ImGui::SameLine();
		if (NYX_UI(Nyx::UI::Button("New Folder")))
		{
			NewFolder(editing);
		}
		ImGui::SetItemTooltip("In the selected folder, or beside the selected entity. Folders only sort the outliner; the game never sees them.");

		ImGui::SameLine();
		const bool bHasSelection = selection.has_value();
		ImGui::BeginDisabled(!bHasSelection && SelectedFolder == 0);
		if (NYX_UI(Nyx::UI::Button("Delete Selected")))
		{
			if (bHasSelection)
			{
				editing.DeleteSelectedEntity();
			}
			else if (DeleteOutlineFolder(editing, SelectedFolder))
			{
				SelectedFolder = 0;
			}
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("A folder goes, what is in it stays: it moves up a level.");

		ImGui::SameLine();
		ImGui::BeginDisabled(!bHasSelection);
		if (NYX_UI(Nyx::UI::Button("Duplicate")) && bHasSelection)
		{
			editing.DuplicateSelectedEntity();
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Ctrl+D. Ctrl+C and Ctrl+V copy and paste; right-click an entity for these too.");
	}

	void SceneOutlinerPanel::DrawRows(SceneEditor& editing, const std::vector<OutlineNode>& rows, bool bFiltering)
	{
		for (const OutlineNode& node : rows)
		{
			if (node.IsFolder())
			{
				DrawFolderRow(editing, node, bFiltering);
			}
			else
			{
				DrawEntityRow(editing, node);
			}
		}
	}

	void SceneOutlinerPanel::DrawFolderRow(SceneEditor& editing, const OutlineNode& node, bool bFiltering)
	{
		const Nyx::SceneOutlineFolder* folder = FindOutlineFolder(editing.Scene.GetOutline(), node.Folder);
		if (!folder)
		{
			return;
		}

		// Copied: a drop on the row replaces the outline's folders
		const uint64_t id = node.Folder;
		const std::string name = folder->Name;
		const bool bRenaming = RenamingFolder == id;

		// Named by its id, so it keeps its open state and its drag when renamed; while it is being
		// renamed, the name's field takes the label's place
		const std::string label = (bRenaming ? std::string() : name) + "##Folder" + std::to_string(id);
		Nyx::UI::TreeRowOptions row;
		row.bSelected = SelectedFolder == id;
		row.bLeaf = node.Children.empty();
		row.bDoubleClickToggles = false;
		row.Icon = Nyx::UI::ETreeRowIcon::Folder;

		// Open as the editor remembers it for the scene; while searching, open to show what matches
		const bool bShownOpen = bFiltering || Folders.IsOpen(id);
		if (!row.bLeaf)
		{
			ImGui::SetNextItemOpen(bShownOpen);
		}
		if (bRenaming)
		{
			ImGui::SetNextItemAllowOverlap();
		}
		const float contentX = ImGui::GetCursorScreenPos().x;
		const Nyx::UI::TreeRowResult result = NYX_UI(Nyx::UI::TreeRow(label.c_str(), row));
		const ImVec2 rowMin = ImGui::GetItemRectMin();
		const ImVec2 rowMax = ImGui::GetItemRectMax();
		DrawnRows.push_back(DrawnRow{ .Folder = id, .Min = rowMin, .Max = rowMax, .ContentX = contentX });
		if (!row.bLeaf && !bFiltering && result.bOpen != bShownOpen)
		{
			Folders.SetOpen(id, result.bOpen);
		}

		// A mouse press selects once the button is up without a drag, as for an entity
		if (result.bClicked)
		{
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				PressedFolder = id;
				PressedEntity.reset();
			}
			else
			{
				SelectFolder(editing, id);
			}
		}

		// A double click on the name renames it; on the arrow it opens and closes it
		const bool bOnName = ImGui::GetIO().MousePos.x >= contentX + Nyx::UI::CalcTreeRowLabelOffset(Nyx::UI::ETreeRowIcon::None);
		if (!bRenaming && bOnName && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			StartRename(editing, id);
		}

		if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
		{
			SelectFolder(editing, id);
			ImGui::OpenPopup(FolderMenu);
		}

		if (!bRenaming && ImGui::BeginDragDropSource())
		{
			ImGui::SetDragDropPayload(FolderDragPayload, &id, sizeof(id));
			ImGui::TextUnformatted(name.c_str());
			ImGui::EndDragDropSource();
		}
		DropOnRow(editing, id, {}, result.bOpen, contentX);

		if (bRenaming)
		{
			const float nameX = contentX + Nyx::UI::CalcTreeRowLabelOffset(row.Icon);
			ImGui::SetCursorScreenPos(ImVec2(nameX, rowMin.y));
			ImGui::SetNextItemWidth(std::max(1.0f, rowMax.x - nameX - ImGui::GetStyle().FramePadding.x));
			if (bFocusRename)
			{
				ImGui::SetKeyboardFocusHere();
				bFocusRename = false;
			}
			const bool bEntered = NYX_UI(Nyx::UI::InputText("##Rename", &RenameText, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll));

			// Enter, or a click elsewhere, keeps the name; Esc keeps the old one
			if (bEntered || ImGui::IsItemDeactivated())
			{
				if (!ImGui::IsKeyPressed(ImGuiKey_Escape, false))
				{
					RenameOutlineFolder(editing, id, Trim(RenameText));
				}
				RenamingFolder = 0;
			}
		}

		if (result.bOpen)
		{
			DrawRows(editing, node.Children, bFiltering);
			Nyx::UI::TreeRowEnd();
		}
	}

	void SceneOutlinerPanel::DrawEntityRow(SceneEditor& editing, const OutlineNode& node)
	{
		std::optional<Nyx::Entity>& selection = editing.Scene.GetSelection();
		const Nyx::Entity entity = node.Entity;
		const std::string name = editing.Scene.GetRegistry().Get<Nyx::NameComponent>(entity).Name;

		Nyx::UI::TreeRowOptions row;
		row.bSelected = selection.has_value() && selection.value() == entity;
		row.bLeaf = true;
		row.Icon = Nyx::UI::ETreeRowIcon::Entity;

		const std::string label = name + "##" + std::to_string(entity.Index());

		const float contentX = ImGui::GetCursorScreenPos().x;
		const bool bClicked = NYX_UI(Nyx::UI::TreeRow(label.c_str(), row)).bClicked;
		DrawnRows.push_back(DrawnRow{ .Entity = entity, .Min = ImGui::GetItemRectMin(), .Max = ImGui::GetItemRectMax(), .ContentX = contentX });

		if (bClicked)
		{
			// A mouse press selects once the button is up without a drag, so the entity can be
			// dragged onto a field of the selected entity's details, or elsewhere in the outliner;
			// the keyboard selects at once
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				PressedEntity = entity;
				PressedFolder = 0;
			}
			else
			{
				selection = entity;
			}
		}

		// Right-click: selects the entity and offers what the hotkeys do
		if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
		{
			selection = entity;
			ImGui::OpenPopup(EntityMenu);
		}

		// An entity can be dragged elsewhere in the outliner, and onto a field that names one, e.g. a
		// surface anchor's host
		const EntityGuid guid = editing.Scene.GetGuid(entity);
		if (guid.IsValid() && ImGui::BeginDragDropSource())
		{
			SetEntityDragPayload(guid, name);
			ImGui::EndDragDropSource();
		}
		if (guid.IsValid())
		{
			DropOnRow(editing, 0, guid, false, contentX);
		}
	}

	void SceneOutlinerPanel::DrawMenus(SceneEditor& editing)
	{
		std::optional<Nyx::Entity>& selection = editing.Scene.GetSelection();

		// After the rows, so a new entity doesn't appear while the rows are being drawn
		if (Nyx::UI::BeginPopup("##EntityMenu"))
		{
			if (NYX_UI(Nyx::UI::MenuItem("Duplicate", "Ctrl+D")))
			{
				editing.DuplicateSelectedEntity();
			}
			if (NYX_UI(Nyx::UI::MenuItem("Copy", "Ctrl+C")))
			{
				editing.CopySelectedEntity();
			}
			if (NYX_UI(Nyx::UI::MenuItem("Paste", "Ctrl+V", false, editing.CanPaste())))
			{
				editing.PasteEntity();
			}
			NYX_UI(Nyx::UI::Separator());
			if (NYX_UI(Nyx::UI::MenuItem("New Folder")))
			{
				NewFolder(editing);
			}
			NYX_UI(Nyx::UI::Separator());
			// As saved scenes, logs and NyxDump show it, e.g. 0x5F3A9C2D1E4B7A10
			const EntityGuid guid = selection ? editing.Scene.GetGuid(*selection) : EntityGuid{};
			if (NYX_UI(Nyx::UI::MenuItem("Copy Guid", nullptr, false, guid.IsValid())))
			{
				ImGui::SetClipboardText(GuidToText(guid).c_str());
			}
			ImGui::EndPopup();
		}

		if (Nyx::UI::BeginPopup("##FolderMenu"))
		{
			const uint64_t folder = SelectedFolder;
			if (NYX_UI(Nyx::UI::MenuItem("New Folder", nullptr, false, folder != 0)))
			{
				NewFolder(editing);
			}
			if (NYX_UI(Nyx::UI::MenuItem("Rename", "F2", false, folder != 0)))
			{
				StartRename(editing, folder);
			}
			if (NYX_UI(Nyx::UI::MenuItem("Delete Folder", nullptr, false, folder != 0)) && DeleteOutlineFolder(editing, folder))
			{
				SelectedFolder = 0;
			}
			ImGui::SetItemTooltip("What is in it stays: it moves up a level.");
			ImGui::EndPopup();
		}

		if (Nyx::UI::BeginPopup("##OutlinerMenu"))
		{
			if (NYX_UI(Nyx::UI::MenuItem("Add Entity")))
			{
				editing.CreateEntity();
			}
			if (NYX_UI(Nyx::UI::MenuItem("New Folder")))
			{
				NewFolder(editing);
			}
			ImGui::EndPopup();
		}
	}

	void SceneOutlinerPanel::DropOnRow(SceneEditor& editing, uint64_t folder, Nyx::EntityGuid entity, bool bOpenFolder, float contentX)
	{
		if (!ImGui::BeginDragDropTarget())
		{
			return;
		}

		// An entity's row: before it in its upper half, after it in its lower. A folder's row takes
		// entities into it; folders go before it at its top, after it at its bottom unless it is
		// open (its rows follow), and into it between.
		const OutlineItems items = GetDraggedItems();
		const ImVec2 rowMin = ImGui::GetItemRectMin();
		const ImVec2 rowMax = ImGui::GetItemRectMax();
		const float along = (ImGui::GetIO().MousePos.y - rowMin.y) / std::max(1.0f, rowMax.y - rowMin.y);
		OutlineDrop drop{ .Folder = folder, .Entity = entity };
		if (folder == 0)
		{
			drop.Place = along < 0.5f ? EOutlinePlace::Before : EOutlinePlace::After;
		}
		else if (!items.Folders.empty() && along < 0.25f)
		{
			drop.Place = EOutlinePlace::Before;
		}
		else if (!items.Folders.empty() && along > 0.75f && !bOpenFolder)
		{
			drop.Place = EOutlinePlace::After;
		}
		else
		{
			drop.Place = EOutlinePlace::Into;
		}

		if (AcceptDrop(editing, drop))
		{
			const float y = drop.Place == EOutlinePlace::After ? rowMax.y : rowMin.y;
			Hint = drop.Place == EOutlinePlace::Into ? DropHint{ ImVec2(contentX, rowMin.y), rowMax, true }
				: DropHint{ ImVec2(contentX, y), ImVec2(rowMax.x, y), false };
		}
		ImGui::EndDragDropTarget();
	}

	void SceneOutlinerPanel::DropBelowRows(SceneEditor& editing)
	{
		// From the last row to the bottom, and a row's height even below a long list, so there is
		// always room to scroll to
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		const ImVec2 top = ImGui::GetCursorScreenPos();
		const float rowHeight = Nyx::UI::CalcTreeRowHeight();
		ImGui::Dummy(ImVec2(0.0f, rowHeight));
		const ImRect room(ImVec2(window->WorkRect.Min.x, top.y), ImVec2(window->WorkRect.Max.x, std::max(window->InnerRect.Max.y, top.y + rowHeight)));
		if (ImGui::BeginDragDropTargetCustom(room, ImGui::GetID("##BelowRows")))
		{
			if (AcceptDrop(editing, OutlineDrop{ .Place = EOutlinePlace::End }))
			{
				Hint = DropHint{ ImVec2(room.Min.x, top.y), ImVec2(room.Max.x, top.y), false };
			}
			ImGui::EndDragDropTarget();
		}
	}

	bool SceneOutlinerPanel::AcceptDrop(SceneEditor& editing, const OutlineDrop& drop)
	{
		const OutlineItems items = GetDraggedItems();
		if (!CanDropInOutline(editing.Scene, items, drop))
		{
			return false;
		}

		const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ImGui::GetDragDropPayload()->DataType,
			ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
		if (payload && payload->IsDelivery() && MoveInOutline(editing, items, drop) && drop.Place == EOutlinePlace::Into)
		{
			// What went into a folder shows
			Reveal(editing, drop.Folder);
		}
		return payload != nullptr;
	}

	void SceneOutlinerPanel::SelectFolder(SceneEditor& editing, uint64_t folder)
	{
		editing.Scene.GetSelection().reset();
		ShownSelection.reset();
		SelectedFolder = folder;
	}

	void SceneOutlinerPanel::Reveal(const SceneEditor& editing, uint64_t folder)
	{
		const Nyx::SceneOutline& outline = editing.Scene.GetOutline();
		for (size_t step = 0; folder != 0 && step <= outline.Folders.size(); ++step)
		{
			Folders.SetOpen(folder, true);
			const Nyx::SceneOutlineFolder* found = FindOutlineFolder(outline, folder);
			folder = found ? found->Parent : 0;
		}
	}

	void SceneOutlinerPanel::StartRename(SceneEditor& editing, uint64_t folder)
	{
		const Nyx::SceneOutlineFolder* found = FindOutlineFolder(editing.Scene.GetOutline(), folder);
		if (!found)
		{
			return;
		}

		Reveal(editing, found->Parent);
		RenamingFolder = folder;
		RenameText = found->Name;
		bFocusRename = true;
	}

	void SceneOutlinerPanel::NewFolder(SceneEditor& editing)
	{
		// In the selected folder, or in the selected entity's, beside it
		const std::optional<Nyx::Entity>& selection = editing.Scene.GetSelection();
		const uint64_t parent = SelectedFolder != 0 ? SelectedFolder
			: selection                             ? GetOutlineFolderOf(editing.Scene, editing.Scene.GetGuid(*selection))
													: 0;
		if (const uint64_t folder = CreateOutlineFolder(editing, parent))
		{
			SelectFolder(editing, folder);
			StartRename(editing, folder);
		}
	}
}
