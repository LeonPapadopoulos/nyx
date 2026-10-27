#pragma once

#include "Entity.h"
#include "EntityGuid.h"
#include "OutlinerFolderStates.h"
#include "SceneOutlineEdits.h"

#include <imgui.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Nyx::Editor
{
	class SceneEditor;

	// The "Scene Outliner" window: the scene's entities by name, in folders of the editor's own
	// (SceneOutlineEdits.h), which sort them and do nothing else. At the top a search, which Ctrl+F
	// reaches while the outliner has the focus: it shows only the entities whose names have its
	// text, whatever the case, and the folders that hold them, open. Then Add Entity, New Folder,
	// Delete and Duplicate. Rows are dragged before or after others and into folders; a folder is
	// renamed with a double click or F2. A right-click menu offers Duplicate, Copy, Paste and
	// Copy Guid for an entity, New Folder, Rename and Delete for a folder. An entity dragged from it
	// can also be dropped on a field that names one (EntityDragDrop.h).
	class SceneOutlinerPanel
	{
	public:
		void Draw(SceneEditor& editing);

		bool bOpen = true;

		// Which folders are open, per scene: the editor tells it the scene and the file to keep
		// them in
		OutlinerFolderStates Folders;

		// The search's text
		const std::string& GetFilter() const
		{
			return Filter;
		}

		// The selected folder, while no entity is selected (the scene's selection holds entities); 0
		// for none
		uint64_t GetSelectedFolder() const
		{
			return SelectedFolder;
		}

		// The folder being renamed; 0 for none
		uint64_t GetRenamingFolder() const
		{
			return RenamingFolder;
		}

		// A row as drawn in the last frame: what it shows and where, for tests and tools
		struct DrawnRow
		{
			// A folder's id; 0 for an entity's row
			uint64_t Folder = 0;
			Nyx::Entity Entity{};
			ImVec2 Min;
			ImVec2 Max;
			// Where the row's own content starts, right of its indentation
			float ContentX = 0.0f;
		};
		const std::vector<DrawnRow>& GetDrawnRows() const
		{
			return DrawnRows;
		}

		// Where a drop would go, painted over the rows while something is dragged over them: a
		// line between rows (Min and Max at its ends), or a box round the folder it would go into
		struct DropHint
		{
			ImVec2 Min;
			ImVec2 Max;
			bool bBox = false;
		};
		// As drawn in the last frame; none while nothing that can go there is dragged
		const std::optional<DropHint>& GetDropHint() const
		{
			return Hint;
		}

	private:
		void DrawSearch();
		void DrawToolbar(SceneEditor& editing);
		void DrawRows(SceneEditor& editing, const std::vector<OutlineNode>& rows, bool bFiltering);
		void DrawFolderRow(SceneEditor& editing, const OutlineNode& node, bool bFiltering);
		void DrawEntityRow(SceneEditor& editing, const OutlineNode& node);
		void DrawMenus(SceneEditor& editing);

		// While something is dragged over the row just drawn (a folder, or else an entity): where it
		// would go, shown by a line or a box, and moved there when dropped
		void DropOnRow(SceneEditor& editing, uint64_t folder, Nyx::EntityGuid entity, bool bOpenFolder, float contentX);
		// The room below the rows: the end of the top
		void DropBelowRows(SceneEditor& editing);
		// Takes what is dragged if it can go there; moves it there once it is dropped. False if it
		// can't go there.
		bool AcceptDrop(SceneEditor& editing, const OutlineDrop& drop);

		void SelectFolder(SceneEditor& editing, uint64_t folder);
		// Opens the folder and those it is in
		void Reveal(const SceneEditor& editing, uint64_t folder);
		void StartRename(SceneEditor& editing, uint64_t folder);
		// A new folder where the selection is, selected and being renamed
		void NewFolder(SceneEditor& editing);

		std::string Filter;
		bool bFocusSearch = false;

		// The entity or folder a mouse press is on, selected once the button is up, unless it was dragged
		std::optional<Nyx::Entity> PressedEntity;
		uint64_t PressedFolder = 0;

		uint64_t SelectedFolder = 0;
		// The scene's selection as of the last frame: one made elsewhere, e.g. in a scene view, shows
		// the folders it is in open
		std::optional<Nyx::Entity> ShownSelection;

		// The right-click menus' IDs, in the rows' window
		ImGuiID EntityMenu = 0;
		ImGuiID FolderMenu = 0;

		uint64_t RenamingFolder = 0;
		std::string RenameText;
		bool bFocusRename = false;

		std::optional<DropHint> Hint;

		std::vector<DrawnRow> DrawnRows;
	};
}
