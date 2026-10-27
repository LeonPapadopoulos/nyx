// The Scene Outliner: its search, the order of its rows, its folders, their undo and how scenes save
// them. Drawn headless with ImGui, driven by mouse and keys as by hand.
#include "ComponentRegistration.h"
#include "GuidComponent.h"
#include "Log.h"
#include "NameComponent.h"
#include "OutlinerFolderStates.h"
#include "SceneEditor.h"
#include "SceneOutlineEdits.h"
#include "SceneOutlinerPanel.h"
#include "SceneSerializer.h"
#include "UIWidgets.h"
#include "HeadlessUI.h"
#include "TestSupport.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using namespace Nyx;
	using namespace Nyx::Editor;

	using Nyx::Tests::HeadlessUI;
	using Nyx::Tests::Require;
	using Nyx::Tests::TestDirectory;

	std::string GetFolderName(const SceneDocument& scene, uint64_t folder)
	{
		const SceneOutlineFolder* found = FindOutlineFolder(scene.GetOutline(), folder);
		return found ? found->Name : "?";
	}

	// The tree as text: entities by name, folders as [Name: what it holds]
	std::string Describe(const SceneDocument& scene, const std::vector<OutlineNode>& nodes)
	{
		std::string text;
		for (const OutlineNode& node : nodes)
		{
			text += text.empty() ? "" : " ";
			if (node.IsFolder())
			{
				const std::string inside = Describe(scene, node.Children);
				text += "[" + GetFolderName(scene, node.Folder) + (inside.empty() ? "" : ": " + inside) + "]";
			}
			else
			{
				text += scene.GetRegistry().Get<NameComponent>(node.Entity).Name;
			}
		}
		return text;
	}

	std::string Describe(const SceneDocument& scene)
	{
		return Describe(scene, BuildOutlineTree(scene));
	}

	void RequireOutline(const SceneDocument& scene, const std::string& expected, const std::string& message)
	{
		const std::string shown = Describe(scene);
		Require(shown == expected, message + ": the outliner shows \"" + shown + "\", not \"" + expected + "\"");
	}

	EntityGuid GuidOf(const SceneDocument& scene, Entity entity)
	{
		return scene.GetGuid(entity);
	}

	// The outliner drawn headless at the top left, with the mouse and keys to drive it
	class OutlinerFixture
	{
	public:
		void Frame()
		{
			UI.Frame([&]()
				{
					ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
					ImGui::SetNextWindowSize(ImVec2(360.0f, 600.0f));
					Outliner.Draw(Editing);
				});
		}

		void Frames(int count)
		{
			for (int index = 0; index < count; ++index)
			{
				Frame();
			}
		}

		void MoveTo(ImVec2 at)
		{
			ImGui::GetIO().AddMousePosEvent(at.x, at.y);
			Frame();
		}

		void Click(ImVec2 at, ImGuiMouseButton button = ImGuiMouseButton_Left)
		{
			MoveTo(at);
			ImGui::GetIO().AddMouseButtonEvent(button, true);
			Frame();
			ImGui::GetIO().AddMouseButtonEvent(button, false);
			Frame();
		}

		void DragTo(ImVec2 from, ImVec2 to)
		{
			MoveTo(from);
			ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
			Frame();
			for (int step = 1; step <= 4; ++step)
			{
				MoveTo(ImVec2(from.x + (to.x - from.x) * step / 4.0f, from.y + (to.y - from.y) * step / 4.0f));
			}
			ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
			Frames(2);
		}

		void Press(ImGuiKey key, bool bCtrl = false)
		{
			UI.Press(key, bCtrl, false,
				[&]()
				{
					ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
					ImGui::SetNextWindowSize(ImVec2(360.0f, 600.0f));
					Outliner.Draw(Editing);
				});
		}

		void Type(const char* text)
		{
			ImGui::GetIO().AddInputCharactersUTF8(text);
			Frames(2);
		}

		const SceneOutlinerPanel::DrawnRow* FindRow(Entity entity) const
		{
			for (const SceneOutlinerPanel::DrawnRow& row : Outliner.GetDrawnRows())
			{
				if (row.Folder == 0 && row.Entity == entity)
				{
					return &row;
				}
			}
			return nullptr;
		}

		const SceneOutlinerPanel::DrawnRow* FindFolderRow(uint64_t folder) const
		{
			for (const SceneOutlinerPanel::DrawnRow& row : Outliner.GetDrawnRows())
			{
				if (row.Folder == folder)
				{
					return &row;
				}
			}
			return nullptr;
		}

		const SceneOutlinerPanel::DrawnRow& Row(Entity entity) const
		{
			const SceneOutlinerPanel::DrawnRow* row = FindRow(entity);
			Require(row != nullptr, "The outliner should show the entity's row");
			return *row;
		}

		const SceneOutlinerPanel::DrawnRow& FolderRow(uint64_t folder) const
		{
			const SceneOutlinerPanel::DrawnRow* row = FindFolderRow(folder);
			Require(row != nullptr, "The outliner should show the folder's row");
			return *row;
		}

		// A point in the row, `along` of the way down it
		static ImVec2 At(const SceneOutlinerPanel::DrawnRow& row, float along)
		{
			return ImVec2(row.ContentX + 40.0f, row.Min.y + (row.Max.y - row.Min.y) * along);
		}

		// Clicks the room below the rows, which gives the outliner the focus and selects nothing
		void FocusOutliner()
		{
			Click(ImVec2(100.0f, 560.0f));
		}

		HeadlessUI UI;
		SceneEditor Editing;
		SceneOutlinerPanel Outliner;
	};

	// A scene saved before there was an outline loads in the order its file has its entities; saved
	// with an outline, it keeps its folders and order, and a game loading it skips the outline
	void TestScenesSaveAndLoadTheirOutline()
	{
		TestDirectory directory("NyxOutlinerTests");

		SceneEditor editing;
		const Entity first = editing.Scene.CreateEntity("First");
		const Entity second = editing.Scene.CreateEntity("Second");
		const Entity third = editing.Scene.CreateEntity("Third");

		const std::filesystem::path older = directory.Path / "Older.nyxscene";
		Require(SceneSerializer::SaveToFile(editing.Scene.GetRegistry(), older), "A scene should save without an outline");
		{
			SceneDocument loaded;
			Require(SceneSerializer::LoadFromFile(older, loaded.GetRegistry(), {}, &loaded.GetOutline()), "A scene without an outline should load");
			Require(loaded.GetOutline().Folders.empty() && loaded.GetOutline().Entities.empty(), "A scene without an outline should load an empty one");
			RequireOutline(loaded, "First Second Third", "A scene without an outline should show its entities in the file's order");
		}

		// Third first, Second in a folder inside another
		const uint64_t outer = CreateOutlineFolder(editing, 0);
		const uint64_t inner = CreateOutlineFolder(editing, outer);
		RenameOutlineFolder(editing, outer, "Outer");
		RenameOutlineFolder(editing, inner, "Inner");
		MoveInOutline(editing, OutlineItems{ .Entities = { GuidOf(editing.Scene, third) } },
			OutlineDrop{ .Place = EOutlinePlace::Before, .Entity = GuidOf(editing.Scene, first) });
		MoveInOutline(editing, OutlineItems{ .Entities = { GuidOf(editing.Scene, second) } }, OutlineDrop{ .Place = EOutlinePlace::Into, .Folder = inner });
		const std::string arranged = "[Outer: [Inner: Second]] Third First";
		RequireOutline(editing.Scene, arranged, "The outline should be arranged");

		// A deleted entity isn't saved in the outline
		const Entity gone = editing.Scene.CreateEntity("Gone");
		editing.Scene.GetSelection() = gone;
		editing.DeleteSelectedEntity();

		const std::filesystem::path path = directory.Path / "Arranged.nyxscene";
		const SceneOutline saved = MakeSavedOutline(editing.Scene);
		Require(saved.Entities.size() == 3, "The saved outline should list the three entities there are, not the deleted one");
		Require(SceneSerializer::SaveToFile(editing.Scene.GetRegistry(), path, &saved), "A scene should save with its outline");
		{
			SceneDocument loaded;
			Require(SceneSerializer::LoadFromFile(path, loaded.GetRegistry(), {}, &loaded.GetOutline()), "A scene with an outline should load");
			RequireOutline(loaded, arranged, "A scene should load with its folders and order");
		}
		{
			// As a game loads it
			Registry world;
			Require(SceneSerializer::LoadFromFile(path, world), "A game should load a scene with an outline");
			size_t entities = 0;
			world.ForEachEntity([&](Entity)
				{
					++entities;
				});
			Require(entities == 3, "A game should get the scene's entities, and nothing of the outline");
		}

		// Saved again, as it loaded
		{
			SceneDocument loaded;
			SceneSerializer::LoadFromFile(path, loaded.GetRegistry(), {}, &loaded.GetOutline());
			const std::filesystem::path again = directory.Path / "Again.nyxscene";
			const SceneOutline outline = MakeSavedOutline(loaded);
			SceneSerializer::SaveToFile(loaded.GetRegistry(), again, &outline);
			SceneDocument reloaded;
			SceneSerializer::LoadFromFile(again, reloaded.GetRegistry(), {}, &reloaded.GetOutline());
			RequireOutline(reloaded, arranged, "A scene should keep its outline through a second save");
		}
	}

	// Moving an entity before or after another, and into and out of folders, one undo step each
	void TestReorderAndFoldersAreUndoable()
	{
		SceneEditor editing;
		const Entity a = editing.Scene.CreateEntity("A");
		const Entity b = editing.Scene.CreateEntity("B");
		const Entity c = editing.Scene.CreateEntity("C");
		const EntityGuid guidA = GuidOf(editing.Scene, a);
		const EntityGuid guidC = GuidOf(editing.Scene, c);
		RequireOutline(editing.Scene, "A B C", "New entities should show in the order they were made");

		Require(MoveInOutline(editing, OutlineItems{ .Entities = { guidC } }, OutlineDrop{ .Place = EOutlinePlace::Before, .Entity = guidA }),
			"C should move before A");
		RequireOutline(editing.Scene, "C A B", "C should be first");
		Require(MoveInOutline(editing, OutlineItems{ .Entities = { guidC } }, OutlineDrop{ .Place = EOutlinePlace::After, .Entity = GuidOf(editing.Scene, b) }),
			"C should move after B");
		RequireOutline(editing.Scene, "A B C", "C should be last again");
		Require(!MoveInOutline(editing, OutlineItems{ .Entities = { guidC } }, OutlineDrop{ .Place = EOutlinePlace::After, .Entity = GuidOf(editing.Scene, b) }),
			"A move to where the entity is already should be no undo step");

		editing.Undo();
		RequireOutline(editing.Scene, "C A B", "Undo should put C first again");
		editing.Undo();
		RequireOutline(editing.Scene, "A B C", "Undo should bring the first order back");
		editing.Redo();
		RequireOutline(editing.Scene, "C A B", "Redo should move C first again");

		// Into a folder and out again
		const uint64_t folder = CreateOutlineFolder(editing, 0);
		Require(folder != 0 && GetFolderName(editing.Scene, folder) == "New Folder", "A new folder should be called New Folder");
		RequireOutline(editing.Scene, "[New Folder] C A B", "A folder should show before the entities");
		Require(CreateOutlineFolder(editing, 0) != 0 && Describe(editing.Scene).starts_with("[New Folder] [New Folder (2)]"),
			"A second folder should get a name of its own");
		editing.Undo();

		Require(MoveInOutline(editing, OutlineItems{ .Entities = { guidA } }, OutlineDrop{ .Place = EOutlinePlace::Into, .Folder = folder }),
			"A should move into the folder");
		RequireOutline(editing.Scene, "[New Folder: A] C B", "A should be in the folder");
		Require(GetOutlineFolderOf(editing.Scene, guidA) == folder, "A's folder should be the folder");
		Require(MoveInOutline(editing, OutlineItems{ .Entities = { guidA } }, OutlineDrop{ .Place = EOutlinePlace::End }), "A should move out of the folder");
		RequireOutline(editing.Scene, "[New Folder] C B A", "A should be last at the top");

		editing.Undo();
		RequireOutline(editing.Scene, "[New Folder: A] C B", "Undo should put A back into the folder");
		editing.Undo();
		RequireOutline(editing.Scene, "[New Folder] C A B", "Undo should take A out of the folder, to where it was");
		editing.Undo();
		RequireOutline(editing.Scene, "C A B", "Undo should take the folder away");
		editing.Redo();
		Require(FindOutlineFolder(editing.Scene.GetOutline(), folder), "Redo should bring the folder back with its id");

		// Folders nest; never into themselves
		const uint64_t other = CreateOutlineFolder(editing, 0);
		RenameOutlineFolder(editing, other, "Other");
		Require(MoveInOutline(editing, OutlineItems{ .Folders = { other } }, OutlineDrop{ .Place = EOutlinePlace::Into, .Folder = folder }),
			"A folder should move into another");
		RequireOutline(editing.Scene, "[New Folder: [Other]] C A B", "Other should be inside New Folder");
		Require(!CanDropInOutline(editing.Scene, OutlineItems{ .Folders = { folder } }, OutlineDrop{ .Place = EOutlinePlace::Into, .Folder = other }),
			"A folder shouldn't go into a folder inside it");
		Require(!CanDropInOutline(editing.Scene, OutlineItems{ .Entities = { guidA } }, OutlineDrop{ .Place = EOutlinePlace::Before, .Folder = folder }),
			"An entity shouldn't go among folders");
		Require(MoveInOutline(editing, OutlineItems{ .Folders = { other } }, OutlineDrop{ .Place = EOutlinePlace::Before, .Folder = folder }),
			"A folder should move out, before another");
		RequireOutline(editing.Scene, "[Other] [New Folder] C A B", "Other should be first at the top");

		// A deleted entity comes back to its place
		editing.Scene.GetSelection() = a;
		editing.DeleteSelectedEntity();
		RequireOutline(editing.Scene, "[Other] [New Folder] C B", "A should be gone");
		editing.Undo();
		RequireOutline(editing.Scene, "[Other] [New Folder] C A B", "Undoing the delete should bring A back where it was");
	}

	// Deleting a folder keeps what is in it: it moves up a level, where the folder was
	void TestDeletingAFolderKeepsItsEntities()
	{
		SceneEditor editing;
		const Entity top = editing.Scene.CreateEntity("Top");
		const Entity lamp = editing.Scene.CreateEntity("Lamp");
		const Entity sun = editing.Scene.CreateEntity("Sun");
		const uint64_t first = CreateOutlineFolder(editing, 0);
		const uint64_t lights = CreateOutlineFolder(editing, 0);
		const uint64_t last = CreateOutlineFolder(editing, 0);
		RenameOutlineFolder(editing, first, "First");
		RenameOutlineFolder(editing, lights, "Lights");
		RenameOutlineFolder(editing, last, "Last");
		const uint64_t spots = CreateOutlineFolder(editing, lights);
		RenameOutlineFolder(editing, spots, "Spots");
		MoveInOutline(editing, OutlineItems{ .Entities = { GuidOf(editing.Scene, lamp), GuidOf(editing.Scene, sun) } }, OutlineDrop{ .Place = EOutlinePlace::Into, .Folder = lights });
		RequireOutline(editing.Scene, "[First] [Lights: [Spots] Lamp Sun] [Last] Top", "Lamp and Sun should be in Lights, moved together in their order");

		Require(DeleteOutlineFolder(editing, lights), "The folder should be deleted");
		Require(editing.Scene.GetRegistry().IsAlive(lamp) && editing.Scene.GetRegistry().IsAlive(sun) && editing.Scene.GetRegistry().IsAlive(top),
			"Deleting a folder should delete no entity");
		RequireOutline(editing.Scene, "[First] [Spots] [Last] Lamp Sun Top", "What was in the folder should be a level up, where the folder was");

		editing.Undo();
		RequireOutline(editing.Scene, "[First] [Lights: [Spots] Lamp Sun] [Last] Top", "Undo should bring the folder back with what was in it");
	}

	// A new entity goes right after the selection, in its folder; without one, at the end, or into
	// the folder named. Paste and duplicate too, each one undo step with its place.
	void TestNewEntitiesGoAfterTheSelection()
	{
		SceneEditor editing;
		const Entity a = editing.Scene.CreateEntity("A");
		editing.Scene.CreateEntity("B");
		const uint64_t folder = CreateOutlineFolder(editing, 0);
		RenameOutlineFolder(editing, folder, "F");
		const Entity inside = editing.Scene.CreateEntity("Inside");
		const Entity last = editing.Scene.CreateEntity("Last");
		MoveInOutline(editing, OutlineItems{ .Entities = { GuidOf(editing.Scene, inside), GuidOf(editing.Scene, last) } },
			OutlineDrop{ .Place = EOutlinePlace::Into, .Folder = folder });
		RequireOutline(editing.Scene, "[F: Inside Last] A B", "The scene should be arranged");

		editing.Scene.GetSelection() = a;
		editing.CreateEntity();
		RequireOutline(editing.Scene, "[F: Inside Last] A New Entity B", "A new entity should go after the selected one");
		editing.Undo();
		RequireOutline(editing.Scene, "[F: Inside Last] A B", "Undoing Add Entity should take it out of the outline");
		editing.Redo();
		RequireOutline(editing.Scene, "[F: Inside Last] A New Entity B", "Redoing Add Entity should put it back after A");

		editing.Scene.GetSelection() = inside;
		editing.DuplicateSelectedEntity();
		RequireOutline(editing.Scene, "[F: Inside Inside (2) Last] A New Entity B", "A duplicate should go after the original, in its folder");
		editing.Undo();
		RequireOutline(editing.Scene, "[F: Inside Last] A New Entity B", "Undoing a duplicate should take it out of the outline");

		editing.Scene.GetSelection().reset();
		editing.CreateEntity(folder);
		RequireOutline(editing.Scene, "[F: Inside Last New Entity] A New Entity B", "Without a selection, a new entity should go into the folder named");
		editing.Undo();
		editing.Scene.GetSelection().reset();
		editing.CreateEntity();
		RequireOutline(editing.Scene, "[F: Inside Last] A New Entity B New Entity", "Without a selection, a new entity should go at the end");
	}

	// The search shows the entities whose names have its text, whatever the case, and the folders
	// that hold them, open; Ctrl+F reaches it, the clear button empties it
	void TestSearchFiltersByName()
	{
		OutlinerFixture fixture;
		SceneEditor& editing = fixture.Editing;
		const Entity cube = editing.Scene.CreateEntity("Cube");
		const Entity sphere = editing.Scene.CreateEntity("Sphere");
		const Entity cubeLight = editing.Scene.CreateEntity("Cube Light");
		const uint64_t stuff = CreateOutlineFolder(editing, 0);
		const uint64_t lights = CreateOutlineFolder(editing, stuff);
		const uint64_t empty = CreateOutlineFolder(editing, 0);
		RenameOutlineFolder(editing, stuff, "Stuff");
		RenameOutlineFolder(editing, lights, "Lights");
		RenameOutlineFolder(editing, empty, "Empty");
		MoveInOutline(editing, OutlineItems{ .Entities = { GuidOf(editing.Scene, cubeLight) } }, OutlineDrop{ .Place = EOutlinePlace::Into, .Folder = lights });
		fixture.Outliner.Folders.SetOpen(stuff, false);

		Require(Describe(editing.Scene, FilterOutlineTree(BuildOutlineTree(editing.Scene), editing.Scene, "cUbE")) == "[Stuff: [Lights: Cube Light]] Cube",
			"The filter should keep the matching entities and the folders that hold them");

		fixture.Frames(3);
		Require(fixture.FindRow(cube) && fixture.FindRow(sphere) && fixture.FindFolderRow(stuff) && !fixture.FindRow(cubeLight),
			"Without a search, a closed folder should hide what is in it");

		fixture.FocusOutliner();
		fixture.Press(ImGuiKey_F, true);
		fixture.Frame();
		fixture.Type("CUBE");
		Require(fixture.Outliner.GetFilter() == "CUBE", "Ctrl+F should put the typing into the search");
		fixture.Frame();
		Require(fixture.FindRow(cube) && fixture.FindRow(cubeLight) && !fixture.FindRow(sphere), "The search should show the cubes, whatever the case, not the sphere");
		Require(fixture.FindFolderRow(stuff) && fixture.FindFolderRow(lights) && !fixture.FindFolderRow(empty),
			"The search should show the folders that hold a match, open, and not the others");
		Require(fixture.Row(cubeLight).ContentX > fixture.FolderRow(lights).ContentX && fixture.FolderRow(lights).ContentX > fixture.FolderRow(stuff).ContentX,
			"What a folder holds should be indented under it");
		Require(fixture.FolderRow(stuff).Max.y <= fixture.FolderRow(lights).Min.y + 0.5f && fixture.FolderRow(lights).Max.y <= fixture.Row(cubeLight).Min.y + 0.5f,
			"A folder's rows should follow it, top to bottom");

		// The clear button, right of the search
		ImGuiWindow* window = ImGui::FindWindowByName("Scene Outliner");
		Require(window != nullptr, "The outliner should be drawn");
		const float searchY = window->DC.CursorStartPos.y + ImGui::GetFrameHeight() * 0.5f;
		fixture.Click(ImVec2(window->WorkRect.Max.x - ImGui::GetFrameHeight() * 0.5f, searchY));
		fixture.Frame();
		Require(fixture.Outliner.GetFilter().empty(), "The clear button should empty the search");
		Require(fixture.FindRow(sphere) && fixture.FindFolderRow(empty) && !fixture.FindRow(cubeLight),
			"Without the search, every row should show again, and the closed folder be closed again");
	}

	// Dragging rows: before or after another, into a folder and out below the rows; each drop is
	// one undo step
	void TestDraggingRows()
	{
		OutlinerFixture fixture;
		SceneEditor& editing = fixture.Editing;
		const Entity a = editing.Scene.CreateEntity("A");
		const Entity b = editing.Scene.CreateEntity("B");
		const Entity c = editing.Scene.CreateEntity("C");
		const uint64_t folder = CreateOutlineFolder(editing, 0);
		RenameOutlineFolder(editing, folder, "F");
		const uint64_t other = CreateOutlineFolder(editing, 0);
		RenameOutlineFolder(editing, other, "G");
		fixture.Frames(3);

		// While C is dragged: a line at the top of A's row over its upper half, at the bottom over its
		// lower half, from where its content starts; a box round a folder; nothing over C itself
		const auto hover = [&](ImVec2 at)
		{
			fixture.MoveTo(at);
			fixture.Frame();
			return fixture.Outliner.GetDropHint();
		};
		fixture.MoveTo(OutlinerFixture::At(fixture.Row(c), 0.5f));
		ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
		fixture.Frame();
		const SceneOutlinerPanel::DrawnRow rowA = fixture.Row(a);
		const std::optional<SceneOutlinerPanel::DropHint> beforeA = hover(OutlinerFixture::At(rowA, 0.2f));
		Require(beforeA && !beforeA->bBox && std::abs(beforeA->Min.y - rowA.Min.y) < 0.5f && std::abs(beforeA->Max.y - rowA.Min.y) < 0.5f,
			"Over A's upper half, the drop line should be along A's top");
		Require(std::abs(beforeA->Min.x - rowA.ContentX) < 0.5f && beforeA->Max.x >= rowA.Max.x - 0.5f, "The drop line should span the row from its content");
		const std::optional<SceneOutlinerPanel::DropHint> afterA = hover(OutlinerFixture::At(rowA, 0.8f));
		Require(afterA && !afterA->bBox && std::abs(afterA->Min.y - rowA.Max.y) < 0.5f, "Over A's lower half, the drop line should be along A's bottom");
		const SceneOutlinerPanel::DrawnRow rowF = fixture.FolderRow(folder);
		const std::optional<SceneOutlinerPanel::DropHint> intoF = hover(OutlinerFixture::At(rowF, 0.2f));
		Require(intoF && intoF->bBox && std::abs(intoF->Min.y - rowF.Min.y) < 0.5f && std::abs(intoF->Max.y - rowF.Max.y) < 0.5f,
			"Over a folder, an entity should show a box round the folder's row, wherever on it");
		Require(!hover(OutlinerFixture::At(fixture.Row(c), 0.5f)), "Over its own row, the dragged entity should show no drop");
		ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
		fixture.Frames(2);
		RequireOutline(editing.Scene, "[F] [G] A B C", "Dropping a row on itself should move nothing");
		Require(!fixture.Outliner.GetDropHint(), "Without a drag, there should be no drop line");

		fixture.DragTo(OutlinerFixture::At(fixture.Row(c), 0.5f), OutlinerFixture::At(fixture.Row(a), 0.2f));
		RequireOutline(editing.Scene, "[F] [G] C A B", "C dropped on A's upper half should go before A");
		Require(!editing.Scene.GetSelection(), "Dragging a row shouldn't select it");

		fixture.DragTo(OutlinerFixture::At(fixture.Row(c), 0.5f), OutlinerFixture::At(fixture.Row(b), 0.8f));
		RequireOutline(editing.Scene, "[F] [G] A B C", "C dropped on B's lower half should go after B");

		fixture.DragTo(OutlinerFixture::At(fixture.Row(b), 0.5f), OutlinerFixture::At(fixture.FolderRow(folder), 0.5f));
		RequireOutline(editing.Scene, "[F: B] [G] A C", "B dropped on a folder should go into it");
		fixture.Frames(2);
		Require(fixture.FindRow(b) && fixture.Row(b).ContentX > fixture.FolderRow(folder).ContentX, "B should show in the folder, open, indented under it");

		fixture.DragTo(OutlinerFixture::At(fixture.FolderRow(other), 0.5f), OutlinerFixture::At(fixture.FolderRow(folder), 0.15f));
		RequireOutline(editing.Scene, "[G] [F: B] A C", "A folder dropped on another's top should go before it");

		fixture.DragTo(OutlinerFixture::At(fixture.Row(b), 0.5f), ImVec2(100.0f, 560.0f));
		RequireOutline(editing.Scene, "[G] [F] A C B", "B dropped below the rows should go to the end of the top");

		editing.Undo();
		RequireOutline(editing.Scene, "[G] [F: B] A C", "Undo should put B back into the folder");
		editing.Undo();
		editing.Undo();
		editing.Undo();
		RequireOutline(editing.Scene, "[F] [G] C A B", "Undo should take back each drop in turn");

		// The right-click menus open on rows inside folders too
		const auto openMenus = [&]()
		{
			int open = 0;
			for (const ImGuiPopupData& popup : ImGui::GetCurrentContext()->OpenPopupStack)
			{
				open += popup.Window != nullptr && popup.Window->Active ? 1 : 0;
			}
			return open;
		};
		MoveInOutline(editing, OutlineItems{ .Entities = { GuidOf(editing.Scene, b) }, .Folders = { other } }, OutlineDrop{ .Place = EOutlinePlace::Into, .Folder = folder });
		fixture.Frames(2);
		for (const auto& [at, what] : { std::pair{ OutlinerFixture::At(fixture.Row(b), 0.5f), "an entity" }, std::pair{ OutlinerFixture::At(fixture.FolderRow(other), 0.5f), "a folder" } })
		{
			fixture.Click(at, ImGuiMouseButton_Right);
			fixture.Frames(2);
			Require(openMenus() == 1, std::string("A right-click on ") + what + " inside a folder should open its menu");
			fixture.Click(ImVec2(100.0f, 560.0f));
			fixture.Frames(2);
			Require(openMenus() == 0, "A click elsewhere should close the menu");
		}
		editing.Undo();

		// A click still selects
		fixture.Frames(2);
		fixture.Click(OutlinerFixture::At(fixture.Row(a), 0.5f));
		Require(editing.Scene.GetSelection() == a, "A click on a row should select its entity");
		fixture.Click(OutlinerFixture::At(fixture.FolderRow(folder), 0.5f));
		Require(!editing.Scene.GetSelection() && fixture.Outliner.GetSelectedFolder() == folder, "A click on a folder should select the folder, and no entity");
	}

	// New Folder, renaming by F2 and by a double click, Esc keeping the old name, Delete Selected
	// deleting a selected folder but not what is in it
	void TestFolderCommands()
	{
		OutlinerFixture fixture;
		SceneEditor& editing = fixture.Editing;
		const Entity a = editing.Scene.CreateEntity("A");
		const Entity b = editing.Scene.CreateEntity("B");
		editing.Scene.GetSelection() = b;
		fixture.Frames(3);

		// New Folder: beside the selected entity, selected, its name ready to type over
		ImGuiWindow* window = ImGui::FindWindowByName("Scene Outliner");
		Require(window != nullptr, "The outliner should be drawn");
		const size_t before = editing.Scene.GetOutline().Folders.size();
		// The toolbar's second button: Add Entity, then New Folder
		const float toolbarY = window->DC.CursorStartPos.y + ImGui::GetFrameHeightWithSpacing() + ImGui::GetFrameHeight() * 0.5f;
		const float newFolderX = window->DC.CursorStartPos.x + Nyx::UI::CalcButtonSize("Add Entity").x + ImGui::GetStyle().ItemSpacing.x + 10.0f;
		fixture.Click(ImVec2(newFolderX, toolbarY));
		Require(editing.Scene.GetOutline().Folders.size() == before + 1, "New Folder should make a folder");
		const uint64_t folder = editing.Scene.GetOutline().Folders.back().Id;
		Require(fixture.Outliner.GetSelectedFolder() == folder && !editing.Scene.GetSelection(), "The new folder should be selected");
		Require(fixture.Outliner.GetRenamingFolder() == folder, "The new folder should be being renamed");
		fixture.Frames(2);
		fixture.Type("Props");
		fixture.Press(ImGuiKey_Enter);
		Require(GetFolderName(editing.Scene, folder) == "Props", "Typing and Enter should rename the new folder");
		Require(fixture.Outliner.GetRenamingFolder() == 0, "Enter should end the renaming");
		editing.Undo();
		Require(GetFolderName(editing.Scene, folder) == "New Folder", "Undo should take the new name back");
		editing.Redo();

		// F2 while the outliner has the focus; Esc keeps the old name
		fixture.Frames(2);
		fixture.Click(OutlinerFixture::At(fixture.FolderRow(folder), 0.5f));
		fixture.Press(ImGuiKey_F2);
		Require(fixture.Outliner.GetRenamingFolder() == folder, "F2 should rename the selected folder");
		fixture.Frames(2);
		fixture.Type("Junk");
		fixture.Press(ImGuiKey_Escape);
		Require(GetFolderName(editing.Scene, folder) == "Props" && fixture.Outliner.GetRenamingFolder() == 0, "Esc should keep the old name");

		// A double click on the name
		fixture.Frames(2);
		const ImVec2 name = OutlinerFixture::At(fixture.FolderRow(folder), 0.5f);
		fixture.Click(name);
		fixture.Click(name);
		Require(fixture.Outliner.GetRenamingFolder() == folder, "A double click on a folder's name should rename it");
		fixture.Frames(2);
		fixture.Type("Set");
		fixture.Press(ImGuiKey_Enter);
		Require(GetFolderName(editing.Scene, folder) == "Set", "The double click's renaming should take the name");

		// Delete Selected on the folder keeps its entity
		MoveInOutline(editing, OutlineItems{ .Entities = { GuidOf(editing.Scene, a) } }, OutlineDrop{ .Place = EOutlinePlace::Into, .Folder = folder });
		fixture.Frames(2);
		fixture.Click(OutlinerFixture::At(fixture.FolderRow(folder), 0.5f));
		Require(fixture.Outliner.GetSelectedFolder() == folder, "The folder should be selected");
		const float deleteX = newFolderX + Nyx::UI::CalcButtonSize("New Folder").x + ImGui::GetStyle().ItemSpacing.x;
		fixture.Click(ImVec2(deleteX, toolbarY));
		Require(!FindOutlineFolder(editing.Scene.GetOutline(), folder), "Delete Selected should delete the selected folder");
		Require(editing.Scene.GetRegistry().IsAlive(a), "Deleting the folder should keep its entity");
		RequireOutline(editing.Scene, "A B", "The folder's entity should be at the top again");
	}

	// Open and closed folders are the editor's, per scene: kept in their file between runs, not in
	// the scene or its undo
	void TestFolderStatesAreRememberedPerScene()
	{
		TestDirectory directory("NyxOutlinerFolders");
		const std::filesystem::path file = directory.Path / "OutlinerFolders.txt";
		const std::filesystem::path sceneA = directory.Path / "A.nyxscene";
		const std::filesystem::path sceneB = directory.Path / "B.nyxscene";
		{
			OutlinerFolderStates states;
			states.SetFile(file);
			states.SetScene(sceneA);
			Require(states.IsOpen(7), "A folder should start open");
			states.SetOpen(7, false);
			states.SetOpen(0xABCDEF0123456789ull, false);
			states.SetScene(sceneB);
			Require(states.IsOpen(7), "Another scene's folder of the same id should be open");
		}
		{
			OutlinerFolderStates states;
			states.SetFile(file);
			states.SetScene(sceneA);
			Require(!states.IsOpen(7) && !states.IsOpen(0xABCDEF0123456789ull), "The closed folders should be read back for their scene");
			states.SetOpen(7, true);
			states.MoveScene(sceneB);
			Require(!states.IsOpen(0xABCDEF0123456789ull), "Saving the scene as another file should keep its folders");
		}
		{
			OutlinerFolderStates states;
			states.SetFile(file);
			states.SetScene(sceneB);
			Require(states.IsOpen(7) && !states.IsOpen(0xABCDEF0123456789ull), "The moved scene's folders should be read back under its new path");
		}

		// Closing a folder in the outliner is no undo step
		OutlinerFixture fixture;
		const Entity inside = fixture.Editing.Scene.CreateEntity("Inside");
		const uint64_t folder = CreateOutlineFolder(fixture.Editing, 0);
		MoveInOutline(fixture.Editing, OutlineItems{ .Entities = { GuidOf(fixture.Editing.Scene, inside) } }, OutlineDrop{ .Place = EOutlinePlace::Into, .Folder = folder });
		fixture.Frames(3);
		const SceneOutlinerPanel::DrawnRow& row = fixture.FolderRow(folder);
		fixture.Click(ImVec2(row.ContentX + Nyx::UI::CalcTreeRowLabelOffset(Nyx::UI::ETreeRowIcon::None) * 0.5f, (row.Min.y + row.Max.y) * 0.5f));
		fixture.Frame();
		Require(!fixture.Outliner.Folders.IsOpen(folder), "A click on a folder's arrow should close it");
		Require(fixture.Outliner.GetDrawnRows().size() == 1, "A closed folder should hide its rows");
		fixture.Editing.Undo();
		RequireOutline(fixture.Editing.Scene, "[New Folder] Inside", "Undo should undo the move into the folder, as closing it is no undo step");
		Require(!fixture.Outliner.Folders.IsOpen(folder), "Undo should leave the folder closed");
	}
}

int main()
{
	try
	{
		Nyx::Logger::Get().Init();
		RegisterComponentTypes();

		TestScenesSaveAndLoadTheirOutline();
		TestReorderAndFoldersAreUndoable();
		TestDeletingAFolderKeepsItsEntities();
		TestNewEntitiesGoAfterTheSelection();
		TestSearchFiltersByName();
		TestDraggingRows();
		TestFolderCommands();
		TestFolderStatesAreRememberedPerScene();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Scene outliner test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All scene outliner tests passed.\n";
	return 0;
}
