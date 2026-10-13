#pragma once

#include "InputBackend.h"
#include "InputFrame.h"

#include <vector>

namespace Nyx::Input
{
	// Gathers what the backends report into one InputFrame per frame, and makes the cursor what
	// the frame asked for (Design/input-system.md). The application owns one and runs it around
	// the layers:
	//
	//   window.PollEvents(); input.BeginFrame(NowUs()); layers' OnUpdate and OnUI; input.EndFrame();
	class InputSystem
	{
	public:
		// The backend must outlive the input system; the window owns the GLFW one.
		void AddBackend(IInputBackend& backend);

		// Takes every backend's events, in the order they happened, into the next frame
		void BeginFrame(uint64_t nowUs);

		// Applies the cursor the frame asked for
		void EndFrame();

		const InputFrame& GetFrame() const
		{
			return Frame;
		}

		// Asks for the cursor to be shown, hidden or captured, for this frame only: ask again each
		// frame for as long as it should stay so. Of several requests the strongest wins (Captured
		// over Hidden over Free); without any, the cursor is free.
		void RequestCursor(CursorRequest request);

		// The cursor as the last EndFrame() made it, and who asked for it
		const CursorRequest& GetCursor() const
		{
			return Cursor;
		}

	private:
		std::vector<IInputBackend*> Backends;
		InputFrame Frame;

		std::vector<CursorRequest> CursorRequests;
		CursorRequest Cursor;
	};
}
