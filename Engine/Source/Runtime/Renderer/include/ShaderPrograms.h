#pragma once

#include "FileWatcher.h"
#include "ShaderCache.h"
#include "ShaderCompileService.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Nyx::Engine
{
	// What finished compiling for a program, for the owner of its pipelines to act on and log
	struct ShaderProgramUpdate
	{
		// The program's name, or GetResultKey() of a material's request
		std::string Name;
		ShaderCompileRequest Request;
		// The compile, with its diagnostics and time; on failure the program keeps its last good code
		ShaderCompileResult Result;
		// The program's SPIR-V was replaced: pipelines made from it must be made again
		bool bChanged = false;
	};

	// The SPIR-V of the engine's shaders, by name ("Grid.frag", "Skybox.slang"), with live reload
	// (Design/shader-materials.md, stage 1, changes 3 and 4). A program starts from the newest
	// compiled code that matches its files: what live reload compiled last (the saved cache), or
	// what the build compiled. Code that no longer matches is used all the same, and with live
	// reload on it is compiled again at once. Saving a source compiles it again, 50 ms after the
	// last change, and the owner of its pipelines rebuilds them; what compiled is kept in the saved
	// cache. A program that fails to compile keeps its last good code. Nothing blocks; Update()
	// runs once per frame. Material shaders (change 6) are compiled the same way (GetResult()).
	class ShaderPrograms
	{
	public:
		using Clock = std::chrono::steady_clock;

		// How long after a file's last change it is compiled: saves often come as several writes
		static constexpr std::chrono::milliseconds SettleTime{ 50 };

		// `builtFolder`: where the build put its cache entries (NyxShaderCompiler build);
		// `sourceFolder`: the sources; `savedFolder`: where live reload keeps what it compiled
		// (empty: nowhere)
		ShaderPrograms(std::filesystem::path builtFolder, std::filesystem::path sourceFolder, std::filesystem::path savedFolder = {});

		// The program's last good SPIR-V. Throws when neither cache has any for it, or when the
		// program has more than one entry point.
		const std::vector<uint32_t>& GetSpirv(const std::string& name);
		// One entry point's of a program with several (a .slang file). Throws when there is none
		// by that name.
		const std::vector<uint32_t>& GetSpirv(const std::string& name, const std::string& entryPoint);

		// A material shader's compile or its defaults writer (EShaderCompileKind::Material or
		// DefaultsWriter; the file may lie anywhere): the newest good result that matches its
		// files, else the newest good one at all, else null. Unlike a program, one without any
		// doesn't throw: with live reload on it is compiled at the next update, and an update
		// named GetResultKey(request) brings it.
		const ShaderCompileResult* GetResult(const ShaderCompileRequest& request);
		static std::string GetResultKey(const ShaderCompileRequest& request);
		// Whether the request's compile waits to go out or is under way
		bool IsPending(const ShaderCompileRequest& request) const;

		// Whether the program started from code that no longer matches its files, e.g. a shader
		// changed while nothing was running. With live reload on, it is compiled at once.
		bool StartedOutOfDate(const std::string& name) const;

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
			ShaderCompileRequest Request;
			// The last good compile; its entry points in the file's order. A compile that drops
			// one of them doesn't replace it: pipelines are made from them by name.
			ShaderCompileResult Code;
			// Files whose change compiles it again: its source, and what the last compile used
			std::vector<std::filesystem::path> Dependencies;
			bool bDirty = false;
			bool bStartedOutOfDate = false;
			uint32_t RequestId = 0; // 0 when no compile is under way
		};

		ShaderCompileRequest RequestFor(const std::string& name) const;
		Program& Load(const std::string& name);
		// The program for `request`, from the caches; nullopt when they have none
		std::optional<Program> FromCaches(const ShaderCompileRequest& request) const;

		std::filesystem::path SourceFolder;
		ShaderCache Built;
		std::optional<ShaderCache> Saved;
		std::map<std::string, Program> Programs;

		FileWatcher Watcher;
		std::unique_ptr<IShaderCompileService> Service;
		Clock::time_point LastChange{};
		uint32_t NextRequestId = 1;
		std::string Problem;
	};
}
