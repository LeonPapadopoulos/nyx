// The pieces EditorLayer is built from, without a window or renderer: SceneEditor's commands and
// hotkeys, and the panels drawn headless with ImGui.
#include "ComponentRegistration.h"
#include "CrashCardsPanel.h"
#include "DetailsPanel.h"
#include "Log.h"
#include "NameComponent.h"
#include "Painter.h"
#include "PainterTestWindow.h"
#include "PlaySession.h"
#include "AssetDatabase.h"
#include "GameLinkPanel.h"
#include "PropertyWidgetRegistry.h"
#include "SceneEditor.h"
#include "SceneFilePopups.h"
#include "SceneOutlinerPanel.h"
#include "TransformComponent.h"
#include "UIAnimation.h"
#include "UITheme.h"
#include "UIWidgets.h"
#include "WindowPainter.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
	using namespace Nyx::Engine;
	using namespace Nyx::Editor;

	void Require(bool condition, const std::string& message)
	{
		if (!condition)
		{
			throw std::runtime_error(message);
		}
	}

	size_t CountEntities(const SceneEditor& editing)
	{
		size_t count = 0;
		editing.Scene.GetRegistry().ForEachEntity([&](Entity)
			{
				++count;
			});
		return count;
	}

	std::string GetName(const SceneEditor& editing, Entity entity)
	{
		return editing.Scene.GetRegistry().Get<NameComponent>(entity).Name;
	}

	// A headless ImGui context for the duration of a test
	class HeadlessUI
	{
	public:
		HeadlessUI()
		{
			ImGui::CreateContext();
			ImGuiIO& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.DisplaySize = ImVec2(1280, 800);
			io.DeltaTime = 1.0f / 60.0f;
			unsigned char* pixels = nullptr;
			int width = 0;
			int height = 0;
			io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		}

		~HeadlessUI()
		{
			ImGui::DestroyContext();
		}

		void Frame(const std::function<void()>& draw)
		{
			ImGui::NewFrame();
			draw();
			ImGui::Render();
		}

		// One frame in which the key chord is pressed, then one in which it is released
		void PressChord(ImGuiKey key, bool bShift, const std::function<void()>& handle)
		{
			ImGuiIO& io = ImGui::GetIO();
			io.AddKeyEvent(ImGuiMod_Ctrl, true);
			io.AddKeyEvent(ImGuiMod_Shift, bShift);
			io.AddKeyEvent(key, true);
			Frame(handle);

			io.AddKeyEvent(key, false);
			io.AddKeyEvent(ImGuiMod_Shift, false);
			io.AddKeyEvent(ImGuiMod_Ctrl, false);
			Frame(handle);
		}
	};

	void TestCreateAndDeleteAreUndoable()
	{
		SceneEditor editing;

		editing.CreateEntity();
		Require(CountEntities(editing) == 1, "Add Entity should make one entity");
		Require(editing.Scene.GetSelection().has_value(), "A new entity should be selected");
		const EntityGuid guid = editing.Scene.GetGuid(*editing.Scene.GetSelection());

		editing.DeleteSelectedEntity();
		Require(CountEntities(editing) == 0 && !editing.Scene.GetSelection(), "Delete should remove and deselect the entity");

		editing.Undo();
		Require(editing.Scene.FindEntity(guid).has_value(), "Undoing Delete should bring the entity back with its guid");

		editing.Undo();
		Require(CountEntities(editing) == 0, "Undoing Add Entity should remove it again");

		editing.Redo();
		Require(editing.Scene.FindEntity(guid).has_value(), "Redoing Add Entity should bring it back");
	}

	void TestSelectionFollowsGuid()
	{
		SceneEditor editing;
		editing.CreateEntity();
		const EntityGuid firstGuid = editing.Scene.GetGuid(*editing.Scene.GetSelection());
		editing.DeleteSelectedEntity();

		// Takes the deleted entity's slot, so undo brings it back in another one
		const Entity other = editing.Scene.CreateEntity("Other");
		editing.Scene.GetSelection() = other;

		editing.Undo();
		Require(editing.Scene.GetSelection() == other, "Undo should keep the selection on the same entity");

		const std::optional<Entity> restored = editing.Scene.FindEntity(firstGuid);
		Require(restored.has_value() && *restored != other, "Undo should restore the deleted entity in a free slot");

		editing.Scene.GetSelection() = *restored;
		editing.Redo();
		Require(!editing.Scene.GetSelection(), "Redoing the delete should clear the selection of the deleted entity");
	}

	void TestDuplicateCopyAndPaste()
	{
		SceneEditor editing;
		const Entity cube = editing.Scene.CreateEntity("Cube");
		editing.Scene.GetRegistry().Add<TransformComponent>(cube, TransformComponent{});
		editing.Scene.GetSelection() = cube;

		Require(!editing.CanPaste(), "Nothing should be pasteable before a copy");
		editing.PasteEntity();
		Require(CountEntities(editing) == 1, "Paste with an empty clipboard should do nothing");

		editing.DuplicateSelectedEntity();
		const Entity duplicate = *editing.Scene.GetSelection();
		Require(duplicate != cube && GetName(editing, duplicate) == "Cube (2)", "Duplicate should select a renamed copy");
		Require(editing.Scene.GetGuid(duplicate) != editing.Scene.GetGuid(cube), "A duplicate should get a new guid");
		Require(!editing.CanPaste(), "Duplicate should leave the clipboard alone");

		editing.Scene.GetSelection() = cube;
		editing.CopySelectedEntity();
		Require(editing.CanPaste(), "Copy should fill the clipboard");
		editing.PasteEntity();
		Require(CountEntities(editing) == 3 && GetName(editing, *editing.Scene.GetSelection()) == "Cube (3)",
			"Paste should select a renamed copy");

		editing.Undo();
		Require(CountEntities(editing) == 2, "Paste should be undoable");
	}

	void TestForgetScene()
	{
		SceneEditor editing;
		editing.CreateEntity();

		editing.ForgetScene();

		Require(!editing.Scene.GetSelection(), "Forgetting the scene should clear the selection");
		Require(editing.SceneRevision == 1, "Forgetting the scene should count a new revision");
		Require(!editing.Transactions.Undo(editing.TransactionContext), "Forgetting the scene should clear undo");
	}

	void TestHotkeys()
	{
		HeadlessUI ui;
		SceneEditor editing;
		const auto handle = [&]()
		{
			editing.HandleHotkeys();
		};

		const Entity cube = editing.Scene.CreateEntity("Cube");
		editing.Scene.GetSelection() = cube;

		ui.PressChord(ImGuiKey_D, false, handle);
		Require(CountEntities(editing) == 2, "Ctrl+D should duplicate");

		ui.PressChord(ImGuiKey_Z, false, handle);
		Require(CountEntities(editing) == 1, "Ctrl+Z should undo");

		ui.PressChord(ImGuiKey_Z, true, handle);
		Require(CountEntities(editing) == 2, "Ctrl+Shift+Z should redo");

		ui.PressChord(ImGuiKey_Z, false, handle);
		ui.PressChord(ImGuiKey_Y, false, handle);
		Require(CountEntities(editing) == 2, "Ctrl+Y should redo");

		editing.Scene.GetSelection() = cube;
		ui.PressChord(ImGuiKey_C, false, handle);
		ui.PressChord(ImGuiKey_V, false, handle);
		Require(CountEntities(editing) == 3 && editing.CanPaste(), "Ctrl+C and Ctrl+V should copy and paste");
	}

	void TestPanelsDrawHeadless()
	{
		HeadlessUI ui;
		RegisterDefaultPropertyWidgets();

		SceneEditor editing;
		const Entity cube = editing.Scene.CreateEntity("Cube");
		editing.Scene.GetRegistry().Add<TransformComponent>(cube, TransformComponent{});

		SceneOutlinerPanel outliner;
		DetailsPanel details;

		// Nothing selected, then the cube, then an entity that was deleted meanwhile
		ui.Frame([&]()
			{
				outliner.Draw(editing);
				details.Draw(editing);
			});

		editing.Scene.GetSelection() = cube;
		ui.Frame([&]()
			{
				outliner.Draw(editing);
				details.Draw(editing);
			});
		Require(editing.DetailsContext.CurrentObjectRef.IsValid(), "The details panel should target the selected entity");

		editing.Scene.DestroyEntity(cube);
		ui.Frame([&]()
			{
				details.Draw(editing);
			});
		Require(!editing.Scene.GetSelection(), "The details panel should drop a selection that is gone");

		// A closed panel draws nothing
		outliner.bOpen = false;
		ui.Frame([&]()
			{
				outliner.Draw(editing);
				Require(ImGui::FindWindowByName("Scene Outliner") == nullptr || !ImGui::FindWindowByName("Scene Outliner")->Active,
					"A closed outliner should not be drawn");
			});
	}

	void TestCrashCards()
	{
		HeadlessUI ui;
		CrashCardsPanel crashCards;

		std::optional<GameCrash> restart;
		ui.Frame([&]()
			{
				restart = crashCards.Draw();
			});
		Require(!restart && crashCards.IsEmpty(), "Without crashes there should be no cards");

		GameCrash crash;
		crash.GameName = "Game 2";
		crash.ExitCode = 0xC0000005;
		crashCards.Add(crash, "last line");
		ui.Frame([&]()
			{
				restart = crashCards.Draw();
				Require(ImGui::FindWindowByName("Game Crashed") != nullptr, "A crash should show the Game Crashed window");
			});
		Require(!restart && !crashCards.IsEmpty(), "A card should stay until dismissed or restarted");
	}

	void TestScenePopupsAndIdleSession()
	{
		HeadlessUI ui;
		SceneFilePopups popups;
		AssetDatabase assets;

		popups.RequestSaveAs();
		SceneFileRequest request;
		ui.Frame([&]()
			{
				request = popups.Draw(assets);
				Require(ImGui::IsPopupOpen("Save Scene As"), "RequestSaveAs should open the Save Scene As popup");
			});
		Require(!request.Load && !request.SaveAs, "Opening a popup should not request anything");

		// Play's commands do nothing harmful without games
		Nyx::SceneDocument scene;
		GameLinkPanel gameLink;
		PlaySession session(scene, gameLink);
		session.Stop();
		session.OnSceneReplaced();
		session.Update();
		Require(!session.IsRunning() && !session.IsQuitting() && session.TakeCrashes().empty(), "An idle session should stay idle");
	}

	// The painted widgets (UIWidgets.h) behave as ImGui's own: mouse, keyboard, disabled, and the
	// last-item queries.
	void TestPaintedWidgetsBehaveLikeImGuis()
	{
		HeadlessUI ui;
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

		int clicks = 0;
		bool bToggle = false;
		float gain = 0.25f;
		bool bButtonHovered = false;
		bool bButtonActive = false;
		bool bFocusButton = false;
		bool bDisabled = false;
		ImRect button;
		ImRect toggle;
		ImRect slider;
		const auto draw = [&]()
			{
				ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
				ImGui::SetNextWindowSize(ImVec2(400.0f, 300.0f));
				ImGui::Begin("Painted widgets");
				if (bFocusButton)
				{
					// As Tab or an arrow key would: focus, and show the keyboard's cursor, without
					// which ImGui doesn't activate by keyboard
					ImGui::SetKeyboardFocusHere();
					ImGui::SetNavCursorVisible(true);
				}
				ImGui::BeginDisabled(bDisabled);
				if (Nyx::UI::Button("Apply"))
				{
					++clicks;
				}
				ImGui::EndDisabled();
				bButtonHovered = ImGui::IsItemHovered();
				bButtonActive = ImGui::IsItemActive();
				button = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

				Nyx::UI::Toggle("Snap", &bToggle);
				toggle = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

				ImGui::SetNextItemWidth(200.0f);
				Nyx::UI::SliderFloat("Gain", &gain, 0.0f, 1.0f);
				// The slider's frame: the item without its label
				slider = ImRect(ImGui::GetItemRectMin(), ImVec2(ImGui::GetItemRectMin().x + 200.0f, ImGui::GetItemRectMax().y));
				ImGui::End();
			};
		const auto click = [&](ImVec2 at)
			{
				io.AddMousePosEvent(at.x, at.y);
				ui.Frame(draw);
				io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
				ui.Frame(draw);
				io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
				ui.Frame(draw);
			};

		// Lay out once, then hover the button, press and release it
		ui.Frame(draw);
		io.AddMousePosEvent(button.GetCenter().x, button.GetCenter().y);
		ui.Frame(draw);
		Require(bButtonHovered && !bButtonActive, "IsItemHovered() should be true over a painted button");
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
		ui.Frame(draw);
		Require(bButtonActive && clicks == 0, "A held painted button should be active, and not clicked before it is released");
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
		ui.Frame(draw);
		Require(clicks == 1, "Releasing over a painted button should click it");

		click(toggle.GetCenter());
		Require(bToggle, "Clicking a toggle should switch it on");

		// The slider's left end is its minimum; its right end, past the track, its maximum.
		click(ImVec2(slider.Min.x + 1.0f, slider.GetCenter().y));
		Require(gain == 0.0f, "Clicking a slider's left end should set its minimum, got " + std::to_string(gain));
		click(ImVec2(slider.Max.x - 1.0f, slider.GetCenter().y));
		Require(gain == 1.0f, "Clicking a slider's right end should set its maximum, got " + std::to_string(gain));

		// The keyboard: focus the button, then Space clicks it.
		io.AddMousePosEvent(-100.0f, -100.0f);
		bFocusButton = true;
		ui.Frame(draw);
		bFocusButton = false;
		ui.Frame(draw);
		io.AddKeyEvent(ImGuiKey_Space, true);
		ui.Frame(draw);
		io.AddKeyEvent(ImGuiKey_Space, false);
		ui.Frame(draw);
		Require(clicks == 2, "Space should click a painted button with keyboard focus, clicks " + std::to_string(clicks));

		// Disabled, it ignores the mouse.
		bDisabled = true;
		click(button.GetCenter());
		Require(clicks == 2, "A disabled painted button shouldn't click");
	}

	// Nyx::UI::TreeRow() behaves as ImGui's tree nodes do: the row selects, the arrow, a double
	// click and the Left and Right keys open and close, and children are indented.
	void TestTreeRowsBehaveLikeTreeNodes()
	{
		HeadlessUI ui;
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

		int clicks = 0;
		bool bOpen = false;
		bool bHovered = false;
		bool bFocus = false;
		ImRect row;
		float rowContentX = 0.0f;
		float childContentX = 0.0f;
		float workMinX = 0.0f;
		float workMaxX = 0.0f;
		const auto draw = [&]()
			{
				ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
				ImGui::SetNextWindowSize(ImVec2(300.0f, 200.0f));
				ImGui::Begin("Tree rows");
				workMinX = ImGui::GetCurrentWindow()->WorkRect.Min.x;
				workMaxX = ImGui::GetCurrentWindow()->WorkRect.Max.x;
				if (bFocus)
				{
					ImGui::SetKeyboardFocusHere();
					ImGui::SetNavCursorVisible(true);
				}
				Nyx::UI::TreeRowOptions options;
				options.Icon = Nyx::UI::ETreeRowIcon::Folder;
				rowContentX = ImGui::GetCursorScreenPos().x;
				const Nyx::UI::TreeRowResult result = Nyx::UI::TreeRow("Assets", options);
				clicks += result.bClicked ? 1 : 0;
				bOpen = result.bOpen;
				bHovered = ImGui::IsItemHovered();
				row = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
				if (result.bOpen)
				{
					childContentX = ImGui::GetCursorScreenPos().x;
					Nyx::UI::TreeRowOptions leaf;
					leaf.bLeaf = true;
					Nyx::UI::TreeRow("Meshes", leaf);
					Nyx::UI::TreeRowEnd();
				}
				ImGui::End();
			};
		const auto press = [&](ImVec2 at)
			{
				io.AddMousePosEvent(at.x, at.y);
				ui.Frame(draw);
				io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
				ui.Frame(draw);
				io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
				ui.Frame(draw);
			};
		const auto pressKey = [&](ImGuiKey key)
			{
				io.AddKeyEvent(key, true);
				ui.Frame(draw);
				io.AddKeyEvent(key, false);
				ui.Frame(draw);
			};

		ui.Frame(draw);
		Require(row.Min.x == workMinX && row.Max.x == workMaxX, "A tree row should span the window's width");

		const ImVec2 label(rowContentX + ImGui::GetFontSize() * 4.0f, row.GetCenter().y);
		const ImVec2 arrow(rowContentX + ImGui::GetFontSize() * 0.5f, row.GetCenter().y);
		io.AddMousePosEvent(label.x, label.y);
		ui.Frame(draw);
		Require(bHovered, "IsItemHovered() should be true over a tree row");

		press(label);
		Require(clicks == 1 && !bOpen, "Clicking a row's label should select it, and not open it");

		press(arrow);
		Require(clicks == 1 && bOpen, "Clicking a row's arrow should open it, and not select it");
		Require(childContentX == rowContentX + ImGui::GetStyle().IndentSpacing, "An open row's children should be indented");
		press(arrow);
		Require(!bOpen, "Clicking the arrow again should close the row");

		// Two clicks in a row on the label: a double click
		press(label);
		press(label);
		Require(bOpen && clicks == 3, "A double click on the label should open the row, and select it");

		// The keyboard: Left closes the row with focus, Right opens it.
		io.AddMousePosEvent(-100.0f, -100.0f);
		bFocus = true;
		ui.Frame(draw);
		bFocus = false;
		ui.Frame(draw);
		pressKey(ImGuiKey_LeftArrow);
		Require(!bOpen, "Left should close an open row with keyboard focus");
		pressKey(ImGuiKey_RightArrow);
		Require(bOpen, "Right should open a closed row with keyboard focus");
	}

	// Fields are ImGui's own over a painted frame: ImGui keeps dragging and typing, and the frame
	// is painted exactly where ImGui's goes. Property rows lay out a label column and a value
	// column, which starts at the same place at any indentation. Collapsing headers keep their
	// open state where ImGui's do.
	void TestFieldsAndPropertyRows()
	{
		HeadlessUI ui;
		ImGuiIO& io = ImGui::GetIO();
		ImGuiContext& g = *GImGui;

		float number = 1.0f;
		float vector[3] = { 0.0f, 0.0f, 0.0f };
		std::string text = "Cube";
		bool bNumberActivated = false;
		bool bNumberCommitted = false;
		bool bVectorCommitted = false;
		bool bLabelHovered = false;
		bool bFramePainted = false;
		bool bStyleRestored = false;
		bool bImGuiHeader = false;
		bool bHeaderOpen = false;
		ImRect header;
		ImRect label;
		ImRect numberField;
		ImRect vectorField;
		ImRect textField;
		float workMinX = 0.0f;
		float workMaxX = 0.0f;
		float firstRowTop = 0.0f;
		float secondRowTop = 0.0f;
		float nestedValueX = 0.0f;
		const auto draw = [&]()
			{
				ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
				ImGui::SetNextWindowSize(ImVec2(400.0f, 300.0f));
				ImGui::Begin("Fields");
				workMinX = ImGui::GetCurrentWindow()->WorkRect.Min.x;
				workMaxX = ImGui::GetCurrentWindow()->WorkRect.Max.x;

				bHeaderOpen = bImGuiHeader ? ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen) : Nyx::UI::CollapsingHeader("Transform");
				header = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

				firstRowTop = ImGui::GetCursorScreenPos().y;
				Nyx::UI::BeginPropertyRow("Intensity");
				bLabelHovered = ImGui::IsItemHovered();
				label = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
				const int colors = g.ColorStack.Size;
				const int styleVars = g.StyleVarStack.Size;
				Nyx::UI::DragFloat("##Number", &number, 0.5f);
				bNumberActivated |= ImGui::IsItemActivated();
				bNumberCommitted |= ImGui::IsItemDeactivatedAfterEdit();
				numberField = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
				bStyleRestored = g.ColorStack.Size == colors && g.StyleVarStack.Size == styleVars;
				Nyx::UI::EndPropertyRow();

				secondRowTop = ImGui::GetCursorScreenPos().y;
				Nyx::UI::BeginPropertyRow("Position");
				Nyx::UI::DragVector("##Vector", vector, 3, 1.0f);
				bVectorCommitted |= ImGui::IsItemDeactivatedAfterEdit();
				vectorField = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
				Nyx::UI::EndPropertyRow();

				Nyx::UI::BeginPropertyRow("Name");
				Nyx::UI::InputText("##Text", &text);
				textField = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
				Nyx::UI::EndPropertyRow();

				Nyx::UI::TreeRowOptions group;
				group.bDefaultOpen = true;
				group.bToggleOnClick = true;
				if (Nyx::UI::TreeRow("Lighting", group).bOpen)
				{
					Nyx::UI::BeginPropertyRow("Shadows");
					nestedValueX = ImGui::GetCursorScreenPos().x;
					ImGui::TextUnformatted("On");
					Nyx::UI::EndPropertyRow();
					Nyx::UI::TreeRowEnd();
				}

				// The number field's painted frame: a filled rect where ImGui's frame is
				bFramePainted = false;
				for (const PainterCommand& command : Nyx::UI::GetWindowPainter().GetPainter().GetCommands())
				{
					const PainterRect rect = GetPayload<PainterRectPayload>(command).Rect;
					bFramePainted |= command.Type == static_cast<uint32_t>(EPainterCommandType::Rect) && command.OutlineWidth == 0.0f &&
						rect.Min == glm::vec2(numberField.Min.x, numberField.Min.y) && rect.Max == glm::vec2(numberField.Max.x, numberField.Max.y);
				}
				ImGui::End();
			};
		const auto moveTo = [&](ImVec2 at)
			{
				io.AddMousePosEvent(at.x, at.y);
				ui.Frame(draw);
			};
		const auto press = [&](ImVec2 at)
			{
				moveTo(at);
				io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
				ui.Frame(draw);
				io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
				ui.Frame(draw);
			};
		const auto pressKey = [&](ImGuiKey key)
			{
				io.AddKeyEvent(key, true);
				ui.Frame(draw);
				io.AddKeyEvent(key, false);
				ui.Frame(draw);
			};

		ui.Frame(draw);
		ui.Frame(draw);
		const float valueX = Nyx::UI::CalcPropertyValueX(workMinX, workMaxX);
		const float valueRight = workMaxX - Nyx::UI::GetTheme().RowPillMargin * 2.0f;
		Require(numberField.Min.x == valueX && numberField.Max.x == valueRight && numberField.Min.y == firstRowTop,
			"A property row's value should fill its value column, from the row's top");
		Require(secondRowTop == firstRowTop + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y, "Property rows should be a frame high");
		Require(vectorField.Min.x == valueX && vectorField.Max.x == valueRight, "A vector field's components should share the value column");
		Require(nestedValueX == valueX, "A property row inside a tree row should keep the value column where it is");
		Require(bFramePainted, "A number field's frame should be painted exactly where ImGui's frame goes");
		Require(bStyleRestored, "A field should give ImGui's style back as it found it");

		moveTo(label.GetCenter());
		Require(bLabelHovered, "IsItemHovered() after BeginPropertyRow() should be true over the label");

		// Dragging the number to the right raises it, and records one edit when let go.
		moveTo(numberField.GetCenter());
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
		ui.Frame(draw);
		Require(bNumberActivated, "Pressing a number field should activate it");
		for (int step = 1; step <= 4; ++step)
		{
			moveTo(ImVec2(numberField.GetCenter().x + 5.0f * static_cast<float>(step), numberField.GetCenter().y));
		}
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
		ui.Frame(draw);
		Require(number > 1.0f && bNumberCommitted, "Dragging a number field should raise it and commit the edit, got " + std::to_string(number));

		// A double click types into it instead.
		press(numberField.GetCenter());
		press(numberField.GetCenter());
		io.AddKeyEvent(ImGuiMod_Ctrl, true);
		pressKey(ImGuiKey_A);
		io.AddKeyEvent(ImGuiMod_Ctrl, false);
		io.AddInputCharactersUTF8("42");
		ui.Frame(draw);
		pressKey(ImGuiKey_Enter);
		Require(number == 42.0f, "Typing into a number field after a double click should set it, got " + std::to_string(number));

		// Dragging the vector's middle component changes only Y.
		const ImVec2 y = vectorField.GetCenter();
		moveTo(y);
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
		ui.Frame(draw);
		for (int step = 1; step <= 4; ++step)
		{
			moveTo(ImVec2(y.x + 5.0f * static_cast<float>(step), y.y));
		}
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
		ui.Frame(draw);
		Require(vector[0] == 0.0f && vector[1] > 0.0f && vector[2] == 0.0f, "Dragging a vector's Y should change only Y");
		Require(bVectorCommitted, "IsItemDeactivatedAfterEdit() after a vector field should cover its components");

		// Typing at the end of the text field
		press(ImVec2(textField.Max.x - 4.0f, textField.GetCenter().y));
		io.AddInputCharactersUTF8("s");
		ui.Frame(draw);
		pressKey(ImGuiKey_Enter);
		Require(text == "Cubes", "Typing into a text field should change its text, got " + text);

		// The header: open at first, closed by a click, and ImGui's own header under the same
		// label finds it closed.
		Require(bHeaderOpen, "A collapsing header should be open at first");
		press(header.GetCenter());
		Require(!bHeaderOpen, "Clicking a collapsing header should close it");
		bImGuiHeader = true;
		ui.Frame(draw);
		Require(!bHeaderOpen, "ImGui's collapsing header under the same label should find it closed");
	}

	void TestAnimationsEaseAndAreForgotten()
	{
		HeadlessUI ui;
		const ImGuiID id = 0x1234;
		constexpr float Speed = 18.0f;
		float value = -1.0f;

		ui.Frame([&]() { value = Nyx::UI::Animate(id, 0.0f, Speed); });
		Require(value == 0.0f, "An animation should start at its target");

		// One frame of 1/60 s towards 1: 1 - e^(-18 / 60)
		ui.Frame([&]()
			{
				value = Nyx::UI::Animate(id, 1.0f, Speed);
				Require(Nyx::UI::Animate(id, 1.0f, Speed) == value, "An animation should move once per frame, however often it is asked");
			});
		const float expected = 1.0f - std::exp(-Speed / 60.0f);
		Require(std::abs(value - expected) < 1e-4f, "One frame should ease " + std::to_string(expected) + " of the way, got " + std::to_string(value));

		for (int frame = 0; frame < 60; ++frame)
		{
			ui.Frame([&]() { value = Nyx::UI::Animate(id, 1.0f, Speed); });
		}
		Require(value == 1.0f, "An animation should arrive at its target");

		const size_t kept = Nyx::UI::GetAnimationCount();
		for (int frame = 0; frame <= Nyx::UI::AnimationForgetFrames + 1; ++frame)
		{
			ui.Frame([]() {});
		}
		Require(kept == 1 && Nyx::UI::GetAnimationCount() == 0, "An animation not asked for in a while should be forgotten");
	}

	// Zooming the Painter Test window bakes glyphs at new sizes, which can make ImGui's font atlas
	// grow into a new texture in the middle of a frame. The ImDrawList comparison then still held
	// glyphs on the old texture, and handing that to a draw list asserted.
	void TestPainterComparisonSurvivesAtlasGrowth()
	{
		// A font atlas that grows as glyphs are baked, as the editor's does
		ImGuiContext* context = ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
		io.IniFilename = nullptr;
		io.DisplaySize = ImVec2(1280, 800);
		io.DeltaTime = 1.0f / 60.0f;
		ImFont* font = io.Fonts->AddFontDefault();
		ImGui::NewFrame();

		Painter painter;
		painter.BeginFrame(400, 200);
		painter.AddText(font, 13.0f, { 10.0f, 10.0f }, glm::vec4(1.0f), "Before");
		const ImTextureData* oldTexture = io.Fonts->TexData;

		// Large glyphs until the atlas moves to a bigger texture
		for (float size = 100.0f; size < 400.0f && io.Fonts->TexData == oldTexture; size += 20.0f)
		{
			for (ImWchar character = 'A'; character <= 'Z'; ++character)
			{
				font->GetFontBaked(size)->FindGlyph(character);
			}
		}
		Require(io.Fonts->TexData != oldTexture, "The font atlas should have grown");

		painter.AddText(font, 13.0f, { 10.0f, 40.0f }, glm::vec4(1.0f), "After");
		painter.EndFrame();
		Require(painter.GetTextures().size() == 2, "Text before and after the growth should use the old and the new texture");

		ImGui::Begin("Comparison");
		ImDrawList& drawList = *ImGui::GetWindowDrawList();
		// The window's own title is drawn on the new texture too; only what comes after counts.
		const unsigned int indicesBefore = static_cast<unsigned int>(drawList.IdxBuffer.Size);
		DrawPainterFrameWithImDrawList(painter, drawList, ImGui::GetCursorScreenPos());
		ImGui::End();

		bool bUsesOldTexture = false;
		bool bUsesNewTexture = false;
		for (const ImDrawCmd& command : drawList.CmdBuffer)
		{
			bUsesOldTexture |= command.TexRef._TexData == oldTexture;
			bUsesNewTexture |= command.TexRef._TexData == io.Fonts->TexData && command.ElemCount > 0 && command.IdxOffset >= indicesBefore;
		}
		Require(!bUsesOldTexture, "The comparison should leave out glyphs on the replaced texture");
		Require(bUsesNewTexture, "The comparison should draw glyphs on the new texture");

		ImGui::EndFrame();
		ImGui::DestroyContext(context);
	}
}

int main()
{
	try
	{
		Nyx::Core::Logger::Get().Init();
		RegisterComponentTypes();

		TestCreateAndDeleteAreUndoable();
		TestSelectionFollowsGuid();
		TestDuplicateCopyAndPaste();
		TestForgetScene();
		TestHotkeys();
		TestPanelsDrawHeadless();
		TestCrashCards();
		TestScenePopupsAndIdleSession();
		TestPainterComparisonSurvivesAtlasGrowth();
		TestPaintedWidgetsBehaveLikeImGuis();
		TestTreeRowsBehaveLikeTreeNodes();
		TestFieldsAndPropertyRows();
		TestAnimationsEaseAndAreForgotten();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Editor panel test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All editor panel tests passed.\n";
	return 0;
}
