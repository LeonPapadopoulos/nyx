#pragma once

#include <filesystem>
#include <string>

namespace Nyx
{
	// Prints an input asset or a player's input settings (.nyxinput) as text, for NyxDump. Returns
	// false, with the reason at the end of outText, if the file can't be read.
	bool PrintInputFile(const std::filesystem::path& path, std::string& outText);
}
