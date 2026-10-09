#include "ComponentRegistration.h"
#include "EditorLinkRecorder.h"
#include "Log.h"
#include "SceneSerializer.h"

#include <filesystem>
#include <iostream>
#include <string>

// NyxDump: prints Nyx data files as readable text, to see exactly what was saved.
// Usage: NyxDump <file.nyxscene | file.nyxlinklog>
int main(int argc, char** argv)
{
	if (argc != 2)
	{
		std::cerr << "Usage: NyxDump <file.nyxscene | file.nyxlinklog>\n";
		return 1;
	}

	// The engine logs through the logger, and the registered component types
	// let the printout show property names instead of name hashes.
	Nyx::Core::Logger::Get().Init();
	Nyx::Engine::RegisterComponentTypes();

	// Recordings of the editor link (.nyxlinklog) or scene files
	const std::filesystem::path path(argv[1]);
	std::string text;
	const bool bPrinted = path.extension() == ".nyxlinklog" ? Nyx::Engine::EditorLinkRecorder::PrintFile(path, text)
															: Nyx::Engine::SceneSerializer::PrintFile(path, text);

	// On failure, the text ends with the reason
	(bPrinted ? std::cout : std::cerr) << text;
	return bPrinted ? 0 : 1;
}
