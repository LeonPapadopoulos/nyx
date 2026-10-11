#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Nyx
{
	// One function on the call stack of a crash. Module, function, file and line are empty or 0
	// when there are no symbols for it (no .pdb next to the module).
	struct CrashStackFrame
	{
		uint64_t Address = 0;
		std::string Module;
		std::string Function;
		std::string File;
		uint32_t Line = 0;
	};

	// What made the program crash, and where
	struct CrashReport
	{
		// The system's exception code, e.g. 0xC0000005 for an access violation; also the exit code
		uint32_t ExceptionCode = 0;

		// In words, e.g. "access violation writing address 0x0000000000000000"
		std::string Description;

		// Innermost first
		std::vector<CrashStackFrame> Frames;
	};

	// Called on the crashing thread, after the report is made and before the program ends. The
	// program is in a broken state then, so it should do little: e.g. send the report and log it.
	using CrashCallback = std::function<void(const CrashReport& report)>;

	// From now on, an exception nothing catches (an access violation, a failed ASSERT, an
	// uncaught C++ exception, ...) makes a CrashReport, calls the callback, and ends the program
	// with the exception code, without the system's "stopped working" dialog. While a debugger is
	// attached, the debugger gets the exception instead. Call it once, early; a second call
	// replaces the callback.
	void InstallCrashHandler(CrashCallback callback);

	// The report as text, one frame per line, e.g.
	//   access violation writing address 0x0000000000000000 (exception 0xC0000005)
	//     #0 NyxGame.exe!Nyx::Game::GameLayer::OnUpdate + 0x2A  GameLayer.cpp:42
	std::string FormatCrashReport(const CrashReport& report);
}
