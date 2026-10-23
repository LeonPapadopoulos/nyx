#pragma once

#include "InputDebugView.h"
#include "InputHistory.h"
#include "InputTuningView.h"

namespace Nyx::Input
{
	class InputSystem;
}

namespace Nyx::Editor
{
	class InputAssetDocument;
	class PlaySession;
	class TransactionSystem;

	// Window > Input: the editor's own input or a running game's, through the input system's debug
	// view (devices, actions with their traces, contexts, the control map, events, timeline), and
	// with an input asset attached, its Tuning and Checks tabs (InputTuningView). It keeps the last
	// seconds of the editor's input whether it is open or not, so the timeline has them when it
	// opens. A game streams its input while the window is open and shows it (InputReplica).
	class InputPanel
	{
	public:
		// The asset the Tuning and Checks tabs edit, with the editor's undo; both must outlive this
		void Attach(InputAssetDocument& document, TransactionSystem& transactions)
		{
			Document = &document;
			Transactions = &transactions;
		}

		// The games whose input the window can show; it must outlive this
		void SetGames(PlaySession* games)
		{
			Games = games;
		}

		// Once per frame, after the input system began the frame. Also asks the game shown, and
		// only that one, to stream its input.
		void Record(const Nyx::Input::InputSystem& input);

		void Draw(Nyx::Input::InputSystem& input);

		// Shows this tab from the next Draw() on (tests)
		void SelectTab(Nyx::Input::InputDebugView::ETab tab)
		{
			View.SelectTab(tab);
		}

		// "Tuning" or "Checks"
		void SelectExtraTab(std::string name)
		{
			View.SelectExtraTab(std::move(name));
		}

		// Whose input it shows: empty for the editor's, or a game's name (tests)
		void SelectSource(std::string gameName)
		{
			ShownGame = std::move(gameName);
		}

		InputTuningView& GetTuning()
		{
			return Tuning;
		}

		bool bOpen = false;

	private:
		Nyx::Input::InputHistory History;
		Nyx::Input::InputDebugView View;
		InputTuningView Tuning;
		InputAssetDocument* Document = nullptr;
		TransactionSystem* Transactions = nullptr;

		PlaySession* Games = nullptr;
		// The game whose input is shown; empty for the editor's
		std::string ShownGame;
	};
}
