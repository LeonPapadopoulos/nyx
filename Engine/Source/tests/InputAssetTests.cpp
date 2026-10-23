// Input assets without a window: saving and reading back, problems named with where they are,
// tuning layers (asset, pad profile, player) and which one a value comes from, and the router
// using a pad model's variant of a binding while that model drives it.
#include "InputAsset.h"
#include "InputFilePrinter.h"
#include "InputSystem.h"
#include "Log.h"
#include "ReflectedArchiveSerializer.h"
#include "VirtualInputBackend.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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

	bool Near(float a, float b, float tolerance = 1e-4f)
	{
		return std::abs(a - b) <= tolerance;
	}

	constexpr uint64_t Ms = 1000;

	std::string ReadBytes(const std::filesystem::path& path)
	{
		std::ifstream file(path, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
	}

	bool HasProblem(const std::vector<InputAssetProblem>& problems, const std::string& where, const std::string& what)
	{
		for (const InputAssetProblem& problem : problems)
		{
			if (problem.Where == where && problem.What.find(what) != std::string::npos)
			{
				return true;
			}
		}
		return false;
	}

	// One context, "Walk", with the left stick on Move (0.15 inner dead zone) and W on Jump
	InputAssetData MakeWalkAsset()
	{
		InputAssetData asset;
		asset.Actions = { { "Move", EActionValue::Axis2D }, { "Jump", EActionValue::Button } };

		InputContextData walk;
		walk.Name = "Walk";
		walk.Priority = 10;

		InputBindingData stick;
		stick.Action = "Move";
		stick.Control = "Gamepad/LeftStick";
		stick.Pipeline.DeadZone = { .bEnabled = true, .Shape = EDeadZoneShape::ScaledRadial, .Inner = 0.15f };
		walk.Bindings.push_back(stick);

		InputBindingData jump;
		jump.Action = "Jump";
		jump.Control = "Keyboard/W";
		walk.Bindings.push_back(jump);

		asset.Contexts.push_back(walk);
		return asset;
	}

	TuningOverrideData StickDeadZone(float inner)
	{
		return { .Control = "Gamepad/LeftStick", .Field = "Pipeline.DeadZone.Inner", .Value = inner };
	}

	// An input system with a virtual pad, and frames 16 ms apart
	struct Rig
	{
		VirtualInputBackend Backend;
		InputSystem Input;
		InputDeviceId Pad;
		uint64_t NowUs = 1'000'000;

		Rig()
		{
			Input.AddBackend(Backend);
			Pad = Backend.AddDevice(EDeviceKind::Gamepad, "Pad", NowUs);
			Frame();
		}

		void SetModel(EGamepadModel model)
		{
			InputDeviceInfo info;
			info.Name = "Pad";
			info.Model = model;
			Backend.SetDeviceInfo(Pad, info, NowUs + 1);
		}

		void Frame()
		{
			NowUs += 16 * Ms;
			Input.BeginFrame(NowUs);
			Input.EndFrame();
		}
	};

	// ---------------------------------------------------------------------------------------
	// Files

	void TestRoundTrip()
	{
		const std::filesystem::path path = std::filesystem::temp_directory_path() / "NyxInputAssetTests.nyxinput";
		const std::filesystem::path again = std::filesystem::temp_directory_path() / "NyxInputAssetTestsAgain.nyxinput";

		InputAssetData asset = MakeDefaultInputAsset();
		asset.PadProfiles.push_back({ EGamepadModel::DualShock4, { StickDeadZone(0.25f) } });
		Require(SaveInputAsset(asset, path), "The asset saves");

		Nyx::ReadWarnings warnings;
		const std::optional<InputAssetData> loaded = LoadInputAsset(path, warnings);
		Require(loaded.has_value(), "And reads back");
		Require(loaded->Contexts.size() == 4 && loaded->Contexts[0].Name == "EditorFlyCamera", "With the editor's and the game's contexts");
		Require(loaded->Contexts[0].Bindings[1].SwizzleY == -1, "Negative swizzles stay");
		Require(loaded->PadProfiles.size() == 1 && loaded->PadProfiles[0].Model == EGamepadModel::DualShock4, "The profile's model stays");
		Require(Near(loaded->PadProfiles[0].Overrides[0].Value, 0.25f), "And its override");

		Require(SaveInputAsset(*loaded, again), "Saving it again");
		Require(ReadBytes(path) == ReadBytes(again), "Gives the same bytes");

		std::string text;
		Require(Nyx::PrintInputFile(path, text), "NyxDump can print it");
		Require(text.starts_with("Input asset, version 1"), "As an input asset: " + text.substr(0, 40));
		Require(text.find("Model: DualShock4") != std::string::npos, "Enums by name");

		Require(!LoadPlayerSettings(path, warnings).has_value(), "An asset isn't player settings");

		std::filesystem::remove(path);
		std::filesystem::remove(again);
	}

	// ---------------------------------------------------------------------------------------
	// Problems

	void TestProblemsSayWhere()
	{
		InputAssetData asset = MakeWalkAsset();
		InputContextData& walk = asset.Contexts[0];

		InputBindingData unknownControl;
		unknownControl.Action = "Jump";
		unknownControl.Control = "Gamepad/FaceNorthEast";
		walk.Bindings.push_back(unknownControl);

		InputBindingData unknownAction;
		unknownAction.Action = "Fly";
		unknownAction.Control = "Keyboard/F";
		walk.Bindings.push_back(unknownAction);

		InputBindingData chord;
		chord.Action = "Jump";
		chord.Control = "Keyboard/Space";
		chord.Trigger.Kind = ETriggerKind::Chord;
		chord.Trigger.ChordActionName = "Crouch";
		walk.Bindings.push_back(chord);

		asset.PadProfiles.push_back({ EGamepadModel::XboxOne, { { .Field = "Pipeline.DeadZone.Width", .Value = 1.0f } } });

		InputSystem input;
		const std::vector<InputAssetProblem> problems = ApplyInputAsset(input, asset);
		Require(HasProblem(problems, "Walk, binding 2", "\"Gamepad/FaceNorthEast\" isn't a control"), "An unknown control, where it is");
		Require(HasProblem(problems, "Walk, binding 3", "no action is called \"Fly\""), "An unknown action");
		Require(HasProblem(problems, "Walk, binding 4", "\"Crouch\" isn't an action"), "An unknown chord action");
		Require(HasProblem(problems, "Xbox One profile, override 0", "isn't a field of a binding"), "An unknown field, in the profile's layer");
		Require(problems.size() == 4, "Each once, though the override matches every binding: " + std::to_string(problems.size()));

		// The player's override is resolved for each model with a profile, and still reported once
		InputPlayerSettingsData player;
		player.Overrides.push_back({ .Field = "Trigger", .Value = 1.0f });
		InputSystem withPlayer;
		const std::vector<InputAssetProblem> playerProblems = ApplyInputAsset(withPlayer, asset, &player);
		Require(HasProblem(playerProblems, "Player settings, override 0", "is a Struct"), "A struct isn't a value");
		Require(playerProblems.size() == 5, "The player's once more: " + std::to_string(playerProblems.size()));

		const ContextHandle context = input.FindContext("Walk");
		Require(context.IsValid() && input.GetContext(context).Bindings.size() == 2, "The usable bindings are there, the others left out");
		Require(input.GetContext(context).ModelVariants.size() == 1 && input.GetContext(context).ModelVariants[0].Bindings.size() == 2,
			"The profile's variant has the same bindings");
	}

	// ---------------------------------------------------------------------------------------
	// Layers

	void TestLayersAndWhereValuesComeFrom()
	{
		InputAssetData asset = MakeWalkAsset();
		asset.PadProfiles.push_back({ EGamepadModel::DualSense, { StickDeadZone(0.25f) } });

		InputPlayerSettingsData player;
		const auto inner = [&](const InputPlayerSettingsData* withPlayer, EGamepadModel model)
		{
			const std::optional<LayeredValue> value = FindTuning(asset, withPlayer, 0, 0, "Pipeline.DeadZone.Inner", model);
			Require(value.has_value(), "The dead zone is a tuning value");
			return *value;
		};

		LayeredValue value = inner(nullptr, EGamepadModel::XboxOne);
		Require(Near(value.Value, 0.15f) && value.Layer == ETuningLayer::Asset, "Another model: the asset's");
		value = inner(nullptr, EGamepadModel::DualSense);
		Require(Near(value.Value, 0.25f) && value.Layer == ETuningLayer::PadProfile, "The DualSense: its profile's");

		player.Overrides.push_back({ .Context = "Walk", .Action = "Move", .Field = "Pipeline.DeadZone.Inner", .Value = 0.2f });
		value = inner(&player, EGamepadModel::DualSense);
		Require(Near(value.Value, 0.2f) && value.Layer == ETuningLayer::Player, "The player's over the profile");
		Require(std::string(GetTuningLayerName(value.Layer)) == "player", "Named for the Input window");

		Require(!FindTuning(asset, &player, 0, 0, "Pipeline.DeadZone", EGamepadModel::Unknown).has_value(), "A struct isn't a value");
		Require(!FindTuning(asset, &player, 0, 9, "Pipeline.DeadZone.Inner", EGamepadModel::Unknown).has_value(), "Nor a binding that isn't there");

		// Matching: the player's override names the action, so W's binding keeps its own
		const InputAssetData resolved = ResolveLayers(asset, &player, EGamepadModel::DualSense);
		Require(Near(resolved.Contexts[0].Bindings[0].Pipeline.DeadZone.Inner, 0.2f), "Resolved like FindTuning says");
		Require(Near(resolved.Contexts[0].Bindings[1].Pipeline.DeadZone.Inner, asset.Contexts[0].Bindings[1].Pipeline.DeadZone.Inner),
			"Jump's binding doesn't match");
	}

	void TestOverrideKinds()
	{
		InputBindingData binding;
		binding.Control = "Keyboard/Space";
		std::string problem;

		Require(ApplyOverride(binding, { .Field = "Pipeline.Sensitivity.bInvertY", .Value = 1.0f }, &problem) &&
				binding.Pipeline.Sensitivity.bInvertY,
			"Bools take 1");
		Require(ApplyOverride(binding, { .Field = "Trigger.Kind", .Value = static_cast<float>(ETriggerKind::Hold) }, &problem) &&
				binding.Trigger.Kind == ETriggerKind::Hold,
			"Enums take their number");
		Require(!ApplyOverride(binding, { .Field = "Trigger.Kind", .Value = 99.0f }, &problem) && binding.Trigger.Kind == ETriggerKind::Hold,
			"Not one no enumerator has");
		Require(problem.find("no enumerator has the number 99") != std::string::npos, "And says so: " + problem);
		Require(!ApplyOverride(binding, { .Field = "Control", .Value = 1.0f }, &problem), "Strings aren't tuning");
		Require(!ApplyOverride(binding, { .Field = "Trigger.Kind.Size", .Value = 1.0f }, &problem) && problem.find("has no fields") != std::string::npos,
			"Nor paths through values: " + problem);

		// A composite's override by one of its controls
		InputContextData context;
		context.Name = "Fly";
		InputBindingData keys;
		keys.Action = "Move";
		keys.Composite.Forward = "Keyboard/W";
		Require(MatchesOverride({ .Control = "Keyboard/W" }, context, keys), "A composite matches its controls");
		Require(!MatchesOverride({ .Control = "Keyboard/Q" }, context, keys), "Not others");
		Require(!MatchesOverride({ .Context = "Walk" }, context, keys), "Nor another context");
	}

	// ---------------------------------------------------------------------------------------
	// Applying

	void TestApplyKeepsHandlesAndState()
	{
		InputSystem input;
		const ActionHandle move = input.AddAction({ "Move", EActionValue::Axis2D });
		const ContextHandle walk = input.AddContext({ .Name = "Walk" });
		input.GetUser().SetContextActive(walk, true);

		InputPlayerSettingsData player;
		player.HapticsStrength = 0.5f;
		Require(ApplyInputAsset(input, MakeWalkAsset(), &player).empty(), "No problems");
		Require(input.FindAction("Move") == move && input.FindContext("Walk") == walk, "The same handles");
		Require(input.GetContext(walk).Priority == 10 && input.GetContext(walk).Bindings.size() == 2, "With the asset's context");
		Require(input.GetUser().IsContextActive(walk), "Still on");
		Require(input.FindAction("Jump").IsValid(), "New actions added");
		Require(Near(input.GetUser().GetHapticsStrength(), 0.5f), "The player's haptics strength");
	}

	// Tuning while a button is held, as the editor does on every drag: the hold goes on
	void TestReapplyKeepsWhatBindingsRemember()
	{
		InputAssetData asset;
		asset.Actions = { { "Reload", EActionValue::Button } };
		InputContextData play;
		play.Name = "Play";
		InputBindingData hold;
		hold.Action = "Reload";
		hold.Control = "Gamepad/FaceWest";
		hold.Trigger.Kind = ETriggerKind::Hold;
		hold.Trigger.HoldSeconds = 0.5f;
		play.Bindings.push_back(hold);
		asset.Contexts.push_back(play);

		Rig rig;
		Require(ApplyInputAsset(rig.Input, asset).empty(), "No problems");
		const ActionHandle reload = rig.Input.FindAction("Reload");
		rig.Input.GetUser().SetContextActive(rig.Input.FindContext("Play"), true);
		rig.Frame();

		rig.Backend.Press(rig.Pad, Gamepad(EGamepadControl::FaceWest), rig.NowUs + 1 * Ms);
		for (int frame = 0; frame < 20; ++frame)
		{
			rig.Frame();
		}
		Require(!rig.Input.GetUser().Get(reload).bTriggered, "Held 320 ms of 500");

		// Shorter: fires at once, counting from the press, not from the change
		asset.Contexts[0].Bindings[0].Trigger.HoldSeconds = 0.3f;
		ApplyInputAsset(rig.Input, asset);
		rig.Frame();
		Require(rig.Input.GetUser().Get(reload).bTriggered, "The press kept its time across the change");

		// Another control: what it remembered no longer fits
		asset.Contexts[0].Bindings[0].Control = "Gamepad/FaceNorth";
		ApplyInputAsset(rig.Input, asset);
		rig.Backend.Press(rig.Pad, Gamepad(EGamepadControl::FaceNorth), rig.NowUs + 1 * Ms);
		rig.Frame();
		Require(rig.Input.GetUser().Get(reload).Phase == EActionPhase::Started, "A new binding starts afresh");
	}

	void TestRouterUsesTheModelsVariant()
	{
		InputAssetData asset = MakeWalkAsset();
		asset.PadProfiles.push_back({ EGamepadModel::DualShock4, { StickDeadZone(0.5f) } });

		Rig rig;
		Require(ApplyInputAsset(rig.Input, asset).empty(), "No problems");
		const ActionHandle move = rig.Input.FindAction("Move");
		rig.Input.GetUser().SetContextActive(rig.Input.FindContext("Walk"), true);

		// Past the asset's dead zone, inside the DualShock 4's
		rig.SetModel(EGamepadModel::XboxOne);
		rig.Backend.SetValue(rig.Pad, Gamepad(EGamepadControl::LeftStick), { 0.0f, 0.4f, 0.0f }, rig.NowUs + 1 * Ms);
		rig.Frame();
		Require(rig.Input.GetUser().Get(move).Vec2().y > 0.1f, "An Xbox pad moves with the asset's dead zone");

		rig.SetModel(EGamepadModel::DualShock4);
		rig.Frame();
		Require(Near(rig.Input.GetUser().Get(move).Vec2().y, 0.0f), "A DualShock 4 doesn't: its profile's dead zone");
	}
}

int main()
{
	Nyx::Logger::Get().Init();

	try
	{
		TestRoundTrip();
		TestProblemsSayWhere();
		TestLayersAndWhereValuesComeFrom();
		TestOverrideKinds();
		TestApplyKeepsHandlesAndState();
		TestReapplyKeepsWhatBindingsRemember();
		TestRouterUsesTheModelsVariant();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Input asset test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All input asset tests passed.\n";
	return 0;
}
