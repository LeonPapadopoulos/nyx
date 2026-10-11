#include "CrashHandler.h"

#if defined(_WIN32)
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <Windows.h>
	#include <DbgHelp.h>
#else
	#error Nyx::InstallCrashHandler is not implemented for this platform yet.
#endif

#include <atomic>
#include <cstdio>
#include <filesystem>

namespace
{
	Nyx::CrashCallback GCrashCallback;

	// A crash inside the crash handler must not start it again
	std::atomic<bool> GHandlingCrash{ false };

	// Deeper stacks are cut off; the innermost frames are the interesting ones
	constexpr size_t MaxStackFrames = 64;

	// The exception code of a C++ exception, as the Visual C++ runtime raises it
	constexpr DWORD CppExceptionCode = 0xE06D7363;

	std::string ToHex(uint64_t value, int digits)
	{
		char text[32];
		std::snprintf(text, sizeof(text), "0x%0*llX", digits, static_cast<unsigned long long>(value));
		return text;
	}

	std::string DescribeException(const EXCEPTION_RECORD& record)
	{
		switch (record.ExceptionCode)
		{
		case EXCEPTION_ACCESS_VIOLATION:
		case EXCEPTION_IN_PAGE_ERROR:
		{
			const char* kind = record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION ? "access violation" : "in-page error";
			if (record.NumberParameters < 2)
			{
				return kind;
			}

			// 0 read, 1 write, 8 execute (data execution prevention)
			const ULONG_PTR operation = record.ExceptionInformation[0];
			const char* what = operation == 0 ? "reading" : operation == 1 ? "writing" : operation == 8 ? "executing" : "at";
			return std::string(kind) + " " + what + " address " + ToHex(record.ExceptionInformation[1], 16);
		}

		case EXCEPTION_BREAKPOINT:
			return "breakpoint, e.g. a failed ASSERT (the log says which)";
		case EXCEPTION_INT_DIVIDE_BY_ZERO:
			return "integer division by zero";
		case EXCEPTION_STACK_OVERFLOW:
			return "stack overflow";
		case EXCEPTION_ILLEGAL_INSTRUCTION:
			return "illegal instruction";
		case EXCEPTION_PRIV_INSTRUCTION:
			return "privileged instruction";
		case EXCEPTION_DATATYPE_MISALIGNMENT:
			return "misaligned data access";
		case CppExceptionCode:
			return "C++ exception that nothing caught";
		default:
			return "exception " + ToHex(record.ExceptionCode, 8);
		}
	}

	std::vector<Nyx::CrashStackFrame> WalkStack(CONTEXT context)
	{
		std::vector<Nyx::CrashStackFrame> frames;

#if defined(_M_X64)
		const HANDLE process = ::GetCurrentProcess();
		const HANDLE thread = ::GetCurrentThread();

		STACKFRAME64 stackFrame{};
		stackFrame.AddrPC.Offset = context.Rip;
		stackFrame.AddrPC.Mode = AddrModeFlat;
		stackFrame.AddrFrame.Offset = context.Rbp;
		stackFrame.AddrFrame.Mode = AddrModeFlat;
		stackFrame.AddrStack.Offset = context.Rsp;
		stackFrame.AddrStack.Mode = AddrModeFlat;

		while (frames.size() < MaxStackFrames &&
			::StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &stackFrame, &context, nullptr, ::SymFunctionTableAccess64,
				::SymGetModuleBase64, nullptr))
		{
			if (stackFrame.AddrPC.Offset == 0)
			{
				break;
			}

			Nyx::CrashStackFrame& frame = frames.emplace_back();
			frame.Address = stackFrame.AddrPC.Offset;

			// Outer frames hold return addresses, which point after the call; one byte back is inside it
			const DWORD64 lookupAddress = frames.size() > 1 ? frame.Address - 1 : frame.Address;

			IMAGEHLP_MODULE64 module{};
			module.SizeOfStruct = sizeof(module);
			if (::SymGetModuleInfo64(process, lookupAddress, &module))
			{
				frame.Module = std::filesystem::path(module.ImageName).filename().string();
			}

			alignas(SYMBOL_INFO) char symbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
			SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
			symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
			symbol->MaxNameLen = MAX_SYM_NAME;
			DWORD64 symbolDisplacement = 0;
			if (::SymFromAddr(process, lookupAddress, &symbolDisplacement, symbol))
			{
				frame.Function = symbol->Name;
			}

			IMAGEHLP_LINE64 line{};
			line.SizeOfStruct = sizeof(line);
			DWORD lineDisplacement = 0;
			if (::SymGetLineFromAddr64(process, lookupAddress, &lineDisplacement, &line))
			{
				frame.File = line.FileName;
				frame.Line = line.LineNumber;
			}
		}
#endif

		return frames;
	}

	LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* exception)
	{
		if (GHandlingCrash.exchange(true))
		{
			return EXCEPTION_EXECUTE_HANDLER;
		}

		Nyx::CrashReport report;
		report.ExceptionCode = exception->ExceptionRecord->ExceptionCode;
		report.Description = DescribeException(*exception->ExceptionRecord);
		report.Frames = WalkStack(*exception->ContextRecord);

		if (GCrashCallback)
		{
			GCrashCallback(report);
		}

		// Ends the program with the exception code as its exit code, without the system's dialog
		return EXCEPTION_EXECUTE_HANDLER;
	}
}

namespace Nyx
{
	void InstallCrashHandler(CrashCallback callback)
	{
		GCrashCallback = std::move(callback);

		static bool bInstalled = false;
		if (bInstalled)
		{
			return;
		}
		bInstalled = true;

		// Symbols load when a crash needs them, from the .pdb files next to the modules
		::SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
		::SymInitialize(::GetCurrentProcess(), nullptr, TRUE);

		// After a stack overflow, the handler runs on what is left of the stack; this keeps some
		// for it on this thread
		ULONG stackGuarantee = 64 * 1024;
		::SetThreadStackGuarantee(&stackGuarantee);

		::SetErrorMode(::GetErrorMode() | SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
		::SetUnhandledExceptionFilter(&OnUnhandledException);
	}

	std::string FormatCrashReport(const CrashReport& report)
	{
		std::string text = report.Description + " (exception " + ToHex(report.ExceptionCode, 8) + ")";

		for (size_t i = 0; i < report.Frames.size(); ++i)
		{
			const CrashStackFrame& frame = report.Frames[i];
			text += "\n  #" + std::to_string(i) + " ";
			text += frame.Module.empty() ? std::string("?") : frame.Module;
			text += "!";
			text += frame.Function.empty() ? ToHex(frame.Address, 16) : frame.Function;

			if (!frame.File.empty())
			{
				text += "  " + frame.File + ":" + std::to_string(frame.Line);
			}
		}

		return text;
	}
}
