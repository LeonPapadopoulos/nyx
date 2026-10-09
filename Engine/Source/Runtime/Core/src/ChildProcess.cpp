#include "ChildProcess.h"

#if defined(_WIN32)
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <Windows.h>
#else
	#error Nyx::ChildProcess is not implemented for this platform yet.
#endif

namespace
{
	std::wstring Utf8ToWide(const std::string& text)
	{
		if (text.empty())
		{
			return {};
		}

		const int length = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
		std::wstring wide(static_cast<size_t>(length), L'\0');
		::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
		return wide;
	}

	// Windows passes one command line, which the started program splits into arguments again.
	// This quotes an argument so it comes out unchanged: backslashes only need doubling in front
	// of a quote, and a quote inside the argument gets a backslash.
	std::wstring QuoteArgument(const std::wstring& argument)
	{
		if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos)
		{
			return argument;
		}

		std::wstring quoted = L"\"";
		size_t backslashCount = 0;

		for (const wchar_t c : argument)
		{
			if (c == L'\\')
			{
				++backslashCount;
				continue;
			}

			if (c == L'"')
			{
				quoted.append(backslashCount * 2 + 1, L'\\');
			}
			else
			{
				quoted.append(backslashCount, L'\\');
			}

			quoted += c;
			backslashCount = 0;
		}

		// Backslashes right before the closing quote must not escape it
		quoted.append(backslashCount * 2, L'\\');
		quoted += L'"';
		return quoted;
	}
}

namespace Nyx
{
	ChildProcess::~ChildProcess()
	{
		ReleaseHandle();
	}

	bool ChildProcess::Start(const std::filesystem::path& executable, const std::vector<std::string>& arguments)
	{
		if (IsRunning())
		{
			return false;
		}

		ReleaseHandle();

		std::wstring commandLine = QuoteArgument(executable.wstring());
		for (const std::string& argument : arguments)
		{
			commandLine += L' ';
			commandLine += QuoteArgument(Utf8ToWide(argument));
		}

		STARTUPINFOW startupInfo{};
		startupInfo.cb = sizeof(startupInfo);

		PROCESS_INFORMATION processInfo{};

		// CreateProcessW may write into the command line, so it gets the string's own buffer
		const BOOL bStarted = ::CreateProcessW(
			executable.c_str(),
			commandLine.data(),
			nullptr,
			nullptr,
			FALSE,
			CREATE_NEW_CONSOLE,
			nullptr,
			nullptr,
			&startupInfo,
			&processInfo);

		if (!bStarted)
		{
			return false;
		}

		// Only the process handle is needed, not the one of its first thread
		::CloseHandle(processInfo.hThread);
		Handle = processInfo.hProcess;
		ProcessId = processInfo.dwProcessId;
		return true;
	}

	bool ChildProcess::IsRunning() const
	{
		return Handle && ::WaitForSingleObject(Handle, 0) == WAIT_TIMEOUT;
	}

	std::optional<uint32_t> ChildProcess::GetExitCode() const
	{
		DWORD exitCode = 0;
		if (!Handle || IsRunning() || !::GetExitCodeProcess(Handle, &exitCode))
		{
			return std::nullopt;
		}

		return static_cast<uint32_t>(exitCode);
	}

	void ChildProcess::Terminate()
	{
		if (!IsRunning())
		{
			return;
		}

		// Terminating only starts ending the program; wait (at most a few seconds) until it's gone
		constexpr DWORD ExitCodeWhenTerminated = 1;
		::TerminateProcess(Handle, ExitCodeWhenTerminated);
		::WaitForSingleObject(Handle, 5000);
	}

	void ChildProcess::WaitForDebugger()
	{
		while (!::IsDebuggerPresent())
		{
			::Sleep(100);
		}
	}

	void ChildProcess::ReleaseHandle()
	{
		if (Handle)
		{
			::CloseHandle(Handle);
			Handle = nullptr;
		}
	}
}
