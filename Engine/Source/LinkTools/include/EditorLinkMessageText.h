#pragma once

#include "EditorLinkMessages.h"

#include <cstdint>
#include <string>

namespace Nyx
{
	// A message as readable text, for the editor's message log and NyxDump
	struct EditorLinkMessageText
	{
		// "LogLine", or "type 57" for a type this build doesn't know
		std::string Name;

		// One line, e.g. "[Info] APP: Running scene 'Default.nyxscene'"
		std::string Summary;

		// Every field on its own line, e.g. "Level: Info". Left empty without bWithDetails.
		std::string Details;
	};

	// Without details, it is quicker for messages that carry a lot, such as CreateEntity, e.g.
	// for a log that only shows the details of the message selected.
	EditorLinkMessageText DescribeEditorLinkMessage(const Net::Message& message, bool bWithDetails = true);

	// "Info", "Warning" and so on
	const char* GetLogLevelName(ELogLevel level);

	// "22:15:03.123", or "2026-10-09 22:15:03.123" with the date, in this computer's time zone.
	// timeMs is milliseconds since 1970 (UTC).
	std::string FormatClockTime(uint64_t timeMs, bool bWithDate = false);
}
