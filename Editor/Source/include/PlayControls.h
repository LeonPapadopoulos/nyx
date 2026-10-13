#pragma once

#include <glm/glm.hpp>

namespace Nyx::Editor
{
	class PlaySession;
	struct EditorPreferences;

	// Play (Stop while games run), Play All, Pause and Step in the toolbar, Play's right-click
	// menu, and the "Play Setups" window that edits the setups. Pause and Step are disabled until
	// games can pause. Changes to the setups are saved to the preferences right away.
	class PlayControls
	{
	public:
		// The buttons side by side, each `buttonSize`, `spacing` apart
		void DrawButtons(PlaySession& session, EditorPreferences& preferences, glm::vec2 buttonSize, float spacing);

		// The width DrawButtons() takes
		static float GetButtonsWidth(glm::vec2 buttonSize, float spacing);

		void DrawSetupsWindow(EditorPreferences& preferences);

		// The "Play Setups" window; the setups themselves are in the preferences
		bool bShowSetups = false;

	private:
		void DrawOptionsMenu(PlaySession& session, EditorPreferences& preferences);
	};
}
