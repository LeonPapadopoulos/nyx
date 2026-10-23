#include "FlyCameraInput.h"

namespace Nyx::Input
{
	FlyCameraInput::FlyCameraInput(InputSystem& input, Settings settings)
		: Input(input)
		, FlySettings(std::move(settings))
	{
		FlyActions.Move = Input.AddAction({ "Move", EActionValue::Axis3D });
		FlyActions.MoveVertical = Input.AddAction({ "MoveVertical", EActionValue::Axis1D });
		FlyActions.Look = Input.AddAction({ "Look", EActionValue::Axis2D });
		FlyActions.Speed = Input.AddAction({ "Speed", EActionValue::Axis1D });
		FlyActions.LookButton = Input.AddAction({ "LookButton", EActionValue::Button });

		const DeadZoneSettings stickDeadZone{ .bEnabled = true, .Shape = EDeadZoneShape::ScaledRadial, .Inner = FlySettings.StickDeadZone };

		InputContextDef fly;
		fly.Name = FlySettings.FlyContextName;
		fly.Priority = FlySettings.Priority;

		fly.Bindings.push_back(InputBinding::ForComposite(FlyActions.Move,
			CompositeKeys{ .Right = Key(EKey::D), .Left = Key(EKey::A), .Forward = Key(EKey::W), .Back = Key(EKey::S) }));

		// The left stick's up is forward
		InputBinding moveStick = InputBinding::ForControl(FlyActions.Move, Gamepad(EGamepadControl::LeftStick));
		moveStick.Swizzle = glm::ivec3(0, -1, 1);
		moveStick.Pipeline.DeadZone = stickDeadZone;
		fly.Bindings.push_back(moveStick);

		// Up and down as one axis: E and the right trigger up, Q and the left trigger down
		fly.Bindings.push_back(InputBinding::ForComposite(FlyActions.MoveVertical, CompositeKeys{ .Right = Key(EKey::E), .Left = Key(EKey::Q) }));
		InputBinding verticalTriggers = InputBinding::ForComposite(FlyActions.MoveVertical,
			CompositeKeys{ .Right = Gamepad(EGamepadControl::RightTrigger), .Left = Gamepad(EGamepadControl::LeftTrigger) });
		verticalTriggers.Pipeline.DeadZone = { .bEnabled = true, .Shape = EDeadZoneShape::ScaledRadial, .Inner = 0.05f };
		fly.Bindings.push_back(verticalTriggers);

		// The right stick turns at a rate: finer near the middle (curve), faster held at the edge
		// (look acceleration), times the frame's time
		InputBinding lookStick = InputBinding::ForControl(FlyActions.Look, Gamepad(EGamepadControl::RightStick));
		lookStick.Pipeline.DeadZone = stickDeadZone;
		lookStick.Pipeline.Curve = { .bEnabled = true, .Kind = ECurveKind::Power, .Exponent = 2.0f };
		lookStick.Pipeline.Sensitivity = { .bEnabled = true, .Scale = glm::vec3(FlySettings.StickTurnRate) };
		lookStick.Pipeline.LookAcceleration = { .bEnabled = true };
		lookStick.Pipeline.bMultiplyByDeltaTime = true;
		fly.Bindings.push_back(lookStick);

		InputBinding speed = InputBinding::ForControl(FlyActions.Speed, Mouse(EMouseControl::Wheel));
		speed.Swizzle = glm::ivec3(1, -1, -1);
		fly.Bindings.push_back(speed);

		fly.Bindings.push_back(InputBinding::ForControl(FlyActions.LookButton, Mouse(EMouseControl::Right)));
		FlyContext = Input.AddContext(std::move(fly));

		// The mouse moves a distance, not a rate: no delta time. Its y points down; Look's up.
		InputContextDef look;
		look.Name = FlySettings.MouseLookContextName;
		look.Priority = FlySettings.Priority + 10;
		look.Cursor = ECursorMode::Captured;
		InputBinding lookMouse = InputBinding::ForControl(FlyActions.Look, Mouse(EMouseControl::Delta));
		lookMouse.Pipeline.Sensitivity = { .bEnabled = true, .Scale = glm::vec3(FlySettings.MouseSensitivity), .bInvertY = true };
		look.Bindings.push_back(lookMouse);
		MouseLookContext = Input.AddContext(std::move(look));
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
