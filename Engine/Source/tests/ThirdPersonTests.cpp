// Walking a character through commands, without a window: a walk becomes a MoveCharacterCommand
// relative to the camera's yaw; the command walks the character in the next step, once, through
// SimContext::Commands; of two in one step the last move counts and two jumps jump once; and the
// built-in player (ThirdPersonPlayer) walks the character of CharacterTest.nyxscene without a game
// module, the orbit camera following, and switches the orbit camera's free look.
#include "CharacterComponent.h"
#include "CommandQueue.h"
#include "ComponentRegistration.h"
#include "GameWorld.h"
#include "GuidComponent.h"
#include "InputSystem.h"
#include "Log.h"
#include "MoveCharacterCommand.h"
#include "OrbitCameraComponent.h"
#include "OrbitCameraInput.h"
#include "OrbitCameras.h"
#include "Paths.h"
#include "PhysicsComponents.h"
#include "SceneSerializer.h"
#include "TestSupport.h"
#include "ThirdPersonInput.h"
#include "ThirdPersonPlayer.h"
#include "TransformComponent.h"
#include "VirtualInputBackend.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using namespace Nyx;
	using namespace Nyx::Input;
	using Nyx::Tests::Require;

	constexpr uint64_t Ms = 1000;
	constexpr float StepSeconds = 1.0f / 60.0f;

	bool Near(const glm::vec3& a, const glm::vec3& b, float tolerance = 1e-4f)
	{
		return glm::length(a - b) < tolerance;
	}

	std::string Describe(const glm::vec3& v)
	{
		return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
	}

	// A virtual keyboard and mouse on an input system
	struct Devices
	{
		VirtualInputBackend Backend;
		InputSystem Input;
		InputDeviceId Keyboard;
		InputDeviceId Mouse;
		uint64_t NowUs = 1000 * Ms;

		Devices()
		{
			Input.AddBackend(Backend);
			Keyboard = Backend.AddDevice(EDeviceKind::Keyboard, "Keyboard", NowUs);
			Mouse = Backend.AddDevice(EDeviceKind::Mouse, "Mouse", NowUs);
		}

		void Press(EKey key)
		{
			Backend.Press(Keyboard, Key(key), NowUs + 1 * Ms);
		}

		void Release(EKey key)
		{
			Backend.Release(Keyboard, Key(key), NowUs + 1 * Ms);
		}
	};

	// A floor, a character on it with a guid, and an orbit camera following it, turned `yawDegrees`
	Entity AddCharacterAndCamera(Registry& world, float yawDegrees, Entity* outCamera = nullptr)
	{
		const Entity floor = world.CreateEntity();
		world.Add<TransformComponent>(floor, TransformComponent{ .Position = glm::vec3(0.0f, -0.5f, 0.0f) });
		world.Add<BoxColliderComponent>(floor, BoxColliderComponent{ .HalfExtents = glm::vec3(30.0f, 0.5f, 30.0f) });

		const Entity character = world.CreateEntity();
		world.Add<GuidComponent>(character, GuidComponent{ EntityGuid::Generate() });
		world.Add<TransformComponent>(character, TransformComponent{});
		world.Add<CharacterComponent>(character, CharacterComponent{ .Acceleration = 10000.0f });

		const Entity camera = world.CreateEntity();
		world.Add<TransformComponent>(camera, TransformComponent{ .Rotation = glm::angleAxis(glm::radians(yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f)) });
		world.Add<OrbitCameraComponent>(camera, OrbitCameraComponent{ .Target = world.Get<GuidComponent>(character).Guid });
		if (outCamera)
		{
			*outCamera = camera;
		}
		return character;
	}

	void TestAWalkBecomesACommandRelativeToTheCamera()
	{
		Devices devices;
		ThirdPersonInput thirdPerson(devices.Input, ThirdPersonInput::Settings{});
		Registry world;
		// Turned a quarter to the left: it looks along -X
		const Entity character = AddCharacterAndCamera(world, 90.0f);
		devices.Input.GetUser().SetContextActive(thirdPerson.GetContext(), true);
		devices.Input.BeginFrame(devices.NowUs += 16 * Ms);
		devices.Input.EndFrame();

		devices.Press(EKey::W);
		devices.Input.BeginFrame(devices.NowUs += 16 * Ms);
		std::optional<MoveCharacterCommand> command = MakeMoveCharacterCommand(world, character, devices.Input.GetUser(), thirdPerson.GetActions());
		devices.Input.EndFrame();
		Require(command && command->Character == world.Get<GuidComponent>(character).Guid, "The command names the character by its guid");
		Require(Near(command->Move, glm::vec3(-1.0f, 0.0f, 0.0f)), "W walks where the turned camera looks, along -X " + Describe(command->Move));
		Require(!command->bJump, "No jump without Space");

		devices.Release(EKey::W);
		devices.Press(EKey::D);
		devices.Press(EKey::Space);
		devices.Input.BeginFrame(devices.NowUs += 16 * Ms);
		command = MakeMoveCharacterCommand(world, character, devices.Input.GetUser(), thirdPerson.GetActions());
		devices.Input.EndFrame();
		Require(Near(command->Move, glm::vec3(0.0f, 0.0f, -1.0f)), "D walks to the camera's right, along -Z " + Describe(command->Move));
		Require(command->bJump, "Space jumps");

		devices.Input.BeginFrame(devices.NowUs += 16 * Ms);
		command = MakeMoveCharacterCommand(world, character, devices.Input.GetUser(), thirdPerson.GetActions());
		devices.Input.EndFrame();
		Require(!command->bJump, "A held Space jumps once");

		world.Remove<GuidComponent>(character);
		Require(!MakeMoveCharacterCommand(world, character, devices.Input.GetUser(), thirdPerson.GetActions()),
			"A character without a guid can't be named by a command");
	}

	// Counts the move commands each step's systems get
	class CountMoveCommandsSystem final : public ISimSystem
	{
	public:
		const char* GetName() const override
		{
			return "Count Move Commands";
		}

		void Step(const SimContext& context) override
		{
			Count = 0;
			context.Commands.ForEach<MoveCharacterCommand>([&](const MoveCharacterCommand&)
				{
					++Count;
				});
		}

		int Count = 0;
	};

	struct Steps
	{
		GameWorld& World;
		uint64_t Next = 0;

		void Run(int count)
		{
			for (int i = 0; i < count; ++i, ++Next)
			{
				World.Step(StepTime{ StepSeconds, Next, static_cast<double>(Next) * StepSeconds });
			}
		}
	};

	void TestTheCommandWalksTheCharacterInTheNextStep()
	{
		GameWorld world;
		Steps steps{ world };
		const Entity character = AddCharacterAndCamera(world.GetRegistry(), 0.0f);
		auto& counter = static_cast<CountMoveCommandsSystem&>(world.AddSystem(std::make_unique<CountMoveCommandsSystem>()));
		steps.Run(10);
		const EntityGuid guid = world.GetRegistry().Get<GuidComponent>(character).Guid;
		const glm::vec3 start = world.GetRegistry().Get<TransformComponent>(character).Position;

		world.GetCommands().Add(MoveCharacterCommand{ .Character = guid, .Move = glm::vec3(1.0f, 0.0f, 0.0f) });
		Require(world.GetRegistry().Get<CharacterComponent>(character).DesiredMove == glm::vec3(0.0f), "Nothing changes before the step");
		steps.Run(1);
		const CharacterComponent& walker = world.GetRegistry().Get<CharacterComponent>(character);
		Require(counter.Count == 1, "The step's systems see the command");
		Require(walker.DesiredMove == glm::vec3(1.0f, 0.0f, 0.0f), "The step applies the move");
		const float walked = world.GetRegistry().Get<TransformComponent>(character).Position.x - start.x;
		Require(std::abs(walked - 4.0f * StepSeconds) < 1e-3f, "The character walks in that step, at its speed (" + std::to_string(walked) + " m)");

		steps.Run(1);
		Require(counter.Count == 0 && world.GetCommands().IsEmpty(), "The command is applied once: the next step has none");
		Require(walker.DesiredMove == glm::vec3(1.0f, 0.0f, 0.0f), "The move stays until the next command, so a frame of two steps walks in both");

		world.GetCommands().Add(MoveCharacterCommand{ .Character = guid });
		steps.Run(1);
		Require(walker.DesiredMove == glm::vec3(0.0f), "A still walk stops it");

		// A guid that names no character is dropped
		world.GetCommands().Add(MoveCharacterCommand{ .Character = EntityGuid::Generate(), .Move = glm::vec3(1.0f, 0.0f, 0.0f) });
		steps.Run(1);
		Require(walker.DesiredMove == glm::vec3(0.0f), "Another guid's command leaves the character alone");
	}

	void TestTheLastMoveCountsAndJumpsAddUp()
	{
		GameWorld world;
		Steps steps{ world };
		const Entity character = AddCharacterAndCamera(world.GetRegistry(), 0.0f);
		steps.Run(10);
		const EntityGuid guid = world.GetRegistry().Get<GuidComponent>(character).Guid;
		const CharacterComponent& walker = world.GetRegistry().Get<CharacterComponent>(character);

		// Two frames without a step between them: both jumped, the second walks elsewhere
		world.GetCommands().Add(MoveCharacterCommand{ .Character = guid, .Move = glm::vec3(1.0f, 0.0f, 0.0f), .bJump = true });
		world.GetCommands().Add(MoveCharacterCommand{ .Character = guid, .Move = glm::vec3(0.0f, 0.0f, 1.0f), .bJump = true });
		steps.Run(1);
		Require(walker.DesiredMove == glm::vec3(0.0f, 0.0f, 1.0f), "The last move of the step counts");
		const float rising = walker.Velocity.y;
		Require(std::abs(rising - (walker.JumpSpeed - 9.81f * StepSeconds)) < 0.01f, "Two jumps in one step jump once (" + std::to_string(rising) + " m/s)");

		// A jump then a frame without one: still a jump
		steps.Run(120);
		world.GetCommands().Add(MoveCharacterCommand{ .Character = guid, .bJump = true });
		world.GetCommands().Add(MoveCharacterCommand{ .Character = guid });
		steps.Run(1);
		Require(walker.Velocity.y > 1.0f, "A jump asked in one frame isn't lost to the next frame's command");
	}

	void TestTheBuiltInPlayerWalksTheSampleScene()
	{
		Devices devices;
		OrbitCameraInput orbitInput(devices.Input, OrbitCameraInput::Settings{});
		ThirdPersonPlayer player(devices.Input);
		GameWorld world;
		Require(SceneSerializer::LoadFromFile(Nyx::Paths::GetAssetsDir() / "Scenes/CharacterTest.nyxscene", world.GetRegistry()),
			"The sample scene can't be loaded");
		uint64_t step = 0;

		// A frame as the game runs it: the cameras' input, the player, a step, the cameras placed
		const auto frame = [&]()
		{
			devices.Input.BeginFrame(devices.NowUs += 16 * Ms);
			Input::UpdateOrbitCameras(world.GetRegistry(), orbitInput);
			player.Update(PlayerFrame{ .World = world, .Input = devices.Input, .Commands = world.GetCommands(), .DeltaSeconds = StepSeconds });
			devices.Input.EndFrame();
			world.Step(StepTime{ StepSeconds, step, static_cast<double>(step) * StepSeconds });
			++step;
			Nyx::UpdateOrbitCameras(world.GetRegistry(), StepSeconds, 1.0f);
		};

		std::optional<Entity> character = FindThirdPersonCharacter(world.GetRegistry());
		Require(character.has_value(), "The sample scene's orbit camera follows its character");
		for (int i = 0; i < 30; ++i)
		{
			frame();
		}
		Require(devices.Input.GetUser().IsContextActive(orbitInput.GetFreeLookContext()), "The built-in player asks for free look");
		Require(devices.Input.GetUser().GetCursor().Mode == ECursorMode::Captured, "Free look captures the cursor");

		// The camera looks along -Z, behind the character: W walks away from it
		const glm::vec3 start = world.GetRegistry().Get<TransformComponent>(*character).Position;
		devices.Press(EKey::W);
		for (int i = 0; i < 30; ++i)
		{
			frame();
		}
		devices.Release(EKey::W);
		const glm::vec3 walked = world.GetRegistry().Get<TransformComponent>(*character).Position - start;
		Require(walked.z < -1.0f && std::abs(walked.x) < 0.05f, "W walks the character away from the camera " + Describe(walked));

		// Tab frees the cursor: the right button held looks from then on
		devices.Press(EKey::Tab);
		frame();
		devices.Release(EKey::Tab);
		frame();
		frame();
		Require(!player.IsFreeLook() && !devices.Input.GetUser().IsContextActive(orbitInput.GetFreeLookContext()), "Tab switches free look off");
		Require(devices.Input.GetUser().GetCursor().Mode == ECursorMode::Free, "And frees the cursor");

		// Without an orbit camera, the player has no character and its context goes off
		std::vector<Entity> cameras;
		world.GetRegistry().Each<OrbitCameraComponent>([&](Entity entity, OrbitCameraComponent&)
			{
				cameras.push_back(entity);
			});
		for (const Entity camera : cameras)
		{
			world.GetRegistry().Remove<OrbitCameraComponent>(camera);
		}
		frame();
		frame();
		Require(!FindThirdPersonCharacter(world.GetRegistry()), "Without an orbit camera there is no third person character");
		Require(!devices.Input.GetUser().IsContextActive(devices.Input.FindContext("ThirdPerson")), "And the third person context is off");
	}
}

int main()
{
	Nyx::Logger::Get().Init();
	Nyx::RegisterComponentTypes();

	try
	{
		TestAWalkBecomesACommandRelativeToTheCamera();
		TestTheCommandWalksTheCharacterInTheNextStep();
		TestTheLastMoveCountsAndJumpsAddUp();
		TestTheBuiltInPlayerWalksTheSampleScene();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Third person test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All third person tests passed.\n";
	return 0;
}
