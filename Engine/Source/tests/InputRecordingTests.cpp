// Input recordings: a session recorded to a file and replayed from it decides every frame as the
// original did; a file cut off by a crash replays up to its last whole frame; NyxDump's printout;
// a recording and the editor link's stream watching one input system at once.
#include "FlyCameraInput.h"
#include "InputAsset.h"
#include "InputFilePrinter.h"
#include "InputRecipes.h"
#include "InputRecording.h"
#include "Log.h"

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
		Require(Contains(text, "Definitions: 7 actions"), "The definitions");
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
