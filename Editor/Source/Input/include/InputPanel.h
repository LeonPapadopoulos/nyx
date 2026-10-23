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
	class TransactionSystem;

	// Window > Input: the editor's own input, through the input system's debug view (devices,
	// actions with their traces, contexts, the control map, events, timeline), and with an input
	// asset attached, its Tuning and Checks tabs (InputTuningView). It keeps the last seconds of
	// input whether it is open or not, so the timeline has them when it opens.
	class InputPanel
	{
	public:
		// The asset the Tuning and Checks tabs edit, with the editor's undo; both must outlive this
		void Attach(InputAssetDocument& document, TransactionSystem& transactions)
		{
			Document = &document;
			Transactions = &transactions;
		}

		// Once per frame, after the input system began the frame
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
	};
}
