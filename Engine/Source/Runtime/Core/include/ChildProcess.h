#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Not named Process.h: Windows doesn't tell upper and lower case apart in file names, so that
// file would be found in place of the system's <process.h>, which <thread> and others include.

namespace Nyx
{
	// Starts another program and keeps track of it, e.g. the editor starting the game.
	// Destroying a ChildProcess doesn't end the program; call Terminate() for that.
	class ChildProcess
	{
	public:
		ChildProcess() = default;
		~ChildProcess();

		ChildProcess(const ChildProcess&) = delete;
		ChildProcess& operator=(const ChildProcess&) = delete;

		// Starts the program with the arguments (UTF-8). It gets its own console window and runs
		// in this program's working directory. Fails if this ChildProcess is still running another one.
		bool Start(const std::filesystem::path& executable, const std::vector<std::string>& arguments);

		bool IsRunning() const;

		// The program's exit code, once it has exited
		std::optional<uint32_t> GetExitCode() const;

		// The system's id of the program started last, 0 before the first Start()
		uint32_t GetProcessId() const
		{
			return ProcessId;
		}

		// Ends the program at once, without letting it clean up, and waits until it is gone.
		void Terminate();

		// Called by the started program itself: blocks until a debugger is attached to it, e.g. to
		// debug the game from its very first line when the editor starts it.
		static void WaitForDebugger();

	private:
		void ReleaseHandle();

	private:
		// The operating system's handle of the program (a Windows HANDLE)
		void* Handle = nullptr;

		uint32_t ProcessId = 0;
	};
}
