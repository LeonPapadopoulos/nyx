// An input system's frames as records, replayed by a replica: from a stream started in the middle
// of things (a button held, a context on), through presses, holds, taps, sticks, the mouse, the
// focus going and coming, ImGui taking the keyboard, contexts switched by code and tuning changed,
// the replica decides every frame as the original did.
#include "BinaryArchive.h"
#include "EditorLinkMessageText.h"
#include "EditorLinkMessages.h"
#include "FlyCameraInput.h"
#include "InputAsset.h"
#include "InputRecipes.h"
#include "InputStream.h"
#include "Log.h"

#include <cmath>
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

	constexpr uint64_t Ms = 1000;

	struct Original
	{
		VirtualInputBackend Backend;
		InputSystem Input;
		InputDeviceId Keyboard;
		InputDeviceId Mouse;
		InputDeviceId Pad;
		uint64_t NowUs = 1'000'000;

		Original()
		{
			Input.AddBackend(Backend);
			Keyboard = Backend.AddDevice(EDeviceKind::Keyboard, "Keyboard", NowUs);
			Mouse = Backend.AddDevice(EDeviceKind::Mouse, "Mouse", NowUs);
			Pad = Backend.AddDevice(EDeviceKind::Gamepad, "Pad", NowUs);
			InputDeviceInfo info;
			info.Name = "Pad";
			info.Model = EGamepadModel::DualSense;
			Backend.SetDeviceInfo(Pad, info, NowUs);
		}
	};

	// Every action's state and every binding's trace alike
	void RequireSame(const InputSystem& original, const InputSystem& replica, int frame)
	{
		const std::string at = " (frame " + std::to_string(frame) + ")";
		Require(original.GetActionCount() == replica.GetActionCount() && original.GetContextCount() == replica.GetContextCount(),
			"The same definitions" + at);
		for (uint32_t index = 0; index < static_cast<uint32_t>(original.GetActionCount()); ++index)
		{
			const ActionHandle action = ActionHandle::Make(index, 1);
			const ActionState& a = original.GetUser().Get(action);
			const ActionState& b = replica.GetUser().Get(action);
			const std::string name = original.GetAction(action).Name;
			Require(a.Phase == b.Phase, name + ": phase " + GetActionPhaseName(a.Phase) + " in the original, " + GetActionPhaseName(b.Phase) + at);
			Require(a.bTriggered == b.bTriggered, name + ": fired in one, not the other" + at);
			Require(glm::length(a.Value - b.Value) < 1e-5f, name + ": value " + FormatValue(a.Value) + " against " + FormatValue(b.Value) + at);
		}

		const std::vector<BindingTrace>& a = original.GetUser().GetTraces();
		const std::vector<BindingTrace>& b = replica.GetUser().GetTraces();
		Require(a.size() == b.size(), "As many traces" + at);
		for (size_t index = 0; index < a.size(); ++index)
		{
			Require(FormatTrace(original, a[index]) == FormatTrace(replica, b[index]),
				"Trace " + std::to_string(index) + ":\n" + FormatTrace(original, a[index]) + "\nagainst\n" + FormatTrace(replica, b[index]) + at);
		}
		Require(original.GetCursor().Mode == replica.GetCursor().Mode, "The same cursor" + at);
	}

	void TestReplicaDecidesTheSame()
	{
		Original original;
		InputAssetData asset = MakeDefaultInputAsset();
		AddRecipe(asset, 2, EInputRecipe::TapAndHold, "Use");
		AddRecipe(asset, 2, EInputRecipe::Press, "Jump");
		Require(ApplyInputAsset(original.Input, asset).empty(), "The asset applies");
		FlyCameraInput camera(original.Input, FlyCameraInput::Settings{});
		camera.SetActive(true, false, "Playing");

		// Before the stream: the stick pushed, E held
		original.Backend.SetValue(original.Pad, Gamepad(EGamepadControl::LeftStick), glm::vec3(0.0f, 0.6f, 0.0f), original.NowUs + 1);
		original.Backend.Press(original.Keyboard, Key(EKey::E), original.NowUs + 2);
		for (int frame = 0; frame < 5; ++frame)
		{
			original.NowUs += 16 * Ms;
			original.Input.BeginFrame(original.NowUs);
			original.Input.EndFrame();
		}

		InputStreamWriter writer(original.Input);
		writer.Start();
		InputReplica replica;
		size_t definitionRecords = 0;

		// What happens in each frame, before it begins
		const std::vector<std::function<void(Original&)>> script = {
			[](Original&)
			{
			},
			[](Original& o)
			{
				o.Backend.Release(o.Keyboard, Key(EKey::E), o.NowUs + 3 * Ms);
			},
			[](Original& o)
			{
				o.Backend.Press(o.Pad, Gamepad(EGamepadControl::FaceWest), o.NowUs + 1 * Ms);
			},
			[](Original&)
			{
			},
			[](Original& o)
			{
				o.Backend.SetValue(o.Pad, Gamepad(EGamepadControl::RightStick), glm::vec3(0.95f, 0.0f, 0.0f), o.NowUs + 2 * Ms);
			},
			[](Original& o)
			{
				o.Backend.SetValue(o.Mouse, Mouse(EMouseControl::Wheel), glm::vec3(0.0f, 1.0f, 0.0f), o.NowUs + 1 * Ms);
			},
			[](Original& o)
			{
				// A tap shorter than a frame
				o.Backend.Press(o.Keyboard, Key(EKey::Space), o.NowUs + 1 * Ms);
				o.Backend.Release(o.Keyboard, Key(EKey::Space), o.NowUs + 4 * Ms);
			},
			[](Original& o)
			{
				o.Input.SetImGuiState({ .bTypingText = true });
			},
			[](Original& o)
			{
				o.Backend.Press(o.Keyboard, Key(EKey::W), o.NowUs + 1 * Ms);
			},
			[](Original& o)
			{
				o.Input.SetImGuiState({});
			},
			[](Original& o)
			{
				o.Backend.SetFocus(false, o.NowUs + 1 * Ms);
			},
			[](Original& o)
			{
				o.Input.SetGamepadsInBackground(true);
			},
			[](Original& o)
			{
				o.Backend.SetFocus(true, o.NowUs + 1 * Ms);
			},
			[&camera](Original&)
			{
				camera.SetActive(true, true, "Looking");
			},
			[](Original& o)
			{
				o.Backend.SetValue(o.Mouse, Mouse(EMouseControl::Delta), glm::vec3(12.0f, -4.0f, 0.0f), o.NowUs + 1 * Ms);
			},
			[&asset](Original& o)
			{
				// Tuning changed while the stick is held
				asset.Contexts[2].Bindings[1].Pipeline.DeadZone.Inner = 0.3f;
				ApplyInputAsset(o.Input, asset);
			},
			[](Original& o)
			{
				o.Backend.Release(o.Pad, Gamepad(EGamepadControl::FaceWest), o.NowUs + 1 * Ms);
			},
			[](Original& o)
			{
				o.Input.GetUser().ConsumeTriggered(o.Input.FindAction("Jump"));
			},
			[](Original& o)
			{
				o.Backend.RemoveDevice(o.Pad, o.NowUs + 1 * Ms);
			},
			[&camera](Original&)
			{
				camera.SetActive(false, false, "");
			},
		};

		for (int frame = 0; frame < static_cast<int>(script.size()) + 30; ++frame)
		{
			if (frame < static_cast<int>(script.size()))
			{
				script[frame](original);
			}
			original.NowUs += 16 * Ms;
			original.Input.BeginFrame(original.NowUs);
			original.Input.EndFrame();

			for (const InputStreamWriter::Record& record : writer.TakeRecords())
			{
				definitionRecords += record.bDefinitions;
				Require(record.bDefinitions ? replica.ApplyDefinitions(record.Bytes) : replica.ApplyFrame(record.Bytes), "Every record reads");
			}
			// The snapshot frame sees E and the stick as pressed now
			if (frame > 0)
			{
				RequireSame(original.Input, replica.GetInput(), frame);
			}
		}

		Require(definitionRecords == 2, "The definitions at the start and when the tuning changed: " + std::to_string(definitionRecords));
		Require(replica.GetFrameCount() == script.size() + 30, "A frame record per frame");
		Require(replica.GetHistory().GetFrames().size() == replica.GetFrameCount(), "The replica keeps its own history");
		Require(replica.GetInput().GetUser().WasEverConsumed(replica.GetInput().FindAction("Jump")), "And knows what the code consumed");

		writer.Stop();
		original.NowUs += 16 * Ms;
		original.Input.BeginFrame(original.NowUs);
		original.Input.EndFrame();
		Require(writer.TakeRecords().empty(), "Stopped: nothing more");
	}

	// Over the editor link, as the game sends them and the editor's Game Link window shows them
	void TestOverTheLink()
	{
		Original original;
		InputAssetData asset = MakeDefaultInputAsset();
		Require(ApplyInputAsset(original.Input, asset).empty(), "The asset applies");
		FlyCameraInput camera(original.Input, FlyCameraInput::Settings{});
		camera.SetActive(true, false, "Playing");

		InputStreamWriter writer(original.Input);
		writer.Start();
		InputReplica replica;
		bool bSawDefinitions = false;
		bool bSawStack = false;
		for (int frame = 0; frame < 3; ++frame)
		{
			original.Backend.Press(original.Keyboard, Key(EKey::W), original.NowUs + 1);
			original.NowUs += 16 * Ms;
			original.Input.BeginFrame(original.NowUs);
			original.Input.EndFrame();

			for (InputStreamWriter::Record& record : writer.TakeRecords())
			{
				if (record.bDefinitions)
				{
					const Nyx::Net::Message message = Nyx::MakeNetMessage(Nyx::InputDefinitionsMessage{ record.Bytes });
					const Nyx::EditorLinkMessageText text = Nyx::DescribeEditorLinkMessage(message);
					Require(text.Name == "InputDefinitions" && text.Summary == "8 actions, 9 contexts", "Definitions described: " + text.Summary);
					Require(text.Details.find("Context: FlyCamera (100), 7 bindings") != std::string::npos, "With every context: " + text.Details);

					Nyx::InputDefinitionsMessage read;
					Require(Nyx::ReadNetMessage(message, read) && replica.ApplyDefinitions(read.Record), "The editor reads them");
					bSawDefinitions = true;
				}
				else
				{
					const Nyx::Net::Message message = Nyx::MakeNetMessage(Nyx::InputFrameMessage{ record.Bytes });
					const Nyx::EditorLinkMessageText text = Nyx::DescribeEditorLinkMessage(message);
					Require(text.Name == "InputFrame", "A frame");
					if (text.Summary.find("the stack") != std::string::npos)
					{
						Require(text.Details.find("Stack: FlyCamera (Playing)") != std::string::npos, "The stack in the details: " + text.Details);
						bSawStack = true;
					}

					Nyx::InputFrameMessage read;
					Require(Nyx::ReadNetMessage(message, read) && replica.ApplyFrame(read.Record), "The editor reads it");
				}
			}
		}
		Require(bSawDefinitions && bSawStack, "The definitions and the stack went over");
		Require(replica.GetInput().GetUser().Get(replica.GetInput().FindAction("Move")).Vec3().z > 0.5f, "The replica moves forward with W");

		const Nyx::EditorLinkMessageText subscribe =
			Nyx::DescribeEditorLinkMessage(Nyx::MakeNetMessage(Nyx::InputSubscribeMessage{ true }));
		Require(subscribe.Name == "InputSubscribe" && subscribe.Summary == "start streaming the game's input", "Subscribing described");
	}

	void TestDamagedRecords()
	{
		InputDefinitionsRecord definitions;
		definitions.Actions.push_back({ "Jump", EActionValue::Button });
		Nyx::BinaryWriter writer;
		WriteDefinitionsRecord(writer, definitions);
		std::vector<std::byte> bytes = writer.GetBytes();
		bytes.resize(bytes.size() - 1);

		InputReplica replica;
		Require(!replica.ApplyDefinitions(bytes), "Cut off definitions don't read");
		Require(replica.GetInput().GetActionCount() == 0, "And change nothing");
		Require(!replica.ApplyFrame({ std::byte{ 1 }, std::byte{ 2 } }), "Nor does a cut off frame");
		Require(replica.GetFrameCount() == 0, "Which isn't counted");
	}
}

int main()
{
	Nyx::Logger::Get().Init();

	try
	{
		TestReplicaDecidesTheSame();
		TestOverTheLink();
		TestDamagedRecords();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Input stream test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All input stream tests passed.\n";
	return 0;
}
