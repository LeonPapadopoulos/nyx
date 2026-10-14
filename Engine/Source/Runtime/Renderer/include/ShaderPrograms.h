#pragma once

#include "FileWatcher.h"
#include "ShaderCompileService.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Nyx::Engine
{
	// What finished compiling for a program, for the owner of its pipelines to act on and log
	struct ShaderProgramUpdate
	{
		std::string Name;
		// The compile, with its diagnostics and time; on failure the program keeps its last good code
		ShaderCompileResult Result;
		// The program's SPIR-V was replaced: pipelines made from it must be made again
		bool bChanged = false;
	};

	// The SPIR-V of the engine's shaders, by name ("Grid.frag"), with live reload
	// (Design/shader-materials.md, stage 1, change 3). Until change 4, a program starts from the
	// .spv the build compiled; once live reload is on, saving its source compiles it again, 50 ms
	// after the last change, and the owner of its pipelines rebuilds them. A program that fails to
	// compile keeps its last good code. Nothing blocks; Update() runs once per frame.
	class ShaderPrograms
	{
	public:
		using Clock = std::chrono::steady_clock;

		// How long after a file's last change it is compiled: saves often come as several writes
		static constexpr std::chrono::milliseconds SettleTime{ 50 };

		// `compiledFolder`: where the build put the .spv files; `sourceFolder`: their sources
		ShaderPrograms(std::filesystem::path compiledFolder, std::filesystem::path sourceFolder);

		// The program's last good SPIR-V; at first what the build compiled (compiledFolder /
		// "<name>.spv"). Throws when there is none.
		const std::vector<uint32_t>& GetSpirv(const std::string& name);

		// Starts watching the source folder, and `moreFolders` (e.g. the shader library), and
		// compiles changed programs with `service`. False when the folders can't be watched, with
		// the reason in GetProblem().
		bool EnableLiveReload(std::unique_ptr<IShaderCompileService> service, const std::vector<std::filesystem::path>& moreFolders = {});

		bool IsLiveReloadEnabled() const
		{
			return Service != nullptr;
		}

		const std::string& GetProblem() const
		{
			return Problem;
		}

		// Takes file changes, sends programs that changed to the service once they settled, and
		// returns what finished compiling since the last call. Only programs that were asked for
		// (GetSpirv()) are compiled.
		std::vector<ShaderProgramUpdate> Update(Clock::time_point now = Clock::now());

		// Whether a compile is waiting or under way, e.g. for tests that wait for one
		bool IsBusy() const;

	private:
		struct Program
		{
			std::vector<uint32_t> Spirv;
			// Files whose change compiles it again: its source, and what the last compile used
			std::vector<std::filesystem::path> Dependencies;
			bool bDirty = false;
			uint32_t RequestId = 0; // 0 when no compile is under way
		};

		std::filesystem::path CompiledFolder;
		std::filesystem::path SourceFolder;
		std::map<std::string, Program> Programs;

		FileWatcher Watcher;
		std::unique_ptr<IShaderCompileService> Service;
		Clock::time_point LastChange{};
		uint32_t NextRequestId = 1;
		std::string Problem;
	};
}
