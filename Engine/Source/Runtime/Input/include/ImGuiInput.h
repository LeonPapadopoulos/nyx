#pragma once

// Where the input system and ImGui meet: ImGui reads GLFW's events through its own callbacks and
// the input system doesn't feed it. Once per frame, the application reads what ImGui wants
// (ReadImGuiState()) and the input system puts the ImGui context on the stack accordingly.
namespace Nyx::Input
{
	struct ImGuiInputState
	{
		// A text field is active: keys are text
		bool bTypingText = false;

		// The mouse is over ImGui UI that isn't a view (MarkAsView())
		bool bMouseOverUI = false;
	};

	// Call inside an ImGui window that shows a scene, e.g. a scene view or the game's window, every
	// frame: ImGui hovering it doesn't take the mouse from the game or the fly camera.
	void MarkAsView();

	// What ImGui wants after the frame's UI was built; forgets the views marked in it. Needs an
	// ImGui context.
	ImGuiInputState ReadImGuiState();
}
