#pragma once

#include "GameInstance.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Nyx::Editor
{
	// The "Game Crashed" window: a card per game that crashed, with why, its call stack and last log
	// lines, until dismissed or restarted. It comes to the front when a card is added.
	class CrashCardsPanel
	{
	public:
		void Add(GameCrash crash, std::string lastLogLines);

		// Returns the crash whose Restart was clicked; its card is gone then.
		std::optional<GameCrash> Draw();

		bool IsEmpty() const
		{
			return Cards.empty();
		}

		// How many of the game's last log lines a card shows
		static constexpr size_t LogLineCount = 30;

	private:
		struct CrashCard
		{
			uint64_t Id = 0;
			GameCrash Crash;
			std::string LastLogLines;
		};

		void RemoveCard(uint64_t id);

		std::vector<CrashCard> Cards;
		uint64_t NextCardId = 1;
		bool bFocus = false;
	};
}
