#pragma once

#include "CrashHandler.h"
#include "EditorLink.h"
#include "EditorLinkLogSink.h"
#include "EditorLinkRecorder.h"
#include "Entity.h"
#include "InputAsset.h"
#include "InputStream.h"
#include "Layer.h"
#include "SceneSerializationTypes.h"

#include <cstdint>
#include <filesystem>
#include <memory>

namespace Nyx
{
	class Application;
}

namespace Nyx::Game
{
	// The game's end of the editor link, used when the editor started the game: connects back to
	// the editor on the port given with --editor-port, sends the game's log lines, applies the
	// editor's live edits to the game's world and its input asset to the game's input (with the
	// player's settings over it), streams the game's input while the editor's Input window shows
	// it, answers the editor's questions about an entity's run-time components (marking the entity
	// for the game's systems while the editor asks, InspectionRequestComponent), and quits when the
	// editor asks. If connecting fails, or the editor goes away, the game
	// keeps running.
	//
	// Shaders come compiled from the editor (IRenderer::FollowEditorShaders()), with the material
	// values it edits, saved or not; the game compiles only what nothing compiled yet. The values of
	// the reflected assets it edits (physics materials, a game's own types) go to whoever listens for
	// their type (ApplyAssetValues()) the same way.
	class EditorLinkLayer : public Nyx::ILayer
	{
	public:
		// logSink collects the log lines to send, from startup on. With a recordingPath
		// (--link-log), every message of the link is recorded there.
		EditorLinkLayer(uint16_t editorPort, std::shared_ptr<Nyx::EditorLinkLogSink> logSink,
			std::filesystem::path recordingPath);

		// Where the editor's live edits go. Without a world, they are skipped with a warning.
		void SetWorld(Nyx::Registry& world, Nyx::ScenePostLoadContext postLoadContext)
		{
			World = &world;
			PostLoadContext = postLoadContext;
		}

		void OnAttach(Nyx::Application& application) override;
		void OnDetach() override;
		void OnUpdate(float deltaTime) override;

		// From the crash handler, on the crashing thread: sends the last log lines and the report
		// to the editor, and waits briefly until they arrive. Does nothing without a connected link.
		static void SendCrashReport(const Nyx::CrashReport& report);

	private:
		// Live tuning, a live edit, or a message this game doesn't know
		void ApplyMessage(const Nyx::Net::Message& message);
		void ApplyLiveEdit(const Nyx::Net::Message& message);
		// An InspectEntity message: the entity's run-time components, at once; marks the entity
		void AnswerInspection(const Nyx::Net::Message& message);

		void SendLogLines();
		void SendInputFrames();
		void EndLink();

	private:
		uint16_t EditorPort = 0;
		std::shared_ptr<Nyx::EditorLinkLogSink> LogSink;
		std::filesystem::path RecordingPath;

		Nyx::Application* App = nullptr;
		Nyx::Registry* World = nullptr;
		Nyx::ScenePostLoadContext PostLoadContext;
		Nyx::EditorLinkRecorder Recorder;
		std::unique_ptr<Nyx::EditorLink> Link;

		// Over the editor's input asset, as at startup
		std::optional<Nyx::Input::InputPlayerSettingsData> PlayerInputSettings;
		// What the last asset from the editor had that can't be used, to log it once
		std::vector<std::string> InputProblems;

		// The game's input frames, while the editor subscribed to them
		std::unique_ptr<Nyx::Input::InputStreamWriter> InputStream;
	};
}
