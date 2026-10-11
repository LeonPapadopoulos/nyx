#pragma once

#include "CrashHandler.h"
#include "EditorLink.h"
#include "EditorLinkLogSink.h"
#include "EditorLinkRecorder.h"
#include "Entity.h"
#include "Layer.h"
#include "SceneSerializationTypes.h"

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
	// the editor on the port given with --editor-port, sends the game's log lines, applies the
	// editor's live edits to the game's world and quits when the editor asks. If connecting fails,
	// or the editor goes away, the game keeps running.
	class EditorLinkLayer : public Nyx::Engine::ILayer
	{
	public:
		// logSink collects the log lines to send, from startup on. With a recordingPath
		// (--link-log), every message of the link is recorded there.
		EditorLinkLayer(uint16_t editorPort, std::shared_ptr<Nyx::Engine::EditorLinkLogSink> logSink,
			std::filesystem::path recordingPath);

		// Where the editor's live edits go. Without a world, they are skipped with a warning.
		void SetWorld(Nyx::Engine::Registry& world, Nyx::Engine::ScenePostLoadContext postLoadContext)
		{
			World = &world;
			PostLoadContext = postLoadContext;
		}

		void OnAttach(Nyx::Engine::Application& application) override;
		void OnDetach() override;
		void OnUpdate(float deltaTime) override;

		// From the crash handler, on the crashing thread: sends the last log lines and the report
		// to the editor, and waits briefly until they arrive. Does nothing without a connected link.
		static void SendCrashReport(const Nyx::CrashReport& report);

	private:
		// A live edit, or a message this game doesn't know
		void ApplyMessage(const Nyx::Net::Message& message);

		void SendLogLines();
		void EndLink();

	private:
		uint16_t EditorPort = 0;
		std::shared_ptr<Nyx::Engine::EditorLinkLogSink> LogSink;
		std::filesystem::path RecordingPath;

		Nyx::Engine::Application* App = nullptr;
		Nyx::Engine::Registry* World = nullptr;
		Nyx::Engine::ScenePostLoadContext PostLoadContext;
		Nyx::Engine::EditorLinkRecorder Recorder;
		std::unique_ptr<Nyx::Engine::EditorLink> Link;
	};
}
