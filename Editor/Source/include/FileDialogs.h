#pragma once

#include <filesystem>
#include <optional>

namespace Nyx::Editor
{
	// Windows' own Open dialog, for image files (PNG, JPEG, BMP, TGA). Blocks until it closes;
	// empty if it was cancelled or couldn't open.
	std::optional<std::filesystem::path> ChooseImageFile();
}
