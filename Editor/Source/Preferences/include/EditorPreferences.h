#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Nyx::Editor
{
	enum class ESourceEditor
	{
		VisualStudio,
		VisualStudioCode
	};

	// A window shape to play in, e.g. "Steam Deck" at 1280x800. Play uses the first setup; Play
	// All starts a game for each setup marked bInPlayAll, side by side.
	struct PlaySetup
	{
		std::string Name;

		// The size of the game's picture, without titlebar and borders
		uint32_t Width = 1280;
		uint32_t Height = 720;

		bool bInPlayAll = true;

		bool operator==(const PlaySetup&) const = default;
	};

	// The setups a new user starts with
	std::vector<PlaySetup> GetDefaultPlaySetups();

	// Saved per user, in a small text file (see GetUserFile()), one setting per line
	struct EditorPreferences
	{
		ESourceEditor SourceEditor = ESourceEditor::VisualStudio;

		// Never empty: without any saved, the defaults
		std::vector<PlaySetup> PlaySetups = GetDefaultPlaySetups();

		// Whether the games of Play All get console windows. Their log reaches the Game Link window
		// anyway, and a wall of games with a console each is crowded. Play always shows the console.
		bool bConsoleWindowsInPlayAll = false;

		// Whether the gamepads drive every game started with Play, also those whose window doesn't
		// have the focus (--input-background), e.g. to play all games of Play All with one pad
		bool bGamepadsDriveAllGames = false;

		// Ambilight: the scene views' pictures glow onto the editor around them. Strength is the
		// glow's opacity at the picture's edge, from 0 to 1; reach is how far it goes, in pixels,
		// up to MaxAmbilightReach.
		bool bAmbilight = true;
		float AmbilightStrength = 0.8f;
		float AmbilightReach = 60.0f;
		static constexpr float MaxAmbilightReach = 400.0f;

		// The editor's frame rate. Vsync shows each frame at the monitor's next refresh, so the
		// editor draws no more frames than the monitor shows. The cap is at most that many frames
		// per second, 0 for none; FrameRateCaps lists the ones to choose from.
		bool bVSync = true;
		uint32_t FrameRateCap = 0;
		static constexpr uint32_t FrameRateCaps[] = { 30, 60, 90, 120, 144, 240 };

		// The theme file the editor loads (ThemeEditorWindow): a file name in Editor/Themes, e.g.
		// "Dark.nyxtheme", or a whole path to one elsewhere. Empty for the default theme.
		std::string Theme;

		// The workspace the editor was in last ("Scene", "Materials"), whose layout imgui.ini holds
		std::string Workspace;

		static std::filesystem::path GetUserFile();

		// Missing settings keep their defaults; lines this editor doesn't know are skipped
		bool Load(const std::filesystem::path& path);
		bool Save(const std::filesystem::path& path) const;

		// The text Save() writes, and the text Load() reads
		std::string ToText() const;
		void FromText(const std::string& text);
	};
}
