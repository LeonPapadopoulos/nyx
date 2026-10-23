#pragma once

#include "InputDebugView.h"
#include "InputHistory.h"

namespace Nyx::Input
{
	class InputSystem;
}

namespace Nyx::Editor
{
	// Window > Input: the editor's own input, through the input system's debug view (devices,
	// actions with their traces, contexts, the control map, events, timeline). It keeps the last
	// seconds of input whether it is open or not, so the timeline has them when it opens.
	class InputPanel
	{
	public:
		// Once per frame, after the input system began the frame
		void Record(const Nyx::Input::InputSystem& input);

		void Draw(Nyx::Input::InputSystem& input);

		// Shows this tab from the next Draw() on (tests)
		void SelectTab(Nyx::Input::InputDebugView::ETab tab)
		{
			View.SelectTab(tab);
		}

		bool bOpen = false;

	private:
		Nyx::Input::InputHistory History;
		Nyx::Input::InputDebugView View;
	};
}
