#include "OrbitCameraInput.h"

#include "Entity.h"
#include "InputAsset.h"
#include "OrbitCameraComponent.h"
#include "TransformComponent.h"

namespace Nyx::Input
{
	std::vector<InputActionData> OrbitCameraInput::MakeActionsData()
	{
		// The wheel and the right mouse button have nothing to do on a pad
		return {
			{ "OrbitLook", EActionValue::Axis2D },
			{ "OrbitZoom", EActionValue::Axis1D, EActionDevices::KeyboardMouse },
			{ "OrbitLookButton", EActionValue::Button, EActionDevices::KeyboardMouse },
		};
	}

	std::vector<InputContextData> OrbitCameraInput::MakeContextsData(const Settings& settings)
	{
		InputContextData orbit;
		orbit.Name = settings.OrbitContextName;
		orbit.Priority = settings.Priority;

		// As the fly camera's right stick: finer near the middle (curve), faster held at the edge
		// (look acceleration), times the frame's time
		InputBindingData lookStick;
		lookStick.Action = "OrbitLook";
		lookStick.Control = "Gamepad/RightStick";
		lookStick.Pipeline.DeadZone = { .bEnabled = true, .Shape = EDeadZoneShape::ScaledRadial, .Inner = settings.StickDeadZone };
		lookStick.Pipeline.Curve = { .bEnabled = true, .Kind = ECurveKind::Power, .Exponent = 2.0f };
		lookStick.Pipeline.Sensitivity = { .bEnabled = true, .Scale = glm::vec3(settings.StickTurnRate) };
		lookStick.Pipeline.LookAcceleration = { .bEnabled = true };
		lookStick.Pipeline.bMultiplyByDeltaTime = true;
		orbit.Bindings.push_back(lookStick);

		// The wheel's notches away from the user zoom in
		InputBindingData zoom;
		zoom.Action = "OrbitZoom";
		zoom.Control = "Mouse/Wheel";
		zoom.SwizzleX = 1;
		zoom.SwizzleY = -1;
		zoom.SwizzleZ = -1;
		orbit.Bindings.push_back(zoom);

		InputBindingData lookButton;
		lookButton.Action = "OrbitLookButton";
		lookButton.Control = "Mouse/Right";
		orbit.Bindings.push_back(lookButton);

		// The mouse moves a distance, not a rate: no delta time. Its y points down; the look's up.
		InputContextData mouseLook;
		mouseLook.Name = settings.MouseLookContextName;
		mouseLook.Priority = settings.Priority + 10;
		mouseLook.Cursor = EContextCursor::Captured;

		InputBindingData lookMouse;
		lookMouse.Action = "OrbitLook";
		lookMouse.Control = "Mouse/Delta";
		lookMouse.Pipeline.Sensitivity = { .bEnabled = true, .Scale = glm::vec3(settings.MouseSensitivity), .bInvertY = true };
		mouseLook.Bindings.push_back(lookMouse);

		// A request, not bindings: what is on decides how the mouse looks
		InputContextData freeLook;
		freeLook.Name = settings.FreeLookContextName;
		freeLook.Priority = settings.Priority + 5;

		return { orbit, mouseLook, freeLook };
	}

	OrbitCameraInput::OrbitCameraInput(InputSystem& input, Settings settings)
		: Input(input)
		, OrbitSettings(std::move(settings))
	{
		// Without an input asset that has them, the defaults
		if (!Input.FindContext(OrbitSettings.OrbitContextName).IsValid() || !Input.FindContext(OrbitSettings.MouseLookContextName).IsValid() ||
			!Input.FindContext(OrbitSettings.FreeLookContextName).IsValid())
		{
			InputAssetData defaults;
			defaults.Actions = MakeActionsData();
			defaults.Contexts = MakeContextsData(OrbitSettings);
			ApplyInputAsset(Input, defaults);
		}

		OrbitActions.Look = Input.FindAction("OrbitLook");
		OrbitActions.Zoom = Input.FindAction("OrbitZoom");
		OrbitActions.LookButton = Input.FindAction("OrbitLookButton");
		OrbitContext = Input.FindContext(OrbitSettings.OrbitContextName);
		MouseLookContext = Input.FindContext(OrbitSettings.MouseLookContextName);
		FreeLookContext = Input.FindContext(OrbitSettings.FreeLookContextName);
	}

	void OrbitCameraInput::SetActive(bool bOrbiting, bool bMouseLooking, std::string reason, void* lookWindow)
	{
		InputUser& user = Input.GetUser();
		user.SetContextActive(OrbitContext, bOrbiting, { .Reason = bOrbiting ? reason : std::string() });
		user.SetContextActive(MouseLookContext, bMouseLooking, { .Reason = bMouseLooking ? std::move(reason) : std::string(), .Window = lookWindow });
	}

	OrbitCameraInput::Values OrbitCameraInput::Read() const
	{
		const InputUser& user = Input.GetUser();
		const ActionState& lookButton = user.Get(OrbitActions.LookButton);

		Values values;
		values.Look = user.Get(OrbitActions.Look).Vec2();
		values.ZoomSteps = user.Get(OrbitActions.Zoom).Float();
		values.bLookButtonStarted = lookButton.bStarted;
		values.bLookButtonHeld = lookButton.IsTriggered();
		values.bFreeLook = user.IsContextActive(FreeLookContext);
		return values;
	}

	bool UpdateOrbitCameras(Registry& world, OrbitCameraInput& input)
	{
		const OrbitCameraInput::Values values = input.Read();

		bool bAnyCamera = false;
		bool bAnyMouseLook = false;
		world.Each<OrbitCameraComponent>(
			[&](Entity entity, OrbitCameraComponent& orbit)
			{
				if (!world.Has<TransformComponent>(entity))
				{
					return;
				}
				bAnyCamera = true;

				// Free look turns with the mouse at once; otherwise the right button held does, the
				// mouse look context coming on from the next frame, so the click's own mouse moves
				// don't turn the camera
				orbit.bMouseLooking = values.bFreeLook || (orbit.bMouseLooking ? values.bLookButtonHeld : values.bLookButtonStarted);
				bAnyMouseLook = bAnyMouseLook || orbit.bMouseLooking;

				orbit.LookDelta += values.Look;
				orbit.ZoomDelta += values.ZoomSteps;
			});

		input.SetActive(bAnyCamera, bAnyMouseLook,
			!bAnyCamera            ? "No orbit camera in the world"
				: values.bFreeLook ? "Free look asked for"
				: bAnyMouseLook    ? "Right mouse button held"
								   : "The world has an orbit camera");
		return bAnyCamera;
	}
}
