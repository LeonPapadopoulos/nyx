// Input recordings: a session recorded to a file and replayed from it decides every frame as the
// original did; a file cut off by a crash replays up to its last whole frame; NyxDump's printout;
// a recording and the editor link's stream watching one input system at once.
#include "FlyCameraInput.h"
#include "InputAsset.h"
#include "InputFilePrinter.h"
#include "InputRecipes.h"
#include "InputRecording.h"
#include "Log.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
	using namespace Nyx::Input;

	void Require(bool condition, const std::string& message)
	{
		if (!condition)
		{
			throw std::runtime_error(message);
		}
	}

	bool Contains(const std::string& text, const std::string& part)
	{
		return text.find(part) != std::string::npos;
	}

	constexpr uint64_t Ms = 1000;

	struct ActionFrame
	{
		std::vector<EActionPhase> Phases;
		std::vector<bool> Triggered;
		std::vector<glm::vec3> Values;
	};

	ActionFrame Snapshot(const InputSystem& input)
	{
		ActionFrame frame;
		for (uint32_t index = 0; index < static_cast<uint32_t>(input.GetActionCount()); ++index)
		{
			const ActionState& state = input.GetUser().Get(ActionHandle::Make(index, 1));
			frame.Phases.push_back(state.Phase);
			frame.Triggered.push_back(state.bTriggered);
			frame.Values.push_back(state.Value);
		}
		return frame;
	}

	// A game's input: the default asset with a tap/hold and a jump, the fly camera on, and a
	// keyboard, a mouse and a pad
	struct Game
	{
		VirtualInputBackend Backend;
		InputSystem Input;
		InputDeviceId Keyboard;
		InputDeviceId Mouse;
		InputDeviceId Pad;
		uint64_t NowUs = 1'000'000;
		std::unique_ptr<FlyCameraInput> Camera;

		Game()
		{
			Input.AddBackend(Backend);
			Keyboard = Backend.AddDevice(EDeviceKind::Keyboard, "Keyboard", NowUs);
			Mouse = Backend.AddDevice(EDeviceKind::Mouse, "Mouse", NowUs);
			Pad = Backend.AddDevice(EDeviceKind::Gamepad, "Pad", NowUs);
			InputAssetData asset = MakeDefaultInputAsset();
			AddRecipe(asset, 2, EInputRecipe::TapAndHold, "Use");
			Require(ApplyInputAsset(Input, asset).empty(), "The asset applies");
			Camera = std::make_unique<FlyCameraInput>(Input, FlyCameraInput::Settings{});
			Camera->SetActive(true, false, "Playing");
		}

		void Frame()
		{
			NowUs += 16 * Ms;
			Input.BeginFrame(NowUs);
			Input.EndFrame();
		}
	};

	// Presses, holds, a tap, the sticks, the mouse look, the focus; one entry per frame
	std::vector<std::function<void(Game&)>> MakeScript()
	{
		return {
			[](Game& g)
			{
				g.Backend.Press(g.Keyboard, Key(EKey::W), g.NowUs + 1 * Ms);
			},
			[](Game& g)
			{
				g.Backend.Press(g.Pad, Gamepad(EGamepadControl::FaceWest), g.NowUs + 1 * Ms);
			},
			[](Game&)
			{
			},
			[](Game& g)
			{
				g.Backend.SetValue(g.Pad, Gamepad(EGamepadControl::RightStick), glm::vec3(0.95f, 0.2f, 0.0f), g.NowUs + 2 * Ms);
			},
			[](Game& g)
			{
				g.Backend.Release(g.Keyboard, Key(EKey::W), g.NowUs + 1 * Ms);
			},
			[](Game& g)
			{
				g.Backend.Press(g.Keyboard, Key(EKey::E), g.NowUs + 1 * Ms);
				g.Backend.Release(g.Keyboard, Key(EKey::E), g.NowUs + 5 * Ms);
			},
			[](Game& g)
			{
				g.Camera->SetActive(true, true, "Looking");
			},
			[](Game& g)
			{
				g.Backend.SetValue(g.Mouse, Mouse(EMouseControl::Delta), glm::vec3(20.0f, -6.0f, 0.0f), g.NowUs + 1 * Ms);
			},
			[](Game& g)
			{
				g.Backend.SetFocus(false, g.NowUs + 1 * Ms);
			},
			[](Game& g)
			{
				g.Backend.SetFocus(true, g.NowUs + 1 * Ms);
			},
			[](Game& g)
			{
				g.Backend.Release(g.Pad, Gamepad(EGamepadControl::FaceWest), g.NowUs + 1 * Ms);
			},
		};
	}

	std::filesystem::path TempPath(const char* name)
	{
		return std::filesystem::temp_directory_path() / name;
	}

	// Records the script; returns the actions' states of every frame
	std::vector<ActionFrame> Record(const std::filesystem::path& path, size_t extraFrames = 20)
	{
		Game game;
		game.Frame();

		InputRecordingSession recording;
		Require(recording.Start(game.Input, path, "NyxGame"), "The recording starts");
		std::vector<ActionFrame> frames;
		const std::vector<std::function<void(Game&)>> script = MakeScript();
		for (size_t frame = 0; frame < script.size() + extraFrames; ++frame)
		{
			if (frame < script.size())
			{
				script[frame](game);
			}
			game.Frame();
			recording.Update();
			frames.push_back(Snapshot(game.Input));
		}
		Require(recording.GetRecorder().GetFrameCount() == frames.size(), "A frame record per frame");
		recording.Stop();
		return frames;
	}

	// Replays the recording; the states of every frame
	std::vector<ActionFrame> Replay(const InputRecording& recording)
	{
		InputReplica replica;
		std::vector<ActionFrame> frames;
		for (const InputStreamRecord& record : recording.Records)
		{
			Require(replica.Apply(record), "Every record replays");
			if (!record.bDefinitions)
			{
				frames.push_back(Snapshot(replica.GetInput()));
			}
		}
		return frames;
	}

	void TestReplayGivesTheSamePhases()
	{
		const std::filesystem::path path = TempPath("NyxInputRecordingTests.nyxinputrec");
		const std::vector<ActionFrame> original = Record(path);

		std::string problem;
		const std::optional<InputRecording> recording = LoadInputRecording(path, &problem);
		Require(recording.has_value(), "The recording loads: " + problem);
		Require(recording->ProgramName == "NyxGame" && recording->StartTimeMs > 0, "With whose and when");
		Require(recording->Records.front().bDefinitions, "Starting with the definitions");

		const std::vector<ActionFrame> replayed = Replay(*recording);
		Require(replayed.size() == original.size(), "As many frames");
		for (size_t frame = 1; frame < original.size(); ++frame)
		{
			for (size_t action = 0; action < original[frame].Phases.size(); ++action)
			{
				const std::string at = "action " + std::to_string(action) + ", frame " + std::to_string(frame);
				Require(original[frame].Phases[action] == replayed[frame].Phases[action], "The same phase: " + at);
				Require(original[frame].Triggered[action] == replayed[frame].Triggered[action], "Fired alike: " + at);
				Require(glm::length(original[frame].Values[action] - replayed[frame].Values[action]) < 1e-5f, "The same value: " + at);
			}
		}

		// NyxDump
		std::string text;
		Require(Nyx::PrintInputRecording(path, text), "It prints");
		Require(Contains(text, "Of NyxGame, starting") && Contains(text, std::to_string(original.size()) + " frames."), "The header");
		Require(Contains(text, "Definitions: 10 actions"), "The definitions");
		Require(Contains(text, "Frame 1 at 0.016 s"), "Frames with their times");
		Require(Contains(text, "Keyboard/W down"), "Events");
		Require(Contains(text, "stack (lowest first): FlyCamera, FlyCameraMouseLook"), "The stack when it changed");
		Require(Contains(text, "focus lost"), "The focus");

		// Cut off in the middle of the last frame, as by a crash
		const uintmax_t size = std::filesystem::file_size(path);
		std::filesystem::resize_file(path, size - 5);
		const std::optional<InputRecording> cut = LoadInputRecording(path, &problem);
		Require(cut && cut->GetFrameCount() == original.size() - 1, "A cut off recording loads up to its last whole frame");
		Require(Replay(*cut).size() == original.size() - 1, "And replays");
		std::filesystem::remove(path);
	}

	// --input-replay: the recording plays into a game's own input system instead of its devices,
	// and decides as it did; the devices are ignored while it plays, and count again after it
	void TestReplayIntoAGame()
	{
		const std::filesystem::path path = TempPath("NyxInputRecordingTestsReplay.nyxinputrec");
		const std::vector<ActionFrame> original = Record(path, 10);
		std::optional<InputRecording> recording = LoadInputRecording(path);
		Require(recording.has_value(), "The recording loads");

		// A game in another state: started later, its keyboard holding A
		Game game;
		game.NowUs = 50'000'000;
		game.Backend.Press(game.Keyboard, Key(EKey::A), game.NowUs + 1);
		game.Frame();
		game.Input.StartReplay(std::move(*recording));
		Require(game.Input.IsReplaying() && game.Input.GetReplayFrameCount() == original.size(), "Replaying");

		const std::vector<std::function<void(Game&)>> script = MakeScript();
		for (size_t frame = 0; frame < original.size(); ++frame)
		{
			// The game's code does as it did (the mouse look on at frame 6); its devices do something else
			if (frame == 6)
			{
				game.Camera->SetActive(true, true, "Looking");
			}
			game.Backend.Press(game.Keyboard, Key(EKey::D), game.NowUs + 1);
			game.Backend.SetValue(game.Pad, Gamepad(EGamepadControl::LeftStick), glm::vec3(-1.0f, 0.0f, 0.0f), game.NowUs + 1);
			game.Frame();

			const ActionFrame replayed = Snapshot(game.Input);
			for (size_t action = 0; action < replayed.Phases.size() && frame > 0; ++action)
			{
				const std::string at = "action " + std::to_string(action) + ", frame " + std::to_string(frame);
				Require(original[frame].Phases[action] == replayed.Phases[action], "The same phase: " + at);
				Require(glm::length(original[frame].Values[action] - replayed.Values[action]) < 1e-5f, "The same value: " + at);
			}
		}
		Require(game.Input.GetReplayedFrames() == original.size(), "Every frame replayed");

		// The end: the recorded devices go, the game's own count again
		game.Frame();
		Require(!game.Input.IsReplaying(), "The replay ended");
		game.Backend.Release(game.Keyboard, Key(EKey::D), game.NowUs + 1);
		game.Backend.Press(game.Keyboard, Key(EKey::D), game.NowUs + 2);
		game.Frame();
		const ActionHandle move = game.Input.FindAction("Move");
		Require(game.Input.GetUser().Get(move).Vec3().x > 0.5f, "D moves right again");
		for (const InputFrame::DeviceState& device : game.Input.GetFrame().GetDevices())
		{
			Require(device.Info.Id.Value < 0x40000000u, "No recorded device is left: " + device.Info.Name);
		}
		std::filesystem::remove(path);
	}

	// The editor's Replay: at the recorded pace, paused, stepped, seeked
	void TestPlayback()
	{
		const std::filesystem::path path = TempPath("NyxInputRecordingTestsPlayback.nyxinputrec");
		const std::vector<ActionFrame> original = Record(path, 10);

		InputPlayback playback;
		Require(playback.Load(path) && playback.GetFrameCount() == original.size(), "It loads");
		Require(playback.GetFrame() == 0 && !playback.IsPlaying(), "At the start, paused");
		Require(std::abs(playback.GetSeconds(3) - 0.048f) < 1e-4f, "Frames 16 ms apart");

		playback.SetPlaying(true);
		playback.Update(0.040f);
		Require(playback.GetFrame() == 3, "40 ms: frames at 0, 16 and 32 ms, " + std::to_string(playback.GetFrame()));
		playback.Step();
		Require(!playback.IsPlaying() && playback.GetFrame() == 4, "Step pauses and goes one frame on");
		playback.Update(1.0f);
		Require(playback.GetFrame() == 4, "Paused, time doesn't move it");

		// Seeking replays from the start: the replica as after that many frames
		playback.SeekTo(8);
		const InputSystem& replica = playback.GetReplica().GetInput();
		for (uint32_t action = 0; action < static_cast<uint32_t>(replica.GetActionCount()); ++action)
		{
			Require(replica.GetUser().Get(ActionHandle::Make(action, 1)).Phase == original[7].Phases[action], "Seeked to frame 8");
		}
		Require(playback.GetReplica().GetHistory().GetFrames().size() == 8, "Its timeline has those frames");

		playback.SetPlaying(true);
		playback.Update(10.0f);
		Require(playback.IsAtEnd() && !playback.IsPlaying(), "Played to the end, it stops");
		playback.SetPlaying(true);
		Require(playback.GetFrame() == 0 && playback.IsPlaying(), "Play at the end starts over");
		std::filesystem::remove(path);
	}

	// Replay through A and B: the same recording, other tuning
	void TestPlaybackThroughOtherTuning()
	{
		const std::filesystem::path path = TempPath("NyxInputRecordingTestsTuning.nyxinputrec");
		Record(path, 10);

		// In the script, the right stick turns at 0.95 from frame 3 on, the mouse from frame 7
		InputPlayback recorded;
		Require(recorded.Load(path), "It loads");
		recorded.SeekTo(6);
		const ActionHandle look = recorded.GetReplica().GetInput().FindAction("Look");
		Require(glm::length(recorded.GetReplica().GetInput().GetUser().Get(look).Value) > 0.0f, "As recorded, the stick turns");

		// Through tuning whose dead zone takes it all
		InputAssetData deadStick = MakeDefaultInputAsset();
		for (InputBindingData& binding : deadStick.Contexts[2].Bindings)
		{
			if (binding.Control == "Gamepad/RightStick")
			{
				binding.Pipeline.DeadZone.Inner = 0.98f;
				binding.Pipeline.DeadZone.Outer = 0.99f;
			}
		}
		InputPlayback tuned;
		Require(tuned.Load(path), "It loads again");
		tuned.SeekTo(6);
		tuned.SetTuning(deadStick);
		Require(tuned.GetFrame() == 6, "Still at frame 6");
		Require(glm::length(tuned.GetReplica().GetInput().GetUser().Get(look).Value) == 0.0f, "Through the other tuning, it doesn't");
		const InputContextDef& fly = tuned.GetReplica().GetInput().GetContext(tuned.GetReplica().GetInput().FindContext("FlyCamera"));
		Require(fly.Bindings[4].Pipeline.DeadZone.Inner == 0.98f, "The other tuning is in the replica");

		tuned.SetTuning(std::nullopt);
		Require(glm::length(tuned.GetReplica().GetInput().GetUser().Get(look).Value) > 0.0f, "Back to the recorded tuning");
		std::filesystem::remove(path);
	}

	void TestNotARecording()
	{
		const std::filesystem::path path = TempPath("NyxInputRecordingTests.txt");
		std::ofstream(path) << "hello";
		std::string problem;
		Require(!LoadInputRecording(path, &problem) && Contains(problem, "is not an input recording"), "Says so: " + problem);
		std::string text;
		Require(!Nyx::PrintInputRecording(path, text) && Contains(text, "is not an input recording"), "NyxDump too");
		std::filesystem::remove(path);
	}

	void TestTwoWatchers()
	{
		Game game;
		InputStreamWriter link(game.Input);
		link.Start();
		const std::filesystem::path path = TempPath("NyxInputRecordingTestsTwo.nyxinputrec");
		InputRecordingSession recording;
		Require(recording.Start(game.Input, path, "NyxGame"), "The recording starts");
		for (int frame = 0; frame < 5; ++frame)
		{
			game.Frame();
			recording.Update();
		}
		Require(link.TakeRecords().size() == 6, "The link's stream: the definitions and five frames");
		Require(recording.GetRecorder().GetFrameCount() == 5, "And the recording's, both");

		recording.Stop();
		game.Frame();
		Require(link.TakeRecords().size() == 1, "Stopping one leaves the other");
		std::filesystem::remove(path);
	}
}

int main()
{
	Nyx::Logger::Get().Init();

	try
	{
		TestReplayGivesTheSamePhases();
		TestReplayIntoAGame();
		TestPlayback();
		TestPlaybackThroughOtherTuning();
		TestNotARecording();
		TestTwoWatchers();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Input recording test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All input recording tests passed.\n";
	return 0;
}
