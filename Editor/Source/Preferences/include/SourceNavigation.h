#pragma once

#include "EditorPreferences.h"
#include "SourceLocation.h"
#include <string>

namespace Nyx::Editor
{
	// Empty result means success. Runs on a worker so starting an IDE cannot stall a frame.
	std::string OpenSourceLocation(SourceLocation location, ESourceEditor editor);
	std::wstring MakeVSCodeSourceUri(const std::filesystem::path& file, uint32_t line);
}
