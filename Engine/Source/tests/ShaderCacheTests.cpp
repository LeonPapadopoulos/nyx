// ShaderCache on real files: an entry is found while every file the compile read keeps its
// content, and is out of date once one changes (its time alone doesn't count); another compiler
// version, a damaged entry and a failed compile are misses; a copy of the folder finds the same
// entries. NyxShaderCompiler's build mode fills a cache, compiling only what changed.
#include "ChildProcess.h"
#include "Paths.h"
#include "ShaderCache.h"
#include "ShaderCompiler.h"
#include "TestSupport.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
	using namespace Nyx;
	using Clock = std::chrono::steady_clock;

	using Nyx::Tests::Require;

	void WriteFile(const std::filesystem::path& file, const std::string& text)
	{
		std::ofstream stream(file, std::ios::binary | std::ios::trunc);
		stream << text;
	}

	const std::string CommonSource = "vec3 Tint()\n{\n    return vec3(0.25);\n}\n";
	const std::string LiveSource = "#version 450\n#include \"Common.glsl\"\nlayout(location = 0) out vec4 outColor;\nvoid main()\n{\n    outColor = vec4(Tint(), 1.0);\n}\n";

	struct TestFolder
	{
		Nyx::Tests::TestDirectory Directory{ "NyxShaderCacheTests" };
		std::filesystem::path Root = Directory.Path;
		std::filesystem::path Sources = Root / "Shaders";
		std::filesystem::path Cache = Root / "Cache";

		TestFolder()
		{
			std::filesystem::create_directories(Sources);
			WriteFile(Sources / "Common.glsl", CommonSource);
			WriteFile(Sources / "Live.frag", LiveSource);
		}
	};

	ShaderCompileRequest ProgramRequest(const std::filesystem::path& file)
	{
		ShaderCompileRequest request;
		request.Kind = EShaderCompileKind::Program;
		request.File = file;
		return request;
	}

	ShaderCompileResult Compile(const std::filesystem::path& file)
	{
		ShaderCompiler compiler({ Nyx::Paths::GetShaderLibraryDir() });
		ShaderCompileResult result = compiler.CompileProgram(file);
		Require(result.bSucceeded, file.filename().string() + " doesn't compile");
		return result;
	}

	bool HasFile(const std::vector<std::filesystem::path>& files, const std::filesystem::path& file)
	{
		const std::filesystem::path wanted = std::filesystem::absolute(file).lexically_normal();
		for (const std::filesystem::path& candidate : files)
		{
			if (std::filesystem::absolute(candidate).lexically_normal() == wanted)
			{
				return true;
			}
		}
		return false;
	}

	void TestEntriesFollowTheirFiles()
	{
		TestFolder folder;
		const ShaderCache cache(folder.Cache, folder.Sources);
		const ShaderCompileRequest request = ProgramRequest(folder.Sources / "Live.frag");
		Require(!cache.Find(request), "an entry before anything was stored");

		const ShaderCompileResult result = Compile(request.File);
		Require(cache.Store(request, result), "the entry can't be stored");
		std::optional<ShaderCache::Entry> entry = cache.Find(request);
		Require(entry && entry->bUpToDate, "the stored entry isn't found up to date");
		Require(entry->Result.EntryPoints.size() == 1 && entry->Result.EntryPoints[0].Spirv == result.EntryPoints[0].Spirv &&
				entry->Result.EntryPoints[0].Name == "main",
			"the entry's code differs from what was stored");
		Require(HasFile(entry->Result.Dependencies, folder.Sources / "Live.frag") && HasFile(entry->Result.Dependencies, folder.Sources / "Common.glsl"),
			"the entry doesn't list the files the compile read");

		// The include changes: out of date, but still there
		WriteFile(folder.Sources / "Common.glsl", "vec3 Tint()\n{\n    return vec3(0.5);\n}\n");
		entry = cache.Find(request);
		Require(entry && !entry->bUpToDate, "a changed include doesn't make the entry out of date");

		// The same content again, written later: up to date again, the time doesn't count
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
		WriteFile(folder.Sources / "Common.glsl", CommonSource);
		entry = cache.Find(request);
		Require(entry && entry->bUpToDate, "a file written again with the same content makes the entry out of date");

		// Another request for the same file is another entry
		ShaderCompileRequest material = request;
		material.Kind = EShaderCompileKind::Material;
		material.Domain = ShaderDomain::Surface();
		Require(cache.GetEntryPath(material) != cache.GetEntryPath(request) && !cache.Find(material), "a material request finds the program's entry");
	}

	void TestMisses()
	{
		TestFolder folder;
		const ShaderCache cache(folder.Cache, folder.Sources);
		const ShaderCompileRequest request = ProgramRequest(folder.Sources / "Live.frag");
		Require(cache.Store(request, Compile(request.File)), "the entry can't be stored");

		Require(!ShaderCache(folder.Cache, folder.Sources, "another compiler").Find(request), "another compiler version finds the entry");

		ShaderCompileResult failed;
		Require(!cache.Store(ProgramRequest(folder.Sources / "Broken.frag"), failed), "a failed compile is stored");
		Require(!std::filesystem::exists(cache.GetEntryPath(ProgramRequest(folder.Sources / "Broken.frag"))), "a failed compile left a file");

		WriteFile(cache.GetEntryPath(request), "not an entry");
		Require(!cache.Find(request), "a damaged entry is found");
	}

	// The build compiles in one folder; the same files elsewhere find its entries
	void TestCopiesFindTheSameEntries()
	{
		TestFolder folder;
		const ShaderCompileRequest request = ProgramRequest(folder.Sources / "Live.frag");
		Require(ShaderCache(folder.Cache, folder.Sources).Store(request, Compile(request.File)), "the entry can't be stored");

		const std::filesystem::path copy = folder.Root / "Copy";
		std::filesystem::copy(folder.Sources, copy);
		const std::optional<ShaderCache::Entry> entry = ShaderCache(folder.Cache, copy).Find(ProgramRequest(copy / "Live.frag"));
		Require(entry && entry->bUpToDate, "a copy of the folder doesn't find the entry up to date");
		Require(HasFile(entry->Result.Dependencies, copy / "Common.glsl"), "the copy's entry doesn't point at the copy's files");
	}

	int RunBuild(const std::filesystem::path& source, const std::filesystem::path& output)
	{
		Nyx::ChildProcess process;
		const std::filesystem::path compiler = Nyx::Paths::GetExecutableDir() / "NyxShaderCompiler.exe";
		Require(process.Start(compiler, { "build", "--source", source.string(), "--output", output.string(), "--search-path", Nyx::Paths::GetShaderLibraryDir().string() }, false),
			"NyxShaderCompiler.exe doesn't start");
		Require(process.WaitForExit(60000), "NyxShaderCompiler build didn't finish within a minute");
		return static_cast<int>(process.GetExitCode().value_or(99));
	}

	void TestTheBuildCompilesWhatChanged()
	{
		TestFolder folder;
		WriteFile(folder.Sources / "Broken.frag", "#version 450\nlayout(location = 0) out vec4 outColor;\nvoid main() { outColor = colr; }\n");
		const ShaderCache cache(folder.Cache, folder.Sources);
		const ShaderCompileRequest live = ProgramRequest(folder.Sources / "Live.frag");
		const ShaderCompileRequest broken = ProgramRequest(folder.Sources / "Broken.frag");

		Require(RunBuild(folder.Sources, folder.Cache) == 1, "a build with a broken shader didn't fail");
		Require(cache.Find(live) && !cache.Find(broken), "the build didn't keep the good shader, or kept the broken one");
		const auto liveTime = std::filesystem::last_write_time(cache.GetEntryPath(live));

		WriteFile(folder.Sources / "Broken.frag", "#version 450\nlayout(location = 0) out vec4 outColor;\nvoid main() { outColor = vec4(1.0); }\n");
		Require(RunBuild(folder.Sources, folder.Cache) == 0, "the build failed with every shader fixed");
		Require(cache.Find(broken) && cache.Find(broken)->bUpToDate, "the fixed shader isn't in the cache");
		Require(std::filesystem::last_write_time(cache.GetEntryPath(live)) == liveTime, "the build compiled a shader that hadn't changed");

		// An include changes: what uses it is compiled again
		WriteFile(folder.Sources / "Common.glsl", "vec3 Tint()\n{\n    return vec3(0.75);\n}\n");
		Require(RunBuild(folder.Sources, folder.Cache) == 0, "the build failed after an include changed");
		Require(cache.Find(live)->bUpToDate, "a shader whose include changed wasn't compiled again");
	}
}

int main()
{
	try
	{
		TestEntriesFollowTheirFiles();
		TestMisses();
		TestCopiesFindTheSameEntries();
		TestTheBuildCompilesWhatChanged();
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All shader cache tests passed\n";
	return 0;
}
