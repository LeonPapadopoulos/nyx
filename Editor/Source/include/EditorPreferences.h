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

		static std::filesystem::path GetUserFile();

		// Missing settings keep their defaults; lines this editor doesn't know are skipped
		bool Load(const std::filesystem::path& path);
		bool Save(const std::filesystem::path& path) const;

		// The text Save() writes, and the text Load() reads
		std::string ToText() const;
		void FromText(const std::string& text);
	};
}
