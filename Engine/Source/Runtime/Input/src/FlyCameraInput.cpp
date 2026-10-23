#include "FlyCameraInput.h"

#include "InputAsset.h"

namespace Nyx::Input
{
	std::vector<InputActionData> FlyCameraInput::MakeActionsData()
	{
		// The wheel and the right mouse button have nothing to do on a pad
		return {
			{ "Move", EActionValue::Axis3D },
			{ "MoveVertical", EActionValue::Axis1D },
			{ "Look", EActionValue::Axis2D },
			{ "Speed", EActionValue::Axis1D, EActionDevices::KeyboardMouse },
			{ "LookButton", EActionValue::Button, EActionDevices::KeyboardMouse },
		};
	}

	std::vector<InputContextData> FlyCameraInput::MakeContextsData(const Settings& settings)
	{
		const DeadZoneSettings stickDeadZone{ .bEnabled = true, .Shape = EDeadZoneShape::ScaledRadial, .Inner = settings.StickDeadZone };

		InputContextData fly;
		fly.Name = settings.FlyContextName;
		fly.Priority = settings.Priority;

		InputBindingData keys;
		keys.Action = "Move";
		keys.Composite = { .Right = "Keyboard/D", .Left = "Keyboard/A", .Forward = "Keyboard/W", .Back = "Keyboard/S" };
		fly.Bindings.push_back(keys);

		// The left stick's up is forward
		InputBindingData moveStick;
		moveStick.Action = "Move";
		moveStick.Control = "Gamepad/LeftStick";
		moveStick.SwizzleY = -1;
		moveStick.SwizzleZ = 1;
		moveStick.Pipeline.DeadZone = stickDeadZone;
		fly.Bindings.push_back(moveStick);

		// Up and down as one axis: E and the right trigger up, Q and the left trigger down
		InputBindingData verticalKeys;
		verticalKeys.Action = "MoveVertical";
		verticalKeys.Composite = { .Right = "Keyboard/E", .Left = "Keyboard/Q" };
		fly.Bindings.push_back(verticalKeys);

		InputBindingData verticalTriggers;
		verticalTriggers.Action = "MoveVertical";
		verticalTriggers.Composite = { .Right = "Gamepad/RightTrigger", .Left = "Gamepad/LeftTrigger" };
		verticalTriggers.Pipeline.DeadZone = { .bEnabled = true, .Shape = EDeadZoneShape::ScaledRadial, .Inner = 0.05f };
		fly.Bindings.push_back(verticalTriggers);

		// The right stick turns at a rate: finer near the middle (curve), faster held at the edge
		// (look acceleration), times the frame's time
		InputBindingData lookStick;
		lookStick.Action = "Look";
		lookStick.Control = "Gamepad/RightStick";
		lookStick.Pipeline.DeadZone = stickDeadZone;
		lookStick.Pipeline.Curve = { .bEnabled = true, .Kind = ECurveKind::Power, .Exponent = 2.0f };
		lookStick.Pipeline.Sensitivity = { .bEnabled = true, .Scale = glm::vec3(settings.StickTurnRate) };
		lookStick.Pipeline.LookAcceleration = { .bEnabled = true };
		lookStick.Pipeline.bMultiplyByDeltaTime = true;
		fly.Bindings.push_back(lookStick);

		InputBindingData speed;
		speed.Action = "Speed";
		speed.Control = "Mouse/Wheel";
		speed.SwizzleX = 1;
		speed.SwizzleY = -1;
		speed.SwizzleZ = -1;
		fly.Bindings.push_back(speed);

		InputBindingData lookButton;
		lookButton.Action = "LookButton";
		lookButton.Control = "Mouse/Right";
		fly.Bindings.push_back(lookButton);

		// The mouse moves a distance, not a rate: no delta time. Its y points down; Look's up.
		InputContextData look;
		look.Name = settings.MouseLookContextName;
		look.Priority = settings.Priority + 10;
		look.Cursor = EContextCursor::Captured;

		InputBindingData lookMouse;
		lookMouse.Action = "Look";
		lookMouse.Control = "Mouse/Delta";
		lookMouse.Pipeline.Sensitivity = { .bEnabled = true, .Scale = glm::vec3(settings.MouseSensitivity), .bInvertY = true };
		look.Bindings.push_back(lookMouse);

		return { fly, look };
	}

	FlyCameraInput::FlyCameraInput(InputSystem& input, Settings settings)
		: Input(input)
		, FlySettings(std::move(settings))
	{
		// Without an input asset that has them, the defaults
		if (!Input.FindContext(FlySettings.FlyContextName).IsValid() || !Input.FindContext(FlySettings.MouseLookContextName).IsValid())
		{
			InputAssetData defaults;
			defaults.Actions = MakeActionsData();
			defaults.Contexts = MakeContextsData(FlySettings);
			ApplyInputAsset(Input, defaults);
		}

		FlyActions.Move = Input.FindAction("Move");
		FlyActions.MoveVertical = Input.FindAction("MoveVertical");
		FlyActions.Look = Input.FindAction("Look");
		FlyActions.Speed = Input.FindAction("Speed");
		FlyActions.LookButton = Input.FindAction("LookButton");
		FlyContext = Input.FindContext(FlySettings.FlyContextName);
		MouseLookContext = Input.FindContext(FlySettings.MouseLookContextName);
	}

	void FlyCameraInput::SetActive(bool bFlying, bool bLooking, std::string reason, void* lookWindow)
	{
		InputUser& user = Input.GetUser();
		user.SetContextActive(FlyContext, bFlying, { .Reason = bFlying ? reason : std::string() });
		user.SetContextActive(MouseLookContext, bLooking, { .Reason = bLooking ? std::move(reason) : std::string(), .Window = lookWindow });
	}

	FlyCameraInput::Values FlyCameraInput::Read() const
	{
		const InputUser& user = Input.GetUser();
		const ActionState& lookButton = user.Get(FlyActions.LookButton);

		Values values;
		const glm::vec3 move = user.Get(FlyActions.Move).Vec3();
		values.Move = glm::vec3(move.x, user.Get(FlyActions.MoveVertical).Float(), move.z);
		values.Look = user.Get(FlyActions.Look).Vec2();
		values.SpeedSteps = user.Get(FlyActions.Speed).Float();
		values.bLookButtonStarted = lookButton.bStarted;
		values.bLookButtonHeld = lookButton.IsTriggered();
		return values;
	}
}
