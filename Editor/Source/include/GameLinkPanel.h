#pragma once

#include "EditorLink.h"
#include "EditorLinkRecorder.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Nyx::Editor
{
	// The "Game Link" window: what the game logs (Game Log tab), and every message of the editor
	// link in both directions (Messages tab). It can also record the messages to .nyxlinklog files.
	class GameLinkPanel
	{
	public:
		// Every message of a game's editor link, both ways. gameName tells the games of Play All
		// apart, e.g. "Game 2". The games' log lines also go to the Game Log tab.
		void AddMessage(const std::string& gameName, Nyx::Engine::ELinkDirection direction, const Nyx::Net::Message& message);

		// A new play session: clears both tabs ("Clear on Play"), or marks where the session
		// starts, and starts new recording files if recording is on (one per game).
		void OnPlayStarted();

		void Draw(bool& bOpen);

		// Where recordings go: a "LinkLogs" folder next to the editor
		static std::filesystem::path GetRecordingFolder();

		// Each tab keeps this many entries; older ones are dropped
		static constexpr size_t MaxEntries = 20000;

	private:
		struct LogEntry
		{
			// Counts up over all lines, also dropped ones, so filtered lists can refer to lines
			uint64_t Sequence = 0;

			uint64_t TimeMs = 0;
			std::string GameName;
			Nyx::Engine::ELogLevel Level = Nyx::Engine::ELogLevel::Info;
			std::string LoggerName;
			std::string Text;

			// Game, logger and text in lower case, what the filter searches
			std::string SearchText;

			// A line that marks the start of a play session instead of a log line
			bool bSessionStart = false;
		};

		struct MessageEntry
		{
			uint64_t Id = 0;

			// When the editor sent or received it
			uint64_t TimeMs = 0;

			std::string GameName;
			Nyx::Engine::ELinkDirection Direction = Nyx::Engine::ELinkDirection::Sent;
			uint16_t Type = 0;
			size_t Size = 0;

			// Name and summary. The details are described from the payload when they are shown,
			// since messages like CreateEntity carry a lot.
			Nyx::Engine::EditorLinkMessageText Text;
			std::vector<std::byte> Payload;

			// A row that marks the start of a play session instead of a message
			bool bSessionStart = false;
		};

		void DrawGameLogTab();
		void DrawMessagesTab();
		void DrawMessageDetails(const MessageEntry& message);

		void AddGameLogEntry(LogEntry entry);
		void ClearGameLog();
		void ClearMessages();

		// Brings FilteredGameLog up to date with the filter text
		void ApplyGameLogFilter();
		bool MatchesGameLogFilter(const LogEntry& entry) const;
		const LogEntry& GetGameLogEntry(uint64_t sequence) const;

		void StartRecording();
		void StopRecording();

		// The game's file of this recording, opened with its first message
		Nyx::Engine::EditorLinkRecorder* GetRecorder(const std::string& gameName);

		bool IsShown(const MessageEntry& entry) const;
		static std::string ToText(const LogEntry& entry);
		static std::string ToText(const MessageEntry& entry);
		static std::string DescribeDetails(const MessageEntry& entry);

	private:
		std::deque<LogEntry> GameLog;
		uint64_t NextLogSequence = 1;
		size_t WarningCount = 0;
		size_t ErrorCount = 0;
		bool bGameLogAutoScroll = true;
		std::array<char, 128> GameLogFilter{};

		// With a filter, the lines it lets through. Kept up to date as lines come and go, and only
		// rebuilt when the filter changes, instead of searching every line every frame.
		std::string AppliedGameLogFilter;
		std::deque<uint64_t> FilteredGameLog;

		std::deque<MessageEntry> Messages;
		uint64_t NextMessageId = 1;
		uint64_t SelectedMessageId = 0;

		// The selected message's details, described once
		uint64_t DetailsMessageId = 0;
		std::string DetailsText;
		bool bPaused = false;

		// Types seen so far, with their names, and whether the Messages tab shows them
		std::map<uint16_t, std::string> TypeNames;
		std::map<uint16_t, bool> bShowType;

		bool bClearOnPlay = true;

		// While recording, each game's messages go to a file of their own, named after the
		// recording's start and the game: GameLink_<start>_<game>.nyxlinklog
		bool bRecording = false;
		std::string RecordingStamp;
		std::map<std::string, std::unique_ptr<Nyx::Engine::EditorLinkRecorder>> Recorders;
		std::string RecordingStatus;
	};
}
