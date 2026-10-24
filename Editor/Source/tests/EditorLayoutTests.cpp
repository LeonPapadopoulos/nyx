// The editor's layout as it is kept between runs, without a window or renderer: the workspaces'
// layout files with the docking and the editor's own sections, the window's placement in them,
// and the window kept on the monitors there are.
#include "Log.h"
#include "Window.h"
#include "Workspaces.h"
#include "HeadlessUI.h"
#include "TestSupport.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
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

	// The panels the Scene workspace's default layout docks
	const char* const ScenePanels[] = { "Scene Outliner", "Details", "Asset Browser", "Game Link", "Scene", "Scene 2" };

	// A frame as the editor draws one: the dock space in a window over the whole display, the
	// default layout built in it if asked, then the panels
	void DrawLayoutFrame(HeadlessUI& ui, Workspaces& layouts)
	{
		ui.Frame([&]()
			{
				const ImGuiViewport* viewport = ImGui::GetMainViewport();
				ImGui::SetNextWindowPos(viewport->Pos);
				ImGui::SetNextWindowSize(viewport->Size);
				ImGui::Begin("DockSpaceWindow", nullptr, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
				const ImGuiID dockSpace = ImGui::GetID("MyDockspace");
				ImGui::DockSpace(dockSpace);
				layouts.BuildDefaultLayout(dockSpace, { "Scene", "Scene 2" });
				ImGui::End();
				for (const char* panel : ScenePanels)
				{
					ImGui::Begin(panel);
					ImGui::End();
				}
			});
	}

	ImGuiWindow& FindWindow(const char* name)
	{
		ImGuiWindow* window = ImGui::FindWindowByName(name);
		Require(window != nullptr, std::string("The window ") + name + " should be there");
		return *window;
	}

	// A headless UI that docks, as the editor's does
	void EnableDocking()
	{
		ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	}

	std::string ReadFile(const std::filesystem::path& path)
	{
		std::ifstream stream(path, std::ios::binary);
		std::stringstream text;
		text << stream.rdbuf();
		return text.str();
	}

	void TestWindowPlacementText()
	{
		const WindowPlacement placement{ ScreenRect{ -1700, 40, 1600, 900 }, true };
		const std::vector<std::string> lines = WriteWindowPlacement(placement);
		Require(lines == std::vector<std::string>{ "Pos=-1700,40", "Size=1600,900", "Maximized=1" }, "The window's section should name its position, size and whether it is maximized");
		const std::optional<WindowPlacement> read = ReadWindowPlacement(lines);
		Require(read && *read == placement, "The window's section should read back as it was written");

		const std::optional<WindowPlacement> withUnknown = ReadWindowPlacement({ "Monitor=2", "Size=800,600", "Pos=10,20" });
		Require(withUnknown && withUnknown->Rect.Width == 800 && withUnknown->Rect.X == 10 && !withUnknown->bMaximized,
			"Lines the editor doesn't know should be skipped, and a placement without Maximized isn't maximized");
		Require(!ReadWindowPlacement({ "Size=800,600" }), "A placement without a position should be nothing");
		Require(!ReadWindowPlacement({ "Pos=0,0", "Size=0,600" }), "A placement without a size should be nothing");
		Require(!ReadWindowPlacement({}), "No section should be no placement");
	}

	void TestWindowKeptOnScreen()
	{
		const std::vector<ScreenRect> twoMonitors = { ScreenRect{ 0, 0, 2560, 1400 }, ScreenRect{ 2560, 0, 1920, 1040 } };
		const std::vector<ScreenRect> leftOnly = { twoMonitors.front() };

		const WindowPlacement onRight{ ScreenRect{ 2700, 100, 1600, 900 }, true };
		Require(FitWindowOnScreen(onRight, twoMonitors) == onRight, "A window on a monitor should stay where it is");

		// Half off the right edge, but its top can still be grabbed
		const WindowPlacement halfOff{ ScreenRect{ 2000, 100, 1600, 900 }, false };
		Require(FitWindowOnScreen(halfOff, leftOnly) == halfOff, "A window whose top lies on a monitor should stay where it is");

		// The right monitor unplugged: back onto the left one, keeping its size and maximized state
		const WindowPlacement moved = FitWindowOnScreen(onRight, leftOnly);
		Require(moved.Rect.X == 2560 - 1600 && moved.Rect.Y == 100 && moved.Rect.Width == 1600 && moved.Rect.Height == 900 && moved.bMaximized,
			"A window on a monitor that is gone should move onto one there is, as large as it was");

		// Its titlebar above the screen, where it can't be dragged from
		const WindowPlacement above{ ScreenRect{ 300, -500, 1280, 720 }, false };
		const WindowPlacement fitted = FitWindowOnScreen(above, twoMonitors);
		Require(fitted.Rect.X == 300 && fitted.Rect.Y == 0, "A window with its top off the screen should move down onto it");

		// Larger than the monitors there are now
		const WindowPlacement huge{ ScreenRect{ 5000, 3000, 4000, 2000 }, false };
		const WindowPlacement shrunk = FitWindowOnScreen(huge, leftOnly);
		Require(shrunk.Rect.X == 0 && shrunk.Rect.Y == 0 && shrunk.Rect.Width == 2560 && shrunk.Rect.Height == 1400,
			"A window larger than the monitor it moves to should become as large as it");

		Require(FitWindowOnScreen(huge, {}) == huge, "Without monitors the window should stay as it is");
	}

	void TestLayoutRoundTrip()
	{
		TestDirectory directory("NyxEditorLayout");
		WindowPlacement placement{ ScreenRect{ 120, 80, 1600, 900 }, true };
		float savedOutlinerWidth = 0.0f;
		ImGuiID savedOutlinerDock = 0;

		{
			HeadlessUI ui;
			EnableDocking();
			Workspaces layouts(directory.Path / "Layouts");
			layouts.AddSection("Window", [&]() { return WriteWindowPlacement(placement); });
			layouts.Start(EWorkspace::Scene);
			Require(ImGui::GetIO().IniFilename == nullptr, "The layout files should replace imgui.ini");
			Require(layouts.GetLoadedSection("Window") == nullptr, "Without a file there should be no section loaded");

			// No file yet: the default layout
			for (int frame = 0; frame < 3; ++frame)
			{
				DrawLayoutFrame(ui, layouts);
			}
			for (const char* panel : ScenePanels)
			{
				Require(FindWindow(panel).DockId != 0, std::string("Without a layout file, the default layout should dock ") + panel);
			}
			const float defaultOutlinerWidth = FindWindow("Scene Outliner").Size.x;

			// The outliner dragged wider
			ImGuiDockNode* outlinerNode = FindWindow("Scene Outliner").DockNode;
			outlinerNode->SizeRef.x = defaultOutlinerWidth + 120.0f;
			ImGui::MarkIniSettingsDirty();
			for (int frame = 0; frame < 2; ++frame)
			{
				DrawLayoutFrame(ui, layouts);
			}
			savedOutlinerWidth = FindWindow("Scene Outliner").Size.x;
			savedOutlinerDock = FindWindow("Scene Outliner").DockId;
			Require(std::abs(savedOutlinerWidth - (defaultOutlinerWidth + 120.0f)) < 2.0f, "The outliner should have become wider");

			layouts.Stop();
			Require(std::filesystem::exists(layouts.GetLayoutFile(EWorkspace::Scene)), "Closing should save the Scene workspace's layout file");
			const std::string text = ReadFile(layouts.GetLayoutFile(EWorkspace::Scene));
			Require(text.find("[Nyx][Window]\nPos=120,80\nSize=1600,900\nMaximized=1\n") != std::string::npos, "The layout file should have the window's section");
			Require(text.find("[Docking][Data]") != std::string::npos, "The layout file should have the docking");
		}

		// The next run: the same layout and window
		{
			HeadlessUI ui;
			EnableDocking();
			Workspaces layouts(directory.Path / "Layouts");
			const WindowPlacement before = placement;
			placement = WindowPlacement{};
			layouts.AddSection("Window", [&]() { return WriteWindowPlacement(placement); });
			layouts.Start(EWorkspace::Scene);
			const std::vector<std::string>* windowSection = layouts.GetLoadedSection("Window");
			Require(windowSection != nullptr, "Starting should load the window's section");
			const std::optional<WindowPlacement> loaded = ReadWindowPlacement(*windowSection);
			Require(loaded && *loaded == before, "The window's placement should come back as it was saved");
			placement = *loaded;

			for (int frame = 0; frame < 3; ++frame)
			{
				DrawLayoutFrame(ui, layouts);
			}
			Require(FindWindow("Scene Outliner").DockId == savedOutlinerDock, "The outliner should be docked where it was");
			Require(std::abs(FindWindow("Scene Outliner").Size.x - savedOutlinerWidth) < 1.0f, "The outliner should be as wide as it was");

			// Window > Reset Layout
			layouts.RequestDefaultLayout();
			for (int frame = 0; frame < 3; ++frame)
			{
				DrawLayoutFrame(ui, layouts);
			}
			Require(FindWindow("Scene Outliner").Size.x < savedOutlinerWidth - 100.0f, "Reset Layout should give the outliner its default width again");
			for (const char* panel : ScenePanels)
			{
				Require(FindWindow(panel).DockId != 0, std::string("Reset Layout should dock ") + panel);
			}
			layouts.Stop();
		}
	}

	void TestLayoutSavedWhileRunning()
	{
		TestDirectory directory("NyxEditorLayoutSaving");
		HeadlessUI ui;
		EnableDocking();
		ImGui::GetIO().IniSavingRate = 0.1f;
		WindowPlacement placement;
		Workspaces layouts(directory.Path);
		layouts.AddSection("Window", [&]() { return WriteWindowPlacement(placement); });
		layouts.Start(EWorkspace::Scene);
		const std::filesystem::path file = layouts.GetLayoutFile(EWorkspace::Scene);

		// Between frames, as the editor does
		const auto runFrames = [&](int count)
		{
			for (int frame = 0; frame < count; ++frame)
			{
				DrawLayoutFrame(ui, layouts);
				layouts.SaveIfChanged();
			}
		};
		runFrames(12);
		Require(std::filesystem::exists(file), "A new layout should be saved a moment after it was built");

		// Only the window moved: the section changed, ImGui's windows didn't
		placement.Rect.Width = 1700;
		runFrames(2);
		Require(ReadFile(file).find("Size=1700,720") == std::string::npos, "The layout shouldn't be saved at once after every change");
		runFrames(10);
		Require(ReadFile(file).find("Size=1700,720") != std::string::npos, "The window's new size should be saved a moment after it changed, without closing");

		// Nothing changed: nothing saved
		std::filesystem::remove(file);
		runFrames(12);
		Require(!std::filesystem::exists(file), "An unchanged layout shouldn't be saved again");
		layouts.Stop();
	}

	void TestWorkspaceSwitchKeepsLayouts()
	{
		TestDirectory directory("NyxEditorLayoutSwitch");
		HeadlessUI ui;
		EnableDocking();
		Workspaces layouts(directory.Path);
		layouts.Start(EWorkspace::Scene);
		for (int frame = 0; frame < 3; ++frame)
		{
			DrawLayoutFrame(ui, layouts);
		}
		ImGuiDockNode* outlinerNode = FindWindow("Scene Outliner").DockNode;
		outlinerNode->SizeRef.x += 150.0f;
		ImGui::MarkIniSettingsDirty();
		DrawLayoutFrame(ui, layouts);
		const float widened = FindWindow("Scene Outliner").Size.x;

		layouts.Request(EWorkspace::Materials);
		Require(layouts.ApplyPending() && layouts.GetCurrent() == EWorkspace::Materials, "Switching should change the workspace");
		Require(std::filesystem::exists(layouts.GetLayoutFile(EWorkspace::Scene)), "Switching should save the workspace left");
		for (int frame = 0; frame < 3; ++frame)
		{
			DrawLayoutFrame(ui, layouts);
		}

		layouts.Request(EWorkspace::Scene);
		Require(layouts.ApplyPending(), "Switching back should change the workspace");
		for (int frame = 0; frame < 3; ++frame)
		{
			DrawLayoutFrame(ui, layouts);
		}
		Require(std::abs(FindWindow("Scene Outliner").Size.x - widened) < 1.0f, "Switching back should bring the workspace's layout back");
		layouts.Stop();
	}
}

int main()
{
	try
	{
		Nyx::Logger::Get().Init();

		TestWindowPlacementText();
		TestWindowKeptOnScreen();
		TestLayoutRoundTrip();
		TestLayoutSavedWhileRunning();
		TestWorkspaceSwitchKeepsLayouts();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Editor layout test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All editor layout tests passed.\n";
	return 0;
}
