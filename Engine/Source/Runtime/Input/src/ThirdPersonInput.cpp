#include "ThirdPersonInput.h"

#include "CameraProjection.h"
#include "GuidComponent.h"
#include "InputAsset.h"
#include "OrbitCameraComponent.h"
#include "TransformComponent.h"

#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace Nyx::Input
{
	namespace
	{
		// The yaw of a camera turned `rotation`: where it looks, flattened onto the ground; where its
		// top points for one looking straight down
		float GroundYawOf(const glm::quat& rotation)
		{
			glm::vec3 forward = rotation * glm::vec3(0.0f, 0.0f, -1.0f);
			if (forward.x * forward.x + forward.z * forward.z < 1e-6f)
			{
				forward = rotation * glm::vec3(0.0f, 1.0f, 0.0f);
			}
			return std::atan2(-forward.x, -forward.z);
		}

		// Where the camera that follows the character looks, as a yaw
		float FindCameraYaw(const Registry& world, EntityGuid character)
		{
			std::optional<glm::quat> rotation;
			world.Each<OrbitCameraComponent>(
				[&](Entity entity, const OrbitCameraComponent& orbit)
				{
					if (!rotation && orbit.Target == character && world.Has<TransformComponent>(entity))
					{
						rotation = world.Get<TransformComponent>(entity).Rotation;
					}
				});
			if (!rotation)
			{
				if (const std::optional<WorldCamera> camera = FindPrimaryCamera(world))
				{
					rotation = camera->Transform.Rotation;
				}
			}
			return rotation ? GroundYawOf(*rotation) : 0.0f;
		}
	}

	std::vector<InputActionData> ThirdPersonInput::MakeActionsData()
	{
		return {
			{ "Walk", EActionValue::Axis2D },
			{ "Jump", EActionValue::Button },
			// Free look is about the mouse
			{ "ToggleFreeLook", EActionValue::Button, EActionDevices::KeyboardMouse },
		};
	}

	std::vector<InputContextData> ThirdPersonInput::MakeContextsData(const Settings& settings)
	{
		InputContextData context;
		context.Name = settings.ContextName;
		context.Priority = settings.Priority;

		InputBindingData keys;
		keys.Action = "Walk";
		keys.Composite = { .Right = "Keyboard/D", .Left = "Keyboard/A", .Up = "Keyboard/W", .Down = "Keyboard/S" };
		context.Bindings.push_back(keys);

		// The stick's up is forward, y
		InputBindingData walkStick;
		walkStick.Action = "Walk";
		walkStick.Control = "Gamepad/LeftStick";
		walkStick.Pipeline.DeadZone = { .bEnabled = true, .Shape = EDeadZoneShape::ScaledRadial, .Inner = settings.StickDeadZone };
		context.Bindings.push_back(walkStick);

		for (const char* control : { "Keyboard/Space", "Gamepad/FaceSouth" })
		{
			InputBindingData jump;
			jump.Action = "Jump";
			jump.Control = control;
			jump.Trigger.Kind = ETriggerKind::Pressed;
			context.Bindings.push_back(jump);
		}

		InputBindingData toggle;
		toggle.Action = "ToggleFreeLook";
		toggle.Control = "Keyboard/Tab";
		toggle.Trigger.Kind = ETriggerKind::Pressed;
		context.Bindings.push_back(toggle);

		return { context };
	}

	ThirdPersonInput::ThirdPersonInput(InputSystem& input, Settings settings)
		: Input(input)
		, PlayerSettings(std::move(settings))
	{
		// Without an input asset that has them, the defaults
		if (!Input.FindContext(PlayerSettings.ContextName).IsValid())
		{
			InputAssetData defaults;
			defaults.Actions = MakeActionsData();
			defaults.Contexts = MakeContextsData(PlayerSettings);
			ApplyInputAsset(Input, defaults);
		}

		PlayerActions.Walk = Input.FindAction("Walk");
		PlayerActions.Jump = Input.FindAction("Jump");
		PlayerActions.ToggleFreeLook = Input.FindAction("ToggleFreeLook");
		Context = Input.FindContext(PlayerSettings.ContextName);
	}

	void ThirdPersonInput::SetActive(bool bActive, std::string reason)
	{
		Input.GetUser().SetContextActive(Context, bActive, { .Reason = bActive ? std::move(reason) : std::string() });
	}

	std::optional<MoveCharacterCommand> MakeMoveCharacterCommand(
		const Registry& world, Entity character, InputUser& user, const ThirdPersonInput::Actions& actions)
	{
		if (!world.Has<GuidComponent>(character) || !world.Get<GuidComponent>(character).Guid.IsValid())
		{
			return std::nullopt;
		}
		const EntityGuid guid = world.Get<GuidComponent>(character).Guid;
		return MoveCharacterCommand{
			.Character = guid,
			.Move = GetGroundMove(FindCameraYaw(world, guid), user.Get(actions.Walk).Vec2()),
			.bJump = user.ConsumeTriggered(actions.Jump),
		};
	}
}
