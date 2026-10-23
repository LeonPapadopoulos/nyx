#pragma once

#include "InputRecording.h"
#include "Layer.h"

#include <filesystem>

namespace Nyx::Game
{
	// Records the game's input to a file from startup on (--input-record), e.g. for a playtester
	// to attach to a bug report; the editor's Input window and NyxDump read it. Or plays a
	// recording into the game's input instead of the devices' (--input-replay), to see the bug
	// happen again. Either path may be empty.
	class InputRecordingLayer : public Nyx::ILayer
	{
	public:
		InputRecordingLayer(std::filesystem::path recordPath, std::filesystem::path replayPath);

		void OnAttach(Nyx::Application& application) override;
		void OnDetach() override;
		void OnUpdate(float deltaTime) override;

	private:
		std::filesystem::path RecordPath;
		std::filesystem::path ReplayPath;
		Nyx::Input::InputRecordingSession Recording;
	};
}
