#pragma once

#include "UITheme.h"
#include "UIThemeFile.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Nyx::Editor
{
	struct EditorPreferences;

	// The "Theme" window: every value of the painted widgets' theme (Nyx::UI::GetTheme()), grouped
	// and searchable, edited live: the whole editor follows each change at once. Colours take a
	// picker, a hex field (#RRGGBB or #RRGGBBAA, as design tools give them) and their alpha; sizes
	// a drag within their range.
	//
	// The theme is a file (UIThemeFile.h has its format). Themes live in Editor/Themes, and the
	// window's combo lists them; Open loads one from anywhere, Save writes the one loaded, Save As
	// writes the theme to a new file and loads that. The theme loaded is remembered in the
	// preferences and loaded at startup. Revert reads the file again; Reset to Defaults goes back
	// to the values in UITheme.h until saved. Loading another theme asks first while there are
	// unsaved changes. The file loaded is read again whenever it changes on disk, e.g. when edited
	// in a text editor.
	//
	// It sets EditorPreferences::Theme to the theme loaded; the caller saves the preferences when
	// that changed.
	class ThemeEditorWindow
	{
	public:
		// `themesFolder`: where the themes are, the project's Editor/Themes unless a test says
		explicit ThemeEditorWindow(std::filesystem::path themesFolder = GetProjectThemesFolder());

		// Loads the theme the preferences name, or the default one. Without a default theme file,
		// writes the defaults to it, so there is a file to edit. Once, at startup.
		void LoadAtStartup(const EditorPreferences& preferences);

		// The window while open, and every frame, reading the file again when it changed
		void Draw(EditorPreferences& preferences);

		// Loads `file`, as choosing it in the window does: at once, or, while there are unsaved
		// changes, after asking in the window (the next Draw()), which must be open
		void ChooseTheme(const std::filesystem::path& file);

		bool bOpen = false;

		// The theme file loaded
		const std::filesystem::path& GetActiveFile() const { return ActiveFile; }

		// Whether a theme chosen waits, while the window asks whether to discard unsaved changes
		// (Enter loads it, Escape cancels)
		bool IsAskingToDiscard() const { return !PendingLoad.empty(); }

		// Editor/Themes in the project
		static std::filesystem::path GetProjectThemesFolder();

		// The default theme in a themes folder, Dark.nyxtheme
		static std::filesystem::path GetDefaultThemeFile(const std::filesystem::path& themesFolder);

		// The file a theme preference names (EditorPreferences::Theme): a file in the themes
		// folder by name, a file elsewhere by path, the default theme when empty
		static std::filesystem::path ResolveThemeFile(const std::string& preference, const std::filesystem::path& themesFolder);

		// The preference for a theme file: empty for the default theme, its name when it lies in
		// the themes folder, else its whole path
		static std::string GetThemePreference(const std::filesystem::path& file, const std::filesystem::path& themesFolder);

		// The theme files in a themes folder, sorted by name
		static std::vector<std::filesystem::path> FindThemeFiles(const std::filesystem::path& themesFolder);

	private:
		// The active file into the theme, from the defaults up, so values it leaves out are the
		// defaults
		void ReadFile(const char* why);
		// Makes `file` the theme: loads it, watches it, and remembers it in the preferences
		void Load(const std::filesystem::path& file, EditorPreferences& preferences);
		void Save();
		void SaveAs(EditorPreferences& preferences);
		void ReloadWhenChanged();

		// The combo of themes, Open and Save As
		void DrawThemeChoice(EditorPreferences& preferences);
		// "Discard unsaved changes?" before loading another theme
		void DrawDiscardPopup(EditorPreferences& preferences);
		// Draws one value's row(s); true when it changed
		bool DrawValue(const Nyx::UI::ThemeValue& value);

		std::filesystem::path ThemesFolder;
		std::filesystem::path ActiveFile;
		// What the file holds, to tell whether the theme has unsaved changes
		Nyx::UI::Theme Saved;
		std::filesystem::file_time_type FileTime{};
		double NextFileCheck = 0.0;

		// The theme chosen (ChooseTheme()), loaded in the next Draw(), which asks first while there
		// are unsaved changes. The question opens there, in the window's own ID scope: choosing
		// happens in the combo's popup, whose IDs differ.
		std::filesystem::path PendingLoad;
		bool bAskToDiscard = false;

		std::string Filter;
		std::string Status;
		std::vector<std::string> Problems;
	};
}
