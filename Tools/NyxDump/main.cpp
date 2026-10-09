#include "ComponentRegistration.h"
#include "Log.h"
#include "SceneSerializer.h"

#include <filesystem>
#include <iostream>
#include <string>

// NyxDump: prints Nyx data files as readable text, to see exactly what was saved.
// Usage: NyxDump <file.nyxscene>
int main(int argc, char** argv)
{
	if (argc != 2)
	{
		std::cerr << "Usage: NyxDump <file.nyxscene>\n";
		return 1;
	}

	// The engine logs through the logger, and the registered component types
	// let the printout show property names instead of name hashes.
	Nyx::Core::Logger::Get().Init();
	Nyx::Engine::RegisterComponentTypes();

	std::string text;
	const bool bPrinted = Nyx::Engine::SceneSerializer::PrintFile(std::filesystem::path(argv[1]), text);

	// On failure, the text ends with the reason
	(bPrinted ? std::cout : std::cerr) << text;
	return bPrinted ? 0 : 1;
}
