#pragma once

#include "Window.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct ImGuiContext;
struct ImGuiSettingsHandler;
struct ImGuiTextBuffer;

namespace Nyx::Editor
{
	// The editor's workspaces: each a saved layout of the dock space, and the panels it shows
	enum class EWorkspace : uint8_t
	{
		// The scene: outliner, scene views, details
		Scene,
		// Materials: the preview, the parameters, the Shader Log and the asset browser
		Materials,
	};

	// "Scene", "Materials": what the titlebar shows and the preferences store
	const char* GetWorkspaceName(EWorkspace workspace);
	std::optional<EWorkspace> FindWorkspace(std::string_view name);

	// The window's section of the layout files: "Pos=<x>,<y>", "Size=<width>,<height>" and
	// "Maximized=<0 or 1>", the client area as IWindow::GetPlacement() has it
	std::vector<std::string> WriteWindowPlacement(const Nyx::WindowPlacement& placement);
	// Nothing without a position and a size of at least 1 by 1; lines it doesn't know are skipped
	std::optional<Nyx::WindowPlacement> ReadWindowPlacement(const std::vector<std::string>& lines);

	// Switches between the workspaces' layouts and keeps them in their files. ImGui keeps one
	// layout: the current workspace's. Each workspace's file, in the project's Saved/Layouts, holds
	// ImGui's windows and docking and the editor's own sections, such as the window's placement;
	// it is the only place the layout is kept, there is no imgui.ini. Switching saves the current
	// layout to its file and loads the other's, which ImGui can only do between frames; a
	// workspace without a file gets its default layout, built in the next frame.
	class Workspaces
	{
	public:
		// `layoutsFolder`: where each workspace's layout file is kept (Saved/Layouts)
		explicit Workspaces(std::filesystem::path layoutsFolder);

		// A section of the editor's own in the layout files, "[Nyx][<name>]" with a line of text
		// for each of `write`'s lines, e.g. "Size=1600,900". What the file loaded last had of it is
		// GetLoadedSection()'s.
		void AddSection(std::string name, std::function<std::vector<std::string>()> write);

		// Before the first frame, in ImGui's context: takes over saving the layout from ImGui, and
		// loads the workspace's from its file, or has its default built in the first frame
		void Start(EWorkspace workspace);
		EWorkspace GetCurrent() const { return Current; }

		// The lines of the section `name` (AddSection()) in the file loaded last, e.g. by Start();
		// nullptr if it had none
		const std::vector<std::string>* GetLoadedSection(std::string_view name) const;

		// Switches at the next ApplyPending()
		void Request(EWorkspace workspace) { Requested = workspace; }

		// Between frames: saves the current layout to its file and loads the requested
		// workspace's, or, without a file, has its default built in the next frame. True when it
		// switched.
		bool ApplyPending();

		// Between frames: saves the current layout to its file a few seconds after it changed
		// (io.IniSavingRate), as ImGui saved imgui.ini, so a layout survives the editor being
		// stopped without closing. The sections count as changed when their lines are.
		void SaveIfChanged();

		// Has the current workspace's default layout built in the next frame (Window > Reset Layout)
		void RequestDefaultLayout() { bBuildDefault = true; }

		// In the frame, in the window that holds the dock space `dockSpace` (its ID), before the
		// panels are drawn: the default layout of a workspace without a file, or reset
		void BuildDefaultLayout(unsigned int dockSpace);

		// Saves the current layout to its file, e.g. as the editor closes
		void SaveCurrent();

		// As the editor closes: saves the current layout, and stops reading and writing the
		// editor's sections, whose functions may be gone after
		void Stop();

		std::filesystem::path GetLayoutFile(EWorkspace workspace) const;

	private:
		struct Section
		{
			std::string Name;
			std::function<std::vector<std::string>()> Write;
		};

		// Loads the current workspace's layout from its file; false without one
		bool LoadCurrentFile();

		// The sections' text as they would be saved now
		std::string WriteSections() const;

		// ImGui's settings handler for the sections, "Nyx"
		static void* ReadOpen(ImGuiContext*, ImGuiSettingsHandler* handler, const char* name);
		static void ReadLine(ImGuiContext*, ImGuiSettingsHandler* handler, void* entry, const char* line);
		static void ReadInit(ImGuiContext*, ImGuiSettingsHandler* handler);
		static void WriteAll(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buffer);

		std::filesystem::path LayoutsFolder;
		EWorkspace Current = EWorkspace::Scene;
		std::optional<EWorkspace> Requested;
		bool bBuildDefault = false;

		std::vector<Section> Sections;
		std::map<std::string, std::vector<std::string>, std::less<>> LoadedSections;
		// The sections as saved last, to see when they change
		std::string SavedSections;
		bool bStarted = false;
	};
}
