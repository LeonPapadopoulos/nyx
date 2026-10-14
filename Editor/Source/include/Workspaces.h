#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

namespace Nyx::Editor
{
	// The editor's workspaces (architecture 3.1, Design/shader-materials.md stage 2): each a saved
	// layout of the dock space, and the panels it shows
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

	// Switches between the workspaces' layouts. ImGui keeps one layout, saved to imgui.ini: the
	// current workspace's. Switching saves it to the workspace's own file and loads the other's
	// from its file, which ImGui can only do between frames; a workspace without a file gets its
	// default layout, built in the next frame.
	class Workspaces
	{
	public:
		// `layoutsFolder`: where each workspace's layout file is kept (Saved/Layouts)
		explicit Workspaces(std::filesystem::path layoutsFolder);

		// The workspace the editor starts in: the layout ImGui loaded from imgui.ini is its own
		void Start(EWorkspace workspace) { Current = workspace; }
		EWorkspace GetCurrent() const { return Current; }

		// Switches at the next ApplyPending()
		void Request(EWorkspace workspace) { Requested = workspace; }

		// Between frames: saves the current layout to its file and loads the requested
		// workspace's, or, without a file, has its default built in the next frame. True when it
		// switched.
		bool ApplyPending();

		// In the frame, in the window that holds the dock space `dockSpace` (its ID), before the
		// panels are drawn: the default layout of a workspace switched to without a file
		void BuildDefaultLayout(unsigned int dockSpace);

		// Saves the current layout to its file, e.g. as the editor closes
		void SaveCurrent() const;

		std::filesystem::path GetLayoutFile(EWorkspace workspace) const;

	private:
		std::filesystem::path LayoutsFolder;
		EWorkspace Current = EWorkspace::Scene;
		std::optional<EWorkspace> Requested;
		bool bBuildDefault = false;
	};
}
