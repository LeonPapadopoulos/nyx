#pragma once

#include "EditorLink.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace Nyx::Engine
{
	// Records every message of an editor link to a .nyxlinklog file, with time and direction, to
	// look at a session afterwards with NyxDump, also after a crash.
	//
	// File:   magic "NXLL" (u32) | version (u32) | program name (string) | start (u64, ms since 1970 UTC)
	// Then per message, as long as the file goes:
	//         time since start (u64, microseconds) | direction (u8: 0 sent, 1 received) | type (u16) |
	//         payload size (u32) | payload
	class EditorLinkRecorder
	{
	public:
		// Starts a new file, replacing an existing one. programName is the program whose side the
		// file shows ("sent" means sent by it). On failure, nothing is recorded.
		bool Open(const std::filesystem::path& path, const std::string& programName);

		bool IsOpen() const
		{
			return File.is_open();
		}

		const std::filesystem::path& GetPath() const
		{
			return Path;
		}

		// Appends the message and flushes, so the file is complete up to here even if the program crashes
		void Record(ELinkDirection direction, const Net::Message& message);

		void Close();

		// Prints a recording as text. A recording that ends in the middle of a message, e.g. after
		// a crash, is printed up to there. Returns false, with the reason at the end of outText,
		// if the file isn't a recording.
		static bool PrintFile(const std::filesystem::path& path, std::string& outText);

	private:
		std::ofstream File;
		std::filesystem::path Path;
		std::chrono::steady_clock::time_point Start;
	};
}
