#pragma once

#include "EditorLink.h"
#include "EditorLinkLogSink.h"
#include "EditorLinkRecorder.h"
#include "Layer.h"

#include <cstdint>
#include <filesystem>
#include <memory>

namespace Nyx::Engine
{
	class Application;
}

namespace Nyx::Game
{
	// The game's end of the editor link, used when the editor started the game: connects back to
	// the editor on the port given with --editor-port, sends the game's log lines and quits when
	// the editor asks. If connecting fails, or the editor goes away, the game keeps running.
	class EditorLinkLayer : public Nyx::Engine::ILayer
	{
	public:
		// logSink collects the log lines to send, from startup on. With a recordingPath
		// (--link-log), every message of the link is recorded there.
		EditorLinkLayer(uint16_t editorPort, std::shared_ptr<Nyx::Engine::EditorLinkLogSink> logSink,
			std::filesystem::path recordingPath);

		void OnAttach(Nyx::Engine::Application& application) override;
		void OnDetach() override;
		void OnUpdate(float deltaTime) override;

	private:
		void SendLogLines();
		void EndLink();

	private:
		uint16_t EditorPort = 0;
		std::shared_ptr<Nyx::Engine::EditorLinkLogSink> LogSink;
		std::filesystem::path RecordingPath;

		Nyx::Engine::Application* App = nullptr;
		Nyx::Engine::EditorLinkRecorder Recorder;
		std::unique_ptr<Nyx::Engine::EditorLink> Link;
	};
}
