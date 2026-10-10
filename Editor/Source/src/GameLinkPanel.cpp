#include "GameLinkPanel.h"

#include "ImGuiSource.h"
#include "Paths.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <string_view>
#include <vector>

namespace
{
	using namespace Nyx::Engine;

	ImVec4 GetLevelColor(ELogLevel level)
	{
		switch (level)
		{
		case ELogLevel::Trace:
		case ELogLevel::Debug: return ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
		case ELogLevel::Warning: return ImVec4(0.95f, 0.78f, 0.30f, 1.0f);
		case ELogLevel::Error:
		case ELogLevel::Critical: return ImVec4(0.95f, 0.40f, 0.40f, 1.0f);
		default: return ImGui::GetStyleColorVec4(ImGuiCol_Text);
		}
	}

	// Seen from the editor: it sends to the game and receives from it
	const char* GetDirectionText(ELinkDirection direction)
	{
		return direction == ELinkDirection::Sent ? "-> game" : "<- game";
	}

	std::string ToLowerCase(std::string text)
	{
		for (char& c : text)
		{
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		return text;
	}

	// The table needs rows of one line each: the first line of a text, and how many follow
	std::string_view GetFirstLine(const std::string& text, size_t& outMoreLineCount)
	{
		const size_t lineEnd = text.find('\n');
		outMoreLineCount = lineEnd == std::string::npos ? 0 : static_cast<size_t>(std::count(text.begin(), text.end(), '\n'));
		return std::string_view(text).substr(0, lineEnd == std::string::npos ? text.size() : lineEnd);
	}
}

namespace Nyx::Editor
{
	void GameLinkPanel::AddMessage(ELinkDirection direction, const Nyx::Net::Message& message)
	{
		// Recording goes on while the tab is paused
		Recorder.Record(direction, message);

		if (direction == ELinkDirection::Received && message.Type == static_cast<uint16_t>(EEditorLinkMessage::LogLine))
		{
			BinaryReader reader;
			reader.LoadFromMemory(message.Payload);
			LogLineMessage line;
			if (line.Read(reader))
			{
				LogEntry entry;
				entry.TimeMs = line.TimeMs;
				entry.Level = line.Level;
				entry.LoggerName = std::move(line.LoggerName);
				entry.Text = std::move(line.Text);
				AddGameLogEntry(std::move(entry));
			}
		}

		if (bPaused)
		{
			return;
		}

		MessageEntry entry;
		entry.Id = NextMessageId++;
		entry.TimeMs = GetClockTimeMs();
		entry.Direction = direction;
		entry.Type = message.Type;
		entry.Size = message.Payload.size();
		entry.Text = DescribeEditorLinkMessage(message, false);
		entry.Payload = message.Payload;

		if (TypeNames.emplace(message.Type, entry.Text.Name).second)
		{
			bShowType[message.Type] = true;
		}

		Messages.push_back(std::move(entry));
		while (Messages.size() > MaxEntries)
		{
			Messages.pop_front();
		}
	}

	void GameLinkPanel::AddGameLogEntry(LogEntry entry)
	{
		entry.Sequence = NextLogSequence++;
		entry.SearchText = ToLowerCase(entry.LoggerName + " " + entry.Text);
		WarningCount += !entry.bSessionStart && entry.Level == ELogLevel::Warning ? 1 : 0;
		ErrorCount += !entry.bSessionStart && entry.Level >= ELogLevel::Error ? 1 : 0;

		if (!AppliedGameLogFilter.empty() && MatchesGameLogFilter(entry))
		{
			FilteredGameLog.push_back(entry.Sequence);
		}

		GameLog.push_back(std::move(entry));

		// The oldest lines go, and with them their counts and their place in the filtered list
		while (GameLog.size() > MaxEntries)
		{
			const LogEntry& oldest = GameLog.front();
			WarningCount -= !oldest.bSessionStart && oldest.Level == ELogLevel::Warning ? 1 : 0;
			ErrorCount -= !oldest.bSessionStart && oldest.Level >= ELogLevel::Error ? 1 : 0;

			while (!FilteredGameLog.empty() && FilteredGameLog.front() <= oldest.Sequence)
			{
				FilteredGameLog.pop_front();
			}

			GameLog.pop_front();
		}
	}

	void GameLinkPanel::OnPlayStarted()
	{
		if (bClearOnPlay)
		{
			ClearGameLog();
			ClearMessages();
		}
		else
		{
			const uint64_t now = GetClockTimeMs();

			LogEntry logMarker;
			logMarker.TimeMs = now;
			logMarker.bSessionStart = true;
			AddGameLogEntry(std::move(logMarker));

			MessageEntry marker;
			marker.Id = NextMessageId++;
			marker.TimeMs = now;
			marker.bSessionStart = true;
			Messages.push_back(std::move(marker));
		}

		// One file per play session
		if (bRecording)
		{
			StartRecording();
		}
	}

	std::filesystem::path GameLinkPanel::GetRecordingFolder()
	{
		return Nyx::Paths::GetExecutableDir() / "LinkLogs";
	}

	void GameLinkPanel::Draw(bool& bOpen)
	{
		ImGui::SetNextWindowSize(ImVec2(820.0f, 380.0f), ImGuiCond_FirstUseEver);
		if (!Nyx::UI::Begin("Game Link", &bOpen))
		{
			ImGui::End();
			return;
		}

		if (ImGui::BeginTabBar("##GameLinkTabs"))
		{
			// The ### part keeps the tab the same while its counts change
			std::string gameLogLabel = "Game Log";
			if (ErrorCount > 0 || WarningCount > 0)
			{
				gameLogLabel += " (" + std::to_string(ErrorCount) + " errors, " + std::to_string(WarningCount) + " warnings)";
			}
			gameLogLabel += "###GameLog";

			if (NYX_UI(ImGui::BeginTabItem(gameLogLabel.c_str())))
			{
				DrawGameLogTab();
				ImGui::EndTabItem();
			}

			if (NYX_UI(ImGui::BeginTabItem("Messages")))
			{
				DrawMessagesTab();
				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}

		ImGui::End();
	}

	void GameLinkPanel::DrawGameLogTab()
	{
		if (NYX_UI(ImGui::Button("Clear")))
		{
			ClearGameLog();
		}

		ImGui::SameLine();
		ApplyGameLogFilter();
		const bool bFiltered = !AppliedGameLogFilter.empty();
		const size_t shownCount = bFiltered ? FilteredGameLog.size() : GameLog.size();

		// The lines shown, oldest first
		const auto getShown = [&](size_t index) -> const LogEntry&
		{
			return bFiltered ? GetGameLogEntry(FilteredGameLog[index]) : GameLog[index];
		};

		if (NYX_UI(ImGui::Button("Copy")))
		{
			std::string text;
			for (size_t i = 0; i < shownCount; ++i)
			{
				text += ToText(getShown(i)) + "\n";
			}
			ImGui::SetClipboardText(text.c_str());
		}
		ImGui::SetItemTooltip("Copies the lines shown, as text");

		ImGui::SameLine();
		NYX_UI(ImGui::Checkbox("Auto-scroll", &bGameLogAutoScroll));

		ImGui::SameLine();
		NYX_UI(ImGui::Checkbox("Clear on Play", &bClearOnPlay));

		ImGui::SameLine();
		ImGui::SetNextItemWidth(220.0f);
		NYX_UI(ImGui::InputTextWithHint("##GameLogFilter", "Filter", GameLogFilter.data(), GameLogFilter.size()));

		const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
			ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;
		if (!ImGui::BeginTable("##GameLog", 4, flags))
		{
			return;
		}

		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("00:00:00.000").x);
		ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Critical").x);
		ImGui::TableSetupColumn("Logger", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("ENGINE").x);
		ImGui::TableSetupColumn("Text", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableHeadersRow();

		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(shownCount));
		while (clipper.Step())
		{
			for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
			{
				const LogEntry& entry = getShown(static_cast<size_t>(row));
				ImGui::TableNextRow();

				ImGui::TableNextColumn();
				ImGui::TextUnformatted(FormatClockTime(entry.TimeMs).c_str());

				if (entry.bSessionStart)
				{
					ImGui::TableSetColumnIndex(3);
					ImGui::TextDisabled("--- Play session started ---");
					continue;
				}

				ImGui::TableNextColumn();
				ImGui::TextColored(GetLevelColor(entry.Level), "%s", GetLogLevelName(entry.Level));

				ImGui::TableNextColumn();
				ImGui::TextUnformatted(entry.LoggerName.c_str());

				// One line per row, as the clipper needs; a longer text shows in full on hover
				ImGui::TableNextColumn();
				size_t moreLineCount = 0;
				const std::string_view firstLine = GetFirstLine(entry.Text, moreLineCount);
				ImGui::PushStyleColor(ImGuiCol_Text, GetLevelColor(entry.Level));
				ImGui::TextUnformatted(firstLine.data(), firstLine.data() + firstLine.size());
				ImGui::PopStyleColor();
				if (moreLineCount > 0)
				{
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("%s", entry.Text.c_str());
					}
					ImGui::SameLine();
					ImGui::TextDisabled("(+%zu lines)", moreLineCount);
				}
			}
		}

		// Stays at the newest line while scrolled to the end
		if (bGameLogAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
		{
			ImGui::SetScrollHereY(1.0f);
		}

		ImGui::EndTable();
	}

	void GameLinkPanel::DrawMessagesTab()
	{
		if (NYX_UI(ImGui::Button(bPaused ? "Resume" : "Pause")))
		{
			bPaused = !bPaused;
		}
		ImGui::SetItemTooltip("While paused, new messages aren't shown; recording and the Game Log go on");

		ImGui::SameLine();
		if (NYX_UI(ImGui::Button("Clear")))
		{
			ClearMessages();
		}

		ImGui::SameLine();
		if (NYX_UI(ImGui::Button("Copy")))
		{
			std::string text;
			for (const MessageEntry& entry : Messages)
			{
				if (IsShown(entry))
				{
					text += ToText(entry);
				}
			}
			ImGui::SetClipboardText(text.c_str());
		}
		ImGui::SetItemTooltip("Copies the messages shown, with all their fields, as text");

		ImGui::SameLine();
		if (NYX_UI(ImGui::Button("Types")))
		{
			ImGui::OpenPopup("##MessageTypes");
		}
		ImGui::SetItemTooltip("Which message types to show");

		if (Nyx::UI::BeginPopup("##MessageTypes"))
		{
			if (TypeNames.empty())
			{
				ImGui::TextDisabled("No messages yet");
			}

			for (const auto& typeAndName : TypeNames)
			{
				bool& bShow = bShowType[typeAndName.first];
				NYX_UI(ImGui::Checkbox(typeAndName.second.c_str(), &bShow));
			}
			ImGui::EndPopup();
		}

		ImGui::SameLine();
		NYX_UI(ImGui::Checkbox("Clear on Play##Messages", &bClearOnPlay));

		ImGui::SameLine();
		bool bRecord = bRecording;
		if (NYX_UI(ImGui::Checkbox("Record", &bRecord)))
		{
			if (bRecord)
			{
				StartRecording();
			}
			else
			{
				StopRecording();
			}
		}
		ImGui::SetItemTooltip("Records every message to a .nyxlinklog file in\n%s\nA new file per play session. NyxDump prints them.",
			GetRecordingFolder().string().c_str());

		if (!RecordingStatus.empty())
		{
			ImGui::SameLine();
			ImGui::TextDisabled("%s", RecordingStatus.c_str());
		}

		// Only with types hidden is a list of the shown messages needed
		const bool bAllTypesShown =
			std::all_of(bShowType.begin(), bShowType.end(), [](const auto& typeAndShown) { return typeAndShown.second; });

		std::vector<const MessageEntry*> shown;
		const MessageEntry* selected = nullptr;
		if (!bAllTypesShown || SelectedMessageId != 0)
		{
			for (const MessageEntry& entry : Messages)
			{
				if (IsShown(entry))
				{
					if (!bAllTypesShown)
					{
						shown.push_back(&entry);
					}
					if (entry.Id == SelectedMessageId)
					{
						selected = &entry;
					}
				}
			}
		}

		const size_t shownCount = bAllTypesShown ? Messages.size() : shown.size();

		// The table above, the selected message's fields below
		const float availableHeight = ImGui::GetContentRegionAvail().y;
		const float tableHeight = selected ? availableHeight * 0.6f : 0.0f;

		const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
			ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;
		if (ImGui::BeginTable("##Messages", 5, flags, ImVec2(0.0f, tableHeight)))
		{
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("00:00:00.000").x);
			ImGui::TableSetupColumn("Direction", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Direction").x);
			ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("SetProperties  ").x);
			ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("000000").x);
			ImGui::TableSetupColumn("Content", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();

			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(shownCount));
			while (clipper.Step())
			{
				for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
				{
					const MessageEntry& entry = bAllTypesShown ? Messages[static_cast<size_t>(row)] : *shown[static_cast<size_t>(row)];
					ImGui::TableNextRow();
					ImGui::PushID(static_cast<int>(entry.Id));

					ImGui::TableNextColumn();
					const std::string time = FormatClockTime(entry.TimeMs);
					if (entry.bSessionStart)
					{
						ImGui::TextUnformatted(time.c_str());
						ImGui::TableSetColumnIndex(4);
						ImGui::TextDisabled("--- Play session started ---");
						ImGui::PopID();
						continue;
					}

					if (NYX_UI(ImGui::Selectable(time.c_str(), entry.Id == SelectedMessageId, ImGuiSelectableFlags_SpanAllColumns)))
					{
						SelectedMessageId = entry.Id == SelectedMessageId ? 0 : entry.Id;
					}

					ImGui::TableNextColumn();
					ImGui::TextUnformatted(GetDirectionText(entry.Direction));

					ImGui::TableNextColumn();
					ImGui::TextUnformatted(entry.Text.Name.c_str());

					ImGui::TableNextColumn();
					ImGui::Text("%zu", entry.Size);

					ImGui::TableNextColumn();
					ImGui::TextUnformatted(entry.Text.Summary.c_str());

					ImGui::PopID();
				}
			}

			ImGui::EndTable();
		}

		if (selected)
		{
			DrawMessageDetails(*selected);
		}
	}

	void GameLinkPanel::DrawMessageDetails(const MessageEntry& message)
	{
		if (DetailsMessageId != message.Id)
		{
			DetailsText = DescribeDetails(message);
			DetailsMessageId = message.Id;
		}

		// Read-only, but selectable, so fields can be copied
		std::string text = message.Text.Name + " " + GetDirectionText(message.Direction) + ", " + std::to_string(message.Size) +
			" bytes, at " + FormatClockTime(message.TimeMs) + "\n" + DetailsText;
		NYX_UI(ImGui::InputTextMultiline("##MessageDetails", text.data(), text.size() + 1, ImVec2(-FLT_MIN, -FLT_MIN),
			ImGuiInputTextFlags_ReadOnly));
	}

	void GameLinkPanel::ClearGameLog()
	{
		GameLog.clear();
		FilteredGameLog.clear();
		WarningCount = 0;
		ErrorCount = 0;
	}

	void GameLinkPanel::ApplyGameLogFilter()
	{
		const std::string filter = ToLowerCase(GameLogFilter.data());
		if (filter == AppliedGameLogFilter)
		{
			return;
		}

		AppliedGameLogFilter = filter;
		FilteredGameLog.clear();

		if (filter.empty())
		{
			return;
		}

		for (const LogEntry& entry : GameLog)
		{
			if (MatchesGameLogFilter(entry))
			{
				FilteredGameLog.push_back(entry.Sequence);
			}
		}
	}

	bool GameLinkPanel::MatchesGameLogFilter(const LogEntry& entry) const
	{
		return entry.bSessionStart || entry.SearchText.find(AppliedGameLogFilter) != std::string::npos;
	}

	const GameLinkPanel::LogEntry& GameLinkPanel::GetGameLogEntry(uint64_t sequence) const
	{
		// Sequences are consecutive within the log, so the index follows from the first one
		return GameLog[static_cast<size_t>(sequence - GameLog.front().Sequence)];
	}

	void GameLinkPanel::ClearMessages()
	{
		Messages.clear();
		SelectedMessageId = 0;
		DetailsMessageId = 0;
		DetailsText.clear();
	}

	void GameLinkPanel::StartRecording()
	{
		// e.g. GameLink_2026-10-09_22-15-03-123.nyxlinklog
		std::string stamp = FormatClockTime(GetClockTimeMs(), true);
		std::replace(stamp.begin(), stamp.end(), ' ', '_');
		std::replace(stamp.begin(), stamp.end(), ':', '-');
		std::replace(stamp.begin(), stamp.end(), '.', '-');

		const std::filesystem::path path = GetRecordingFolder() / ("GameLink_" + stamp + ".nyxlinklog");
		bRecording = Recorder.Open(path, "NyxEditor");
		RecordingStatus = bRecording ? "to " + path.filename().string() : "can't write to " + path.string();
	}

	void GameLinkPanel::StopRecording()
	{
		Recorder.Close();
		bRecording = false;
		RecordingStatus.clear();
	}

	bool GameLinkPanel::IsShown(const MessageEntry& entry) const
	{
		if (entry.bSessionStart)
		{
			return true;
		}

		const auto it = bShowType.find(entry.Type);
		return it == bShowType.end() || it->second;
	}

	std::string GameLinkPanel::ToText(const LogEntry& entry)
	{
		if (entry.bSessionStart)
		{
			return FormatClockTime(entry.TimeMs) + " --- Play session started ---";
		}

		return FormatClockTime(entry.TimeMs) + " [" + GetLogLevelName(entry.Level) + "] " + entry.LoggerName + ": " + entry.Text;
	}

	std::string GameLinkPanel::ToText(const MessageEntry& entry)
	{
		if (entry.bSessionStart)
		{
			return FormatClockTime(entry.TimeMs) + "  --- Play session started ---\n";
		}

		std::string text = FormatClockTime(entry.TimeMs) + "  " + GetDirectionText(entry.Direction) + "  " + entry.Text.Name + " (" +
			std::to_string(entry.Size) + " bytes)";
		if (!entry.Text.Summary.empty())
		{
			text += ": " + entry.Text.Summary;
		}
		text += "\n";

		// Fields indented below, as in NyxDump
		const std::string details = DescribeDetails(entry);
		size_t lineStart = 0;
		while (lineStart <= details.size())
		{
			const size_t lineEnd = details.find('\n', lineStart);
			const size_t end = lineEnd == std::string::npos ? details.size() : lineEnd;
			text += "    " + details.substr(lineStart, end - lineStart) + "\n";
			if (lineEnd == std::string::npos)
			{
				break;
			}
			lineStart = lineEnd + 1;
		}

		return text;
	}

	std::string GameLinkPanel::DescribeDetails(const MessageEntry& entry)
	{
		return DescribeEditorLinkMessage(Nyx::Net::Message{ entry.Type, entry.Payload }).Details;
	}
}
