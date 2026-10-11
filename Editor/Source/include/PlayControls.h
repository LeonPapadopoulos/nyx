#pragma once

namespace Nyx::Editor
{
	class PlaySession;
	struct EditorPreferences;

	// Play, Stop and Play All in the titlebar, Play's right-click menu, and the "Play Setups" window
	// that edits the setups. Changes to the setups are saved to the preferences right away.
	class PlayControls
	{
	public:
		void DrawButtons(PlaySession& session, EditorPreferences& preferences, float buttonHeight);
		void DrawSetupsWindow(EditorPreferences& preferences);

		// The "Play Setups" window; the setups themselves are in the preferences
		bool bShowSetups = false;

	private:
		void DrawOptionsMenu(PlaySession& session, EditorPreferences& preferences);
	};
}
