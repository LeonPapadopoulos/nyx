#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace Nyx::Engine
{
	class ShaderLog;
	struct ShaderDiagnostic;
}

namespace Nyx::Editor
{
	// Opens a file at a line in the source editor the preferences name
	using OpenFileAtLine = std::function<void(const std::filesystem::path& file, uint32_t line)>;

	// Draws a diagnostic as one line ("Stone.slang(23,14): error 30015: undefined identifier"),
	// red for an error, which opens the file at its line when clicked. True when clicked.
	bool DiagnosticLine(const Nyx::Engine::ShaderDiagnostic& diagnostic, const OpenFileAtLine& open);

	// The "Shader Log" window (Design/shader-materials.md, stage 2, change 4): the last compile of
	// every shader since the start, the engine's and the materials', newest first. Each says
	// whether it compiled, when, how long it took, its entry points and the size of their SPIR-V,
	// or that the shader keeps its last good code; below it, what the compiler said, each line
	// opening its file at the line when clicked.
	class ShaderLogPanel
	{
	public:
		void SetOpenFileAtLine(OpenFileAtLine open) { Open = std::move(open); }

		void Draw(const Nyx::Engine::ShaderLog& log);

		bool bOpen = false;
		// Only the shaders that don't compile
		bool bFailingOnly = false;

	private:
		OpenFileAtLine Open;
	};
}
