#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace Nyx::Editor
{
	// Windows' own Open and Save As dialogs. Each blocks until it closes, and is empty if it was
	// cancelled or couldn't open.

	// For image files (PNG, JPEG, BMP, TGA)
	std::optional<std::filesystem::path> ChooseImageFile();

	// For a theme file (*.nyxtheme), starting in `folder`
	std::optional<std::filesystem::path> ChooseThemeFileToOpen(const std::filesystem::path& folder);

	// Where to save a theme file, starting in `folder` with `name` filled in; ".nyxtheme" is added
	// when the name has no extension, and choosing a file that exists asks first
	std::optional<std::filesystem::path> ChooseThemeFileToSave(const std::filesystem::path& folder, const std::wstring& name);

	// For an input recording (*.nyxinputrec), starting in `folder` if it is there
	std::optional<std::filesystem::path> ChooseInputRecording(const std::filesystem::path& folder);

	// Shows `path` in Windows' File Explorer: a file selected in its folder, a folder opened. False
	// if there is nothing at `path` or Explorer couldn't show it.
	bool ShowInFileExplorer(const std::filesystem::path& path);
}
