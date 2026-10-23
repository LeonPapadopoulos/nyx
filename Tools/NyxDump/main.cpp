#include "ComponentRegistration.h"
#include "EditorLinkRecordingPrinter.h"
#include "InputFilePrinter.h"
#include "Log.h"
#include "ScenePrinter.h"

#include <filesystem>
#include <iostream>
#include <string>

// NyxDump: prints Nyx data files as readable text, to see exactly what was saved.
// Usage: NyxDump <file.nyxscene | file.nyxlinklog | file.nyxinput | file.nyxinputrec>
int main(int argc, char** argv)
{
	if (argc != 2)
	{
		std::cerr << "Usage: NyxDump <file.nyxscene | file.nyxlinklog | file.nyxinput | file.nyxinputrec>\n";
		return 1;
	}

	// The engine logs through the logger, and the registered component types
	// let the printout show property names instead of name hashes.
	Nyx::Logger::Get().Init();
	Nyx::RegisterComponentTypes();

	// Recordings of the editor link (.nyxlinklog), input assets and player input settings
	// (.nyxinput), input recordings (.nyxinputrec) or scene files
	const std::filesystem::path path(argv[1]);
	std::string text;
	bool bPrinted = false;
	if (path.extension() == ".nyxlinklog")
	{
		bPrinted = Nyx::PrintEditorLinkRecording(path, text);
	}
	else if (path.extension() == ".nyxinput")
	{
		bPrinted = Nyx::PrintInputFile(path, text);
	}
	else if (path.extension() == ".nyxinputrec")
	{
		bPrinted = Nyx::PrintInputRecording(path, text);
	}
	else
	{
		bPrinted = Nyx::PrintSceneFile(path, text);
	}

	// On failure, the text ends with the reason
	(bPrinted ? std::cout : std::cerr) << text;
	return bPrinted ? 0 : 1;
}
