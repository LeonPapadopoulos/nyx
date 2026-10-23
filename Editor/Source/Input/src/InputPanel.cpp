#include "InputPanel.h"

#include "FileDialogs.h"
#include "GameInstance.h"
#include "ImGuiSource.h"
#include "InputAssetDocument.h"
#include "InputSystem.h"
#include "Log.h"
#include "PlaySession.h"
#include "UIWidgets.h"

#include <imgui.h>

#include <array>

namespace Nyx::Editor
{
	namespace
	{
		GameInstance* FindGame(PlaySession* games, const std::string& name)
		{
			if (!games || name.empty())
			{
				return nullptr;
			}
			for (GameInstance* game : games->GetGames())
			{
				if (game->GetName() == name)
				{
					return game;
				}
			}
			return nullptr;
		}
	}

	void InputPanel::Record(const Nyx::Input::InputSystem& input)
	{
		History.Record(input);
		EditorRecording.Update();

		// A game that ended can't be shown; the editor's input again
		if (!ShownGame.empty() && !FindGame(Games, ShownGame))
		{
			ShownGame.clear();
		}

		// Only the game shown streams its input, and only while the window is open, or it is recorded
		if (Games)
		{
			for (GameInstance* game : Games->GetGames())
			{
				game->SetInputSubscribed((bOpen && game->GetName() == ShownGame) || game->IsRecordingInput());
			}
		}
	}

	bool InputPanel::OpenRecording(const std::filesystem::path& path)
	{
		auto playback = std::make_unique<Nyx::Input::InputPlayback>();
		std::string problem;
		if (!playback->Load(path, &problem))
		{
			LOG_WARNING("Input: can't replay: {0}", problem);
			return false;
		}

		Playback = std::move(playback);
		Playback->SetPlaying(true);
		bShowPlayback = true;
		ShownGame.clear();
		LOG_INFO("Input: replaying '{0}', {1} frames of {2}'s input", path.string(), Playback->GetFrameCount(), Playback->GetRecording().ProgramName);
		return true;
	}

	void InputPanel::DrawPlaybackControls()
	{
		Nyx::Input::InputPlayback& playback = *Playback;
		if (NYX_UI(Nyx::UI::Button(playback.IsPlaying() ? "Pause###PlayPause" : "Play###PlayPause")))
		{
			playback.SetPlaying(!playback.IsPlaying());
		}
		ImGui::SameLine();
		if (NYX_UI(Nyx::UI::Button("Step")))
		{
			playback.Step();
		}
		ImGui::SetItemTooltip("One frame on; the Timeline shows its traces when hovered");
		ImGui::SameLine();
		if (NYX_UI(Nyx::UI::Button("Start over")))
		{
			playback.SeekTo(0);
			playback.SetPlaying(true);
		}

		// Seeking replays from the start up to the frame
		ImGui::SameLine();
		float frame = static_cast<float>(playback.GetFrame());
		const std::string format = "%.0f of " + std::to_string(playback.GetFrameCount()) + " frames";
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
		if (NYX_UI(Nyx::UI::SliderFloat("##Frame", &frame, 0.0f, static_cast<float>(playback.GetFrameCount()), format.c_str())))
		{
			playback.SetPlaying(false);
			playback.SeekTo(static_cast<size_t>(frame + 0.5f));
		}
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("%.2f s", playback.GetSeconds(playback.GetFrame() > 0 ? playback.GetFrame() - 1 : 0));
		ImGui::SameLine();
		if (NYX_UI(Nyx::UI::Button("Close")))
		{
			Playback.reset();
			bShowPlayback = false;
		}
	}

	void InputPanel::DrawRecordButton(Nyx::Input::InputSystem& editorInput)
	{
		GameInstance* game = FindGame(Games, ShownGame);
		const bool bRecording = game ? game->IsRecordingInput() : EditorRecording.IsRecording();
		const uint64_t frames = game ? game->GetInputRecorder().GetFrameCount() : EditorRecording.GetRecorder().GetFrameCount();

		ImGui::SameLine();
		const std::string label = bRecording ? "Stop recording (" + std::to_string(frames) + " frames)###Record" : std::string("Record###Record");
		if (NYX_UI(Nyx::UI::Button(label.c_str(), glm::vec2(0.0f), bRecording ? Nyx::UI::EButtonTone::Stop : Nyx::UI::EButtonTone::Neutral)))
		{
			if (bRecording)
			{
				const std::filesystem::path path = game ? game->GetInputRecorder().GetPath() : EditorRecording.GetRecorder().GetPath();
				if (game)
				{
					game->StopInputRecording();
				}
				else
				{
					EditorRecording.Stop();
				}
				LastRecording = path.filename().string();
				LOG_INFO("Input: recorded {0} frames to '{1}'", frames, path.string());
			}
			else
			{
				const std::filesystem::path newPath = Nyx::Input::MakeInputRecordingPath(game ? game->GetName() : std::string("Editor"));
				const bool bStarted = game ? game->StartInputRecording(newPath) : EditorRecording.Start(editorInput, newPath, "NyxEditor");
				if (!bStarted)
				{
					LOG_WARNING("Input: can't record to '{0}'", newPath.string());
				}
				LastRecording.clear();
			}
		}
		ImGui::SetItemTooltip("Records the input shown to a .nyxinputrec file in %s", Nyx::Input::GetInputRecordingFolder().string().c_str());
		if (!bRecording && !LastRecording.empty())
		{
			ImGui::SameLine();
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("Saved %s", LastRecording.c_str());
		}
	}

	void InputPanel::Draw(Nyx::Input::InputSystem& input)
	{
		if (!bOpen)
		{
			return;
		}

		ImGui::SetNextWindowSize(ImVec2(720.0f, 640.0f), ImGuiCond_FirstUseEver);
		if (Nyx::UI::Begin("Input", &bOpen))
		{
			// Whose input: the editor's, a running game's, or a recording's
			std::vector<std::string> sources = { "Editor" };
			int selected = 0;
			if (Games)
			{
				for (GameInstance* game : Games->GetGames())
				{
					if (game->GetName() == ShownGame && !bShowPlayback)
					{
						selected = static_cast<int>(sources.size());
					}
					sources.push_back(game->GetName());
				}
			}
			const int recordingSource = Playback ? static_cast<int>(sources.size()) : -1;
			if (Playback)
			{
				selected = bShowPlayback ? recordingSource : selected;
				sources.push_back("Recording: " + Playback->GetPath().filename().string());
			}
			std::vector<const char*> names;
			for (const std::string& source : sources)
			{
				names.push_back(source.c_str());
			}
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
			if (NYX_UI(Nyx::UI::Combo("##InputSource", &selected, names)))
			{
				bShowPlayback = selected == recordingSource;
				ShownGame = selected == 0 || bShowPlayback ? std::string() : sources[static_cast<size_t>(selected)];
			}
			ImGui::SetItemTooltip("Whose input to show: the editor's, a game's started with Play, or a recording's");
			if (!bShowPlayback)
			{
				DrawRecordButton(input);
			}
			ImGui::SameLine();
			if (NYX_UI(Nyx::UI::Button("Replay...")))
			{
				if (const std::optional<std::filesystem::path> path = ChooseInputRecording(Nyx::Input::GetInputRecordingFolder()))
				{
					OpenRecording(*path);
				}
			}
			ImGui::SetItemTooltip("Plays a recording (.nyxinputrec) through its own actions and contexts, paused and stepped at will");

			Nyx::Input::InputSystem* shown = &input;
			Nyx::Input::InputHistory* history = &History;
			if (bShowPlayback && Playback)
			{
				DrawPlaybackControls();
			}
			// Close, in the controls, ends it
			if (bShowPlayback && Playback)
			{
				Playback->Update(ImGui::GetIO().DeltaTime);
				shown = &Playback->GetReplica().GetInput();
				history = &Playback->GetReplica().GetHistory();
			}
			else if (GameInstance* game = FindGame(Games, ShownGame))
			{
				Nyx::Input::InputReplica* replica = game->GetInputReplica();
				if (!replica || replica->GetFrameCount() == 0)
				{
					ImGui::SameLine();
					ImGui::AlignTextToFramePadding();
					ImGui::TextDisabled("Waiting for %s's input...", ShownGame.c_str());
					ImGui::End();
					return;
				}
				shown = &replica->GetInput();
				history = &replica->GetHistory();
			}
			View.SetCanSendOutput(shown == &input);

			if (Document && Transactions)
			{
				// With what the input shown knows: its stack, the actions its code took
				Document->RefreshChecks(shown);
				const std::array<Nyx::Input::InputDebugView::ExtraTab, 2> tabs = { {
					{ "Tuning",
						[&]()
						{
							Tuning.DrawTuning(*Document, *Transactions, *shown);
						} },
					{ "Checks",
						[&]()
						{
							Tuning.DrawChecks(*Document, *Transactions, *shown);
						} },
				} };
				View.Draw(*shown, *history, tabs);
				Tuning.PreviewPendingEdits();
				if (Tuning.TakeShowTuningRequest())
				{
					View.SelectExtraTab("Tuning");
				}
			}
			else
			{
				View.Draw(*shown, *history);
			}
		}
		ImGui::End();
	}
}
