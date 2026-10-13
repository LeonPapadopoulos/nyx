#pragma once

#include "UITheme.h"
#include "UIThemeFile.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Nyx::Editor
{
	// The "Theme" window: every value of the painted widgets' theme (Nyx::UI::GetTheme()), grouped
	// and searchable, edited live: the whole editor follows each change at once. Colours take a
	// picker, a hex field (#RRGGBB or #RRGGBBAA, as design tools give them) and their alpha; sizes
	// a drag within their range. Save writes the theme file (GetThemeFile()), Revert reads it
	// again, and Reset to Defaults goes back to the values in UITheme.h until saved.
	//
	// The theme file is the editor's theme: it is read at startup, and read again whenever it
	// changes on disk, e.g. when edited in a text editor (UIThemeFile.h has its format).
	class ThemeEditorWindow
	{
	public:
		// Reads the theme file into the theme; without one, writes the defaults to it, so there is
		// a file to edit. Once, at startup.
		void LoadAtStartup();

		// The window while open, and every frame, reading the file again when it changed
		void Draw();

		bool bOpen = false;

		// Editor/Themes/Dark.nyxtheme in the project
		static std::filesystem::path GetThemeFile();

	private:
		// The file into the theme, from the defaults up, so values it leaves out are the defaults
		void ReadFile(const char* why);
		void Save();
		void ReloadWhenChanged();

		// Draws one value's row(s); true when it changed
		bool DrawValue(const Nyx::UI::ThemeValue& value);

		// What the file holds, to tell whether the theme has unsaved changes
		Nyx::UI::Theme Saved;
		std::filesystem::file_time_type FileTime{};
		double NextFileCheck = 0.0;

		std::string Filter;
		std::string Status;
		std::vector<std::string> Problems;
	};
}
