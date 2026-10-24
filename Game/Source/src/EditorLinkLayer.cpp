#include "EditorLinkLayer.h"
#include "Renderer.h"

#include "Application.h"
#include "LiveEdits.h"
#include "LiveInput.h"
#include "ReflectedArchiveSerializer.h"
#include "Log.h"
#include "NetConnection.h"
#include "RendererLiveEdits.h"

#include <chrono>

namespace
{
	// The layer whose link a crash report goes over; there is one per game
	Nyx::Game::EditorLinkLayer* GCrashReportLink = nullptr;
}

namespace Nyx::Game
{
	void EditorLinkLayer::SendCrashReport(const Nyx::CrashReport& report)
	{
		EditorLinkLayer* layer = GCrashReportLink;
		if (!layer || !layer->Link || !layer->Link->IsConnected())
		{
			return;
		}

		// The log lines first: a failed ASSERT says which in the log
		layer->SendLogLines();
		layer->Link->Send(Nyx::CrashMessage{ report });
		layer->Link->CloseGracefully("the game crashed", std::chrono::milliseconds(1000));
	}

	EditorLinkLayer::EditorLinkLayer(uint16_t editorPort, std::shared_ptr<Nyx::EditorLinkLogSink> logSink,
		std::filesystem::path recordingPath)
		: EditorPort(editorPort)
		, LogSink(std::move(logSink))
		, RecordingPath(std::move(recordingPath))
	{
	}

	void EditorLinkLayer::OnAttach(Nyx::Application& application)
	{
		App = &application;
		// The editor compiles, once for all its games, and sends what it compiled
		App->GetRenderer().FollowEditorShaders();

		const std::filesystem::path playerPath = Nyx::Input::GetPlayerInputSettingsPath();
		std::error_code error;
		if (!playerPath.empty() && std::filesystem::exists(playerPath, error))
		{
			Nyx::ReadWarnings warnings;
			PlayerInputSettings = Nyx::Input::LoadPlayerSettings(playerPath, warnings);
		}

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

		GCrashReportLink = this;
		InputStream = std::make_unique<Nyx::Input::InputStreamWriter>(application.GetInput());

		LOG_INFO("Editor link: connecting to the editor on port {0}", EditorPort);
		Link = std::make_unique<Nyx::EditorLink>(Nyx::Net::Connection::ConnectToLocalPort(EditorPort), "NyxGame",
			[this](Nyx::ELinkDirection direction, const Nyx::Net::Message& message)
			{
				Recorder.Record(direction, message);
			});
	}

	void EditorLinkLayer::OnDetach()
	{
		// It observes the application's input system, which may go before this layer
		InputStream.reset();

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
			switch (static_cast<Nyx::EEditorLinkMessage>(message->Type))
			{
			case Nyx::EEditorLinkMessage::Quit:
				// Like closing the window: the frame loop ends, and OnDetach says goodbye
				LOG_INFO("The editor asked the game to quit");
				App->RequestQuit();
				break;

			case Nyx::EEditorLinkMessage::InputSubscribe:
			{
				// From the next frame on, starting with a snapshot of the devices and the stack
				Nyx::InputSubscribeMessage subscribe;
				if (Nyx::ReadNetMessage(*message, subscribe) && subscribe.bSubscribed)
				{
					InputStream->Start();
				}
				else
				{
					InputStream->Stop();
				}
				break;
			}

			case Nyx::EEditorLinkMessage::InspectEntity:
				AnswerInspection(*message);
				break;

			default:
				ApplyMessage(*message);
				break;
			}
		}

		// The link logged why it ended; the game runs on without it
		if (Link->GetState() == Nyx::EEditorLinkState::Closed)
		{
			EndLink();
			return;
		}

		SendLogLines();
		SendInputFrames();
	}

	void EditorLinkLayer::SendInputFrames()
	{
		for (Nyx::Input::InputStreamWriter::Record& record : InputStream->TakeRecords())
		{
			if (record.bDefinitions)
			{
				Link->Send(Nyx::InputDefinitionsMessage{ std::move(record.Bytes) });
			}
			else
			{
				Link->Send(Nyx::InputFrameMessage{ std::move(record.Bytes) });
			}
		}
	}

	void EditorLinkLayer::AnswerInspection(const Nyx::Net::Message& message)
	{
		Nyx::InspectEntityMessage request;
		if (!Nyx::ReadNetMessage(message, request))
		{
			LOG_WARNING("Editor link: an InspectEntity message can't be read");
			return;
		}

		const uint64_t step = App->GetSimulationClock().GetStepIndex();
		if (!World)
		{
			// Not found, which the editor says
			Link->Send(Nyx::InspectedEntityMessage{ .Request = request.Request, .Entity = request.Entity, .Step = step });
			return;
		}
		Link->Send(Nyx::MakeInspectedEntityMessage(*World, request, step));
	}

	void EditorLinkLayer::ApplyMessage(const Nyx::Net::Message& message)
	{
		// Pause and Step go to the simulation's clock
		if (Nyx::ApplySimulationControl(message, App->GetSimulationClock()) != Nyx::ELiveEditResult::NotALiveEdit)
		{
			return;
		}

		// The editor's shaders and material values go to the renderer
		if (Nyx::ApplyRendererEdit(message, App->GetRenderer()) != Nyx::ELiveEditResult::NotALiveEdit)
		{
			return;
		}

		// A reflected asset's values as the editor edits them, saved or not, e.g. a physics material's
		if (Nyx::ApplyAssetValues(message) != Nyx::ELiveEditResult::NotALiveEdit)
		{
			return;
		}

		// The input asset as the editor edits it, e.g. a dead zone being dragged
		std::vector<Nyx::Input::InputAssetProblem> problems;
		const Nyx::ELiveInputResult input =
			Nyx::ApplyLiveInput(message, App->GetInput(), PlayerInputSettings ? &*PlayerInputSettings : nullptr, &problems);
		if (input == Nyx::ELiveInputResult::NotLiveInput)
		{
			ApplyLiveEdit(message);
			return;
		}

		// A drag sends the asset every frame: log what can't be used when that changes
		const bool bAsset = message.Type == static_cast<uint16_t>(Nyx::EEditorLinkMessage::SetInputAsset);
		std::vector<std::string> texts;
		for (const Nyx::Input::InputAssetProblem& problem : problems)
		{
			texts.push_back(problem.Where + ": " + problem.What);
		}
		if (bAsset && input == Nyx::ELiveInputResult::Applied && texts != InputProblems)
		{
			for (const std::string& text : texts)
			{
				LOG_WARNING("Input from the editor: {0}", text);
			}
			InputProblems = std::move(texts);
		}
	}

	void EditorLinkLayer::ApplyLiveEdit(const Nyx::Net::Message& message)
	{
		if (!World)
		{
			LOG_WARNING("Editor link: skipped a message of type {0}, since the game has no world for it", message.Type);
			return;
		}

		// Logs why it ignores an edit
		const Nyx::ELiveEditResult result = Nyx::ApplyLiveEdit(message, *World, PostLoadContext);

		if (result == Nyx::ELiveEditResult::NotALiveEdit)
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

		for (const Nyx::LogLineMessage& line : LogSink->TakeLines())
		{
			Link->Send(line);
		}
	}

	void EditorLinkLayer::EndLink()
	{
		if (GCrashReportLink == this)
		{
			GCrashReportLink = nullptr;
		}

		Link.reset();
		if (InputStream)
		{
			InputStream->Stop();
		}

		// Nobody takes the lines anymore
		LogSink->Stop();
		Recorder.Close();
	}
}
