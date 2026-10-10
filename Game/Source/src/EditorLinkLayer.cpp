#include "EditorLinkLayer.h"

#include "Application.h"
#include "LiveEdits.h"
#include "Log.h"
#include "NetConnection.h"

#include <chrono>

namespace Nyx::Game
{
	EditorLinkLayer::EditorLinkLayer(uint16_t editorPort, std::shared_ptr<Nyx::Engine::EditorLinkLogSink> logSink,
		std::filesystem::path recordingPath)
		: EditorPort(editorPort)
		, LogSink(std::move(logSink))
		, RecordingPath(std::move(recordingPath))
	{
	}

	void EditorLinkLayer::OnAttach(Nyx::Engine::Application& application)
	{
		App = &application;

		if (!RecordingPath.empty())
		{
			if (Recorder.Open(RecordingPath, "NyxGame"))
			{
				LOG_INFO("Editor link: recording to '{0}'", RecordingPath.string());
			}
			else
			{
				LOG_WARNING("Editor link: can't record to '{0}'", RecordingPath.string());
			}
		}

		LOG_INFO("Editor link: connecting to the editor on port {0}", EditorPort);
		Link = std::make_unique<Nyx::Engine::EditorLink>(Nyx::Net::Connection::ConnectToLocalPort(EditorPort), "NyxGame",
			[this](Nyx::Engine::ELinkDirection direction, const Nyx::Net::Message& message)
			{
				Recorder.Record(direction, message);
			});
	}

	void EditorLinkLayer::OnDetach()
	{
		if (!Link)
		{
			return;
		}

		// The last log lines, e.g. why the game is closing, then wait (briefly) until the editor
		// has them, so they aren't lost when the game exits
		SendLogLines();
		Link->CloseGracefully("the game is closing", std::chrono::milliseconds(1000));
		EndLink();
	}

	void EditorLinkLayer::OnUpdate(float /*deltaTime*/)
	{
		if (!Link)
		{
			return;
		}

		Link->Update();

		while (std::optional<Nyx::Net::Message> message = Link->Receive())
		{
			switch (static_cast<Nyx::Engine::EEditorLinkMessage>(message->Type))
			{
			case Nyx::Engine::EEditorLinkMessage::Quit:
				// Like closing the window: the frame loop ends, and OnDetach says goodbye
				LOG_INFO("The editor asked the game to quit");
				App->RequestQuit();
				break;

			default:
				ApplyMessage(*message);
				break;
			}
		}

		// The link logged why it ended; the game runs on without it
		if (Link->GetState() == Nyx::Engine::EEditorLinkState::Closed)
		{
			EndLink();
			return;
		}

		SendLogLines();
	}

	void EditorLinkLayer::ApplyMessage(const Nyx::Net::Message& message)
	{
		if (!World)
		{
			LOG_WARNING("Editor link: skipped a message of type {0}, since the game has no world for it", message.Type);
			return;
		}

		// Logs why it ignores an edit
		const Nyx::Engine::ELiveEditResult result = Nyx::Engine::ApplyLiveEdit(message, *World, PostLoadContext);

		if (result == Nyx::Engine::ELiveEditResult::NotALiveEdit)
		{
			LOG_WARNING("Editor link: skipped a message of type {0}, which this game doesn't know", message.Type);
		}
	}

	void EditorLinkLayer::SendLogLines()
	{
		// Lines wait in the sink, which keeps a limited number, until the editor is connected and
		// reads what was sent before. Queued for sending, they would pile up without a limit.
		constexpr size_t MaxUnsentBytes = 1024 * 1024;
		if (!Link->IsConnected() || Link->GetUnsentSize() > MaxUnsentBytes)
		{
			return;
		}

		for (const Nyx::Engine::LogLineMessage& line : LogSink->TakeLines())
		{
			Link->Send(line);
		}
	}

	void EditorLinkLayer::EndLink()
	{
		Link.reset();

		// Nobody takes the lines anymore
		LogSink->Stop();
		Recorder.Close();
	}
}
