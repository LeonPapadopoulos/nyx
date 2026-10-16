#pragma once

#include <filesystem>
#include <string>

namespace Nyx
{
	// Prints a recording EditorLinkRecorder made as text. A recording that ends in the middle of a
	// message, e.g. after a crash, is printed up to there. Returns false, with the reason at the
	// end of outText, if the file isn't a recording.
	bool PrintEditorLinkRecording(const std::filesystem::path& path, std::string& outText);
}
