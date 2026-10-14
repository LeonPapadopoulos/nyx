// ShaderPrograms on real files, compiling in this process: programs start from the newest code that
// matches their files, the build's or live reload's, and one that changed while nothing ran is
// compiled at once; a saved source is compiled once it settled; a broken one keeps its last good code and says where
// it broke; an include's change compiles the programs using it; other files change nothing; and a
// save during a compile is compiled after it.
#include "Paths.h"
#include "ShaderCache.h"
#include "ShaderCompiler.h"
#include "ShaderPrograms.h"

#include <algorithm>
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
	using namespace Nyx::Engine;
	using Clock = ShaderPrograms::Clock;

	void Require(bool condition, const std::string& message)
	{
		if (!condition)
		{
			throw std::runtime_error(message);
		}
	}

	void WriteFile(const std::filesystem::path& file, const std::string& text)
	{
		std::ofstream stream(file, std::ios::binary | std::ios::trunc);
		stream << text;
	}

	std::string LiveSource(const char* expression)
	{
		return std::string("#version 450\n#include \"Common.glsl\"\nlayout(location = 0) out vec4 outColor;\nvoid main()\n{\n    outColor = ") + expression + ";\n}\n";
	}

	// Compiles in this process, and can hold requests back to stand for a compile under way
	class TestService final : public IShaderCompileService
	{
	public:
		void Submit(const ShaderCompileRequest& request) override
		{
			++Submits;
			Held.push_back(request);
			if (!bHold)
			{
				Release();
			}
		}

		std::vector<ShaderCompileResponse> TakeResults() override
		{
			return Local.TakeResults();
		}

		size_t GetPendingCount() const override
		{
			return Held.size();
		}

		void Release()
		{
			for (const ShaderCompileRequest& request : Held)
			{
				Local.Submit(request);
			}
			Held.clear();
		}

		bool bHold = false;
		int Submits = 0;

	private:
		LocalShaderCompileService Local{ { Nyx::Paths::GetShaderLibraryDir() } };
		std::vector<ShaderCompileRequest> Held;
	};

	// Sources in a folder of their own, the "build's" cache of Live.frag beside them, and a folder
	// for live reload's cache
	struct TestFolders
	{
		std::filesystem::path Root = std::filesystem::temp_directory_path() / ("NyxShaderProgramsTests-" + std::to_string(Clock::now().time_since_epoch().count()));
		std::filesystem::path Sources = Root / "Shaders";
		std::filesystem::path Compiled = Root / "Compiled";
		std::filesystem::path Saved = Root / "Saved";
		std::vector<uint32_t> BuiltSpirv;

		TestFolders()
		{
			std::filesystem::create_directories(Sources);
			std::filesystem::create_directories(Compiled);
			WriteFile(Sources / "Common.glsl", "vec3 Tint()\n{\n    return vec3(0.25);\n}\n");
			WriteFile(Sources / "Live.frag", LiveSource("vec4(Tint(), 1.0)"));
			WriteFile(Sources / "Other.frag", LiveSource("vec4(1.0)"));

			ShaderCompiler compiler({ Nyx::Paths::GetShaderLibraryDir() });
			const ShaderCompileResult built = compiler.CompileProgram(Sources / "Live.frag");
			Require(built.bSucceeded, "Live.frag doesn't compile to start with");
			BuiltSpirv = built.EntryPoints[0].Spirv;
			ShaderCompileRequest request;
			request.File = Sources / "Live.frag";
			Require(ShaderCache(Compiled, Sources).Store(request, built), "the build's cache entry can't be written");
			// File times that differ from what the test writes next
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}

		~TestFolders()
		{
			std::error_code error;
			std::filesystem::remove_all(Root, error);
		}
	};

	// Updates with the real time until one or more updates arrive, for at most five seconds
	std::vector<ShaderProgramUpdate> WaitForUpdates(ShaderPrograms& programs)
	{
		const Clock::time_point deadline = Clock::now() + std::chrono::seconds(5);
		while (Clock::now() < deadline)
		{
			std::vector<ShaderProgramUpdate> updates = programs.Update();
			if (!updates.empty())
			{
				return updates;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		return {};
	}

	// Updates with the real time for a while, expecting nothing
	void RequireQuiet(ShaderPrograms& programs, const std::string& what)
	{
		const Clock::time_point end = Clock::now() + std::chrono::milliseconds(300);
		while (Clock::now() < end)
		{
			Require(programs.Update().empty(), what);
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	}

	void TestProgramsStartFromTheBuild()
	{
		TestFolders folders;
		ShaderPrograms programs(folders.Compiled, folders.Sources);
		Require(programs.GetSpirv("Live.frag") == folders.BuiltSpirv, "Live.frag doesn't start from the build's SPIR-V");
		Require(!programs.StartedOutOfDate("Live.frag"), "the build's code counts as out of date");

		bool bThrew = false;
		try
		{
			programs.GetSpirv("Missing.frag");
		}
		catch (const std::exception&)
		{
			bThrew = true;
		}
		Require(bThrew, "a program the build didn't compile doesn't throw");

		// Without live reload, nothing is compiled
		WriteFile(folders.Sources / "Live.frag", LiveSource("vec4(1.0, 0.0, 0.0, 1.0)"));
		RequireQuiet(programs, "a program was compiled without live reload");
	}

	// Live.frag changed while nothing ran: it starts from the build's code, is compiled at the first
	// update without waiting, and the next start begins from what that compile gave
	void TestOutOfDateProgramsCompileAtOnce()
	{
		TestFolders folders;
		WriteFile(folders.Sources / "Live.frag", LiveSource("vec4(0.0, 0.0, 1.0, 1.0)"));

		std::vector<uint32_t> compiled;
		{
			ShaderPrograms programs(folders.Compiled, folders.Sources, folders.Saved);
			Require(programs.GetSpirv("Live.frag") == folders.BuiltSpirv, "an out-of-date Live.frag doesn't start from the build's code");
			Require(programs.StartedOutOfDate("Live.frag"), "the changed Live.frag isn't seen as out of date");
			auto service = std::make_unique<TestService>();
			TestService& testService = *service;
			Require(programs.EnableLiveReload(std::move(service)), "live reload doesn't start: " + programs.GetProblem());

			const std::vector<ShaderProgramUpdate> updates = programs.Update();
			Require(testService.Submits == 1 && updates.size() == 1 && updates[0].bChanged, "the out-of-date Live.frag isn't compiled at the first update");
			compiled = programs.GetSpirv("Live.frag");
			Require(compiled != folders.BuiltSpirv, "the compile didn't change Live.frag's code");
		}

		// The next start: live reload's code, which matches the files
		ShaderPrograms programs(folders.Compiled, folders.Sources, folders.Saved);
		Require(programs.GetSpirv("Live.frag") == compiled, "the next start doesn't begin from live reload's code");
		Require(!programs.StartedOutOfDate("Live.frag"), "live reload's code counts as out of date");
		auto service = std::make_unique<TestService>();
		TestService& testService = *service;
		Require(programs.EnableLiveReload(std::move(service)), "live reload doesn't start: " + programs.GetProblem());
		RequireQuiet(programs, "a program that matches its files was compiled at start");
		Require(testService.Submits == 0, "a program that matches its files was sent to compile");

		// The source goes back to what the build compiled: the build's code matches again and wins
		WriteFile(folders.Sources / "Live.frag", LiveSource("vec4(Tint(), 1.0)"));
		ShaderPrograms again(folders.Compiled, folders.Sources, folders.Saved);
		Require(again.GetSpirv("Live.frag") == folders.BuiltSpirv && !again.StartedOutOfDate("Live.frag"), "the build's matching code isn't chosen");
	}

	void TestSavedProgramsCompileAfterTheySettled()
	{
		TestFolders folders;
		ShaderPrograms programs(folders.Compiled, folders.Sources);
		programs.GetSpirv("Live.frag");
		auto service = std::make_unique<TestService>();
		TestService& testService = *service;
		Require(programs.EnableLiveReload(std::move(service)), "live reload doesn't start: " + programs.GetProblem());

		// With the time held still, the change is seen but not compiled
		const Clock::time_point changedAt = Clock::now();
		WriteFile(folders.Sources / "Live.frag", LiveSource("vec4(1.0, 0.0, 0.0, 1.0)"));
		const Clock::time_point deadline = Clock::now() + std::chrono::seconds(2);
		while (!programs.IsBusy() && Clock::now() < deadline)
		{
			programs.Update(changedAt);
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		Require(programs.IsBusy(), "the saved file isn't noticed");
		Require(programs.Update(changedAt + std::chrono::milliseconds(49)).empty() && testService.Submits == 0, "compiled before the file settled");

		const std::vector<ShaderProgramUpdate> updates = programs.Update(changedAt + ShaderPrograms::SettleTime);
		Require(testService.Submits == 1 && updates.size() == 1, "not compiled once the file settled");
		Require(updates[0].Name == "Live.frag" && updates[0].bChanged && updates[0].Result.bSucceeded, "Live.frag didn't compile");
		Require(programs.GetSpirv("Live.frag") != folders.BuiltSpirv, "Live.frag's SPIR-V didn't change");
	}

	void TestBrokenProgramsKeepTheirLastGoodCode()
	{
		TestFolders folders;
		ShaderPrograms programs(folders.Compiled, folders.Sources);
		programs.GetSpirv("Live.frag");
		Require(programs.EnableLiveReload(std::make_unique<TestService>()), "live reload doesn't start: " + programs.GetProblem());

		// Line 6, column 21: "Tnt"
		WriteFile(folders.Sources / "Live.frag", LiveSource("vec4(Tnt(), 1.0)"));
		std::vector<ShaderProgramUpdate> updates = WaitForUpdates(programs);
		Require(updates.size() == 1 && !updates[0].bChanged && !updates[0].Result.bSucceeded, "a broken Live.frag doesn't fail");
		Require(programs.GetSpirv("Live.frag") == folders.BuiltSpirv, "a broken Live.frag lost its last good code");
		const auto& diagnostics = updates[0].Result.Diagnostics;
		const bool bWhere = std::any_of(diagnostics.begin(), diagnostics.end(), [](const ShaderDiagnostic& diagnostic)
			{
				return diagnostic.File.filename() == "Live.frag" && diagnostic.Line == 6 && diagnostic.Column == 21;
			});
		Require(bWhere, "the error doesn't point at Live.frag(6,21)");

		// Fixed again
		WriteFile(folders.Sources / "Live.frag", LiveSource("vec4(Tint() * 2.0, 1.0)"));
		updates = WaitForUpdates(programs);
		Require(updates.size() == 1 && updates[0].bChanged, "the fixed Live.frag isn't compiled");
		const std::vector<uint32_t> fixed = programs.GetSpirv("Live.frag");
		Require(fixed != folders.BuiltSpirv, "the fixed Live.frag's SPIR-V didn't change");

		// An include it uses changes: it is compiled again
		WriteFile(folders.Sources / "Common.glsl", "vec3 Tint()\n{\n    return vec3(0.5);\n}\n");
		updates = WaitForUpdates(programs);
		Require(updates.size() == 1 && updates[0].Name == "Live.frag" && updates[0].bChanged, "a changed include doesn't compile the program using it");
		Require(programs.GetSpirv("Live.frag") != fixed, "the include's change didn't reach the SPIR-V");

		// Other files, and programs nobody asked for, change nothing
		WriteFile(folders.Sources / "Notes.txt", "not a shader");
		WriteFile(folders.Sources / "Other.frag", LiveSource("vec4(0.5)"));
		RequireQuiet(programs, "a change to another file compiled something");
	}

	void TestASaveDuringACompileIsCompiledAfterIt()
	{
		TestFolders folders;
		ShaderPrograms programs(folders.Compiled, folders.Sources);
		programs.GetSpirv("Live.frag");
		auto service = std::make_unique<TestService>();
		TestService& testService = *service;
		testService.bHold = true;
		Require(programs.EnableLiveReload(std::move(service)), "live reload doesn't start: " + programs.GetProblem());

		WriteFile(folders.Sources / "Live.frag", LiveSource("vec4(1.0, 0.0, 0.0, 1.0)"));
		Clock::time_point deadline = Clock::now() + std::chrono::seconds(5);
		while (testService.Submits == 0 && Clock::now() < deadline)
		{
			programs.Update();
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		Require(testService.Submits == 1, "the first save isn't compiled");

		// Saved again while the first compile is under way: it waits for that one
		WriteFile(folders.Sources / "Live.frag", LiveSource("vec4(0.0, 1.0, 0.0, 1.0)"));
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
		for (int frame = 0; frame < 10; ++frame)
		{
			programs.Update();
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		Require(testService.Submits == 1, "a second compile went out while the first was under way");

		testService.Release();
		std::vector<ShaderProgramUpdate> updates = programs.Update();
		Require(updates.size() == 1 && updates[0].bChanged, "the first compile's result didn't arrive");

		// (The held compile read the file when it was released, so both compiles see the second save)
		programs.Update();
		Require(testService.Submits == 2, "the second save isn't compiled after the first");
		testService.Release();
		updates = programs.Update();
		Require(updates.size() == 1 && updates[0].bChanged, "the second compile's result didn't arrive");
	}
}

int main()
{
	try
	{
		TestProgramsStartFromTheBuild();
		TestOutOfDateProgramsCompileAtOnce();
		TestSavedProgramsCompileAfterTheySettled();
		TestBrokenProgramsKeepTheirLastGoodCode();
		TestASaveDuringACompileIsCompiledAfterIt();
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All shader program tests passed\n";
	return 0;
}
