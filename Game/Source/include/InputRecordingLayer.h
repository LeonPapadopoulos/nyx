#pragma once

#include "InputRecording.h"
#include "Layer.h"

#include <filesystem>

namespace Nyx::Game
{
	// Records the game's input to a file from startup on (--input-record), e.g. for a playtester
	// to attach to a bug report. The editor's Input window and NyxDump read it.
	class InputRecordingLayer : public Nyx::ILayer
	{
	public:
		explicit InputRecordingLayer(std::filesystem::path recordPath);

		void OnAttach(Nyx::Application& application) override;
		void OnDetach() override;
		void OnUpdate(float deltaTime) override;

	private:
		std::filesystem::path RecordPath;
		Nyx::Input::InputRecordingSession Recording;
	};
}
