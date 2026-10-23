// Live tuning, both ends in one program: the editor's input asset as a message, put into a game's
// input system with the player's settings over it while a button is held, and pads driving a game
// without the focus.
#include "EditorLinkMessageText.h"
#include "EditorLinkMessages.h"
#include "InputSystem.h"
#include "LiveInput.h"
#include "Log.h"
#include "VirtualInputBackend.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
	using namespace Nyx;
	using namespace Nyx::Input;

	void Require(bool condition, const std::string& message)
	{
		if (!condition)
		{
			throw std::runtime_error(message);
		}
	}

	bool Near(float a, float b)
	{
		return std::abs(a - b) <= 1e-4f;
	}

	constexpr uint64_t Ms = 1000;

	// The game's side: an input system with a virtual pad, and frames 16 ms apart
	struct Game
	{
		VirtualInputBackend Backend;
		InputSystem Input;
		InputDeviceId Pad;
		uint64_t NowUs = 1'000'000;

		Game()
		{
			Input.AddBackend(Backend);
			Pad = Backend.AddDevice(EDeviceKind::Gamepad, "Pad", NowUs);
			Frame();
		}

		void Frame()
		{
			NowUs += 16 * Ms;
			Input.BeginFrame(NowUs);
			Input.EndFrame();
		}
	};

	// One context, "Play": the left stick on Move, and a hold of the left face button on Reload
	InputAssetData MakeAsset()
	{
		InputAssetData asset;
		asset.Actions = { { "Move", EActionValue::Axis2D }, { "Reload", EActionValue::Button } };
		InputContextData play;
		play.Name = "Play";
		InputBindingData stick;
		stick.Action = "Move";
		stick.Control = "Gamepad/LeftStick";
		stick.Pipeline.DeadZone = { .bEnabled = true, .Shape = EDeadZoneShape::ScaledRadial, .Inner = 0.1f };
		play.Bindings.push_back(stick);
		InputBindingData hold;
		hold.Action = "Reload";
		hold.Control = "Gamepad/FaceWest";
		hold.Trigger.Kind = ETriggerKind::Hold;
		hold.Trigger.HoldSeconds = 0.5f;
		play.Bindings.push_back(hold);
		asset.Contexts.push_back(play);
		return asset;
	}

	void TestAssetFromTheEditor()
	{
		// The game started with the asset from the file and the player's sensitivity over it
		Game game;
		InputPlayerSettingsData player;
		player.Overrides.push_back({ .Action = "Move", .Field = "Pipeline.Sensitivity.bEnabled", .Value = 1.0f });
		player.Overrides.push_back({ .Action = "Move", .Field = "Pipeline.Sensitivity.Scale", .Value = 2.0f });
		InputAssetData asset = MakeAsset();
		Require(ApplyInputAsset(game.Input, asset, &player).empty(), "The game's asset applies");
		game.Input.GetUser().SetContextActive(game.Input.FindContext("Play"), true);
		game.Backend.Press(game.Pad, Gamepad(EGamepadControl::FaceWest), game.NowUs + 1 * Ms);
		for (int frame = 0; frame < 20; ++frame)
		{
			game.Frame();
		}
		const ActionHandle reload = game.Input.FindAction("Reload");
		Require(!game.Input.GetUser().Get(reload).bTriggered, "Held 320 ms of 500");

		// The editor drags the hold time and the dead zone: a message each frame
		asset.Contexts[0].Bindings[1].Trigger.HoldSeconds = 0.3f;
		asset.Contexts[0].Bindings[0].Pipeline.DeadZone.Inner = 0.25f;
		const Nyx::Net::Message message = MakeNetMessage(MakeSetInputAssetMessage(asset));
		std::vector<InputAssetProblem> problems;
		Require(ApplyLiveInput(message, game.Input, &player, &problems) == ELiveInputResult::Applied && problems.empty(), "The game applies it");

		const InputBinding& stick = game.Input.GetContext(game.Input.FindContext("Play")).Bindings[0];
		Require(Near(stick.Pipeline.DeadZone.Inner, 0.25f), "The editor's dead zone");
		Require(stick.Pipeline.Sensitivity.bEnabled && Near(stick.Pipeline.Sensitivity.Scale.x, 2.0f), "With the player's sensitivity over it");

		game.Frame();
		Require(game.Input.GetUser().Get(reload).bTriggered, "The held button kept its time: past the new hold time, it fires");

		// What the Game Link window shows
		const EditorLinkMessageText text = DescribeEditorLinkMessage(message);
		Require(text.Name == "SetInputAsset" && text.Summary == "2 actions, 1 contexts, 0 pad profiles", "Described: " + text.Summary);
		Require(text.Details.find("HoldSeconds: 0.3") != std::string::npos, "With the asset printed");

		// Something the game can't use: told, and the rest applied
		asset.Contexts[0].Bindings[0].Control = "Gamepad/Elbow";
		Require(ApplyLiveInput(MakeNetMessage(MakeSetInputAssetMessage(asset)), game.Input, &player, &problems) == ELiveInputResult::Applied,
			"Applied without the binding it can't use");
		Require(problems.size() == 1 && problems[0].Where == "Play, binding 0", "And says which");

		// Damaged
		Nyx::Net::Message damaged = message;
		damaged.Payload.resize(damaged.Payload.size() / 2);
		Require(ApplyLiveInput(damaged, game.Input, &player) == ELiveInputResult::Unreadable, "A damaged asset is unreadable");
		Require(ApplyLiveInput(MakeNetMessage(QuitMessage{}), game.Input, &player) == ELiveInputResult::NotLiveInput, "Other messages aren't");
	}

	void TestPadsInTheBackground()
	{
		Game game;
		Require(ApplyInputAsset(game.Input, MakeAsset()).empty(), "The asset applies");
		game.Input.GetUser().SetContextActive(game.Input.FindContext("Play"), true);
		const InputDeviceId keyboard = game.Backend.AddDevice(EDeviceKind::Keyboard, "Keyboard", game.NowUs + 1);
		game.Backend.SetFocus(false, game.NowUs + 2);
		game.Frame();

		game.Backend.SetValue(game.Pad, Gamepad(EGamepadControl::LeftStick), glm::vec3(0.0f, 0.8f, 0.0f), game.NowUs + 1 * Ms);
		game.Frame();
		const ActionHandle move = game.Input.FindAction("Move");
		Require(Near(game.Input.GetUser().Get(move).Vec2().y, 0.0f), "Without the focus, the pad does nothing");

		const Nyx::Net::Message message = MakeNetMessage(SetInputBackgroundMessage{ true });
		Require(ApplyLiveInput(message, game.Input, nullptr) == ELiveInputResult::Applied && game.Input.AreGamepadsInBackground(), "Applied");
		Require(DescribeEditorLinkMessage(message).Summary == "pads drive the game in the background", "Described");
		game.Frame();
		Require(game.Input.GetUser().Get(move).Vec2().y > 0.5f, "Now the pad drives it");

		// The keyboard and the mouse still belong to the window with the focus
		const std::vector<InputUser::StackEntry> stack = game.Input.GetUser().GetStack();
		Require(!stack.empty() && stack[0].Context == game.Input.GetUnfocusedContext(), "Unfocused is still on top");
		Require(stack[0].Activation.TakesDevices == (DeviceBit(EDeviceKind::Keyboard) | DeviceBit(EDeviceKind::Mouse)),
			"Taking the keyboard and the mouse, not the pads");
		(void)keyboard;

		ApplyLiveInput(MakeNetMessage(SetInputBackgroundMessage{ false }), game.Input, nullptr);
		game.Frame();
		Require(Near(game.Input.GetUser().Get(move).Vec2().y, 0.0f), "Off again");
	}
}

int main()
{
	Nyx::Logger::Get().Init();

	try
	{
		TestAssetFromTheEditor();
		TestPadsInTheBackground();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Live input test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All live input tests passed.\n";
	return 0;
}
