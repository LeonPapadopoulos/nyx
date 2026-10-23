#include "EditorCameraController.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace Nyx::Editor
{
	using namespace Nyx::Input;

	EditorCameraController::EditorCameraController(InputSystem& input)
		: Input(input)
		, MoveSpeed(CameraSettings.StartSpeed)
	{
		CameraActions.Move = Input.AddAction({ "Move", EActionValue::Axis3D });
		CameraActions.Look = Input.AddAction({ "Look", EActionValue::Axis2D });
		CameraActions.Speed = Input.AddAction({ "Speed", EActionValue::Axis1D });
		CameraActions.LookButton = Input.AddAction({ "LookButton", EActionValue::Button });

		InputContextDef fly;
		fly.Name = "EditorFlyCamera";
		fly.Priority = FlyCameraPriority;
		fly.Bindings.push_back(InputBinding::ForComposite(CameraActions.Move,
			CompositeKeys{
				.Right = Key(EKey::D),
				.Left = Key(EKey::A),
				.Up = Key(EKey::E),
				.Down = Key(EKey::Q),
				.Forward = Key(EKey::W),
				.Back = Key(EKey::S),
			}));

		InputBinding speed = InputBinding::ForControl(CameraActions.Speed, Mouse(EMouseControl::Wheel));
		speed.SourceAxis = 1;
		fly.Bindings.push_back(speed);

		fly.Bindings.push_back(InputBinding::ForControl(CameraActions.LookButton, Mouse(EMouseControl::Right)));
		FlyCameraContext = Input.AddContext(std::move(fly));

		InputContextDef look;
		look.Name = "EditorMouseLook";
		look.Priority = MouseLookPriority;
		look.Cursor = ECursorMode::Captured;
		InputBinding lookBinding = InputBinding::ForControl(CameraActions.Look, Mouse(EMouseControl::Delta));
		lookBinding.Pipeline.Sensitivity.bEnabled = true;
		lookBinding.Pipeline.Sensitivity.Scale = glm::vec3(CameraSettings.LookSensitivity);
		look.Bindings.push_back(lookBinding);
		MouseLookContext = Input.AddContext(std::move(look));
	}

	void EditorCameraController::ReportView(const ViewState& view)
	{
		Views.push_back(view);
	}

	void EditorCameraController::Update(float deltaTime, IEditorCameras& cameras)
	{
		InputUser& user = Input.GetUser();

		// The panels report while they are drawn, after this; collect this frame's anew
		const std::vector<ViewState> views = std::move(Views);
		Views.clear();

		const auto findView = [&views](auto predicate) -> const ViewState*
		{
			const auto found = std::find_if(views.begin(), views.end(), predicate);
			return found != views.end() ? &*found : nullptr;
		};

		const ActionState& lookButton = user.Get(CameraActions.LookButton);

		// Looking around ends with the button, or when its view is gone or no longer free
		const ViewState* looking = findView(
			[this](const ViewState& view)
			{
				return LookingViewId != 0 && view.SceneViewId == LookingViewId && view.bFreeCamera;
			});
		if (!looking || !lookButton.IsTriggered())
		{
			LookingViewId = 0;
			looking = nullptr;
		}

		// The view the input goes to: the one being looked around with, else the hovered one,
		// else the focused one
		const ViewState* target = looking;
		if (!target)
		{
			target = findView([](const ViewState& view)
				{
					return view.bFreeCamera && view.bHovered;
				});
		}
		if (!target)
		{
			target = findView([](const ViewState& view)
				{
					return view.bFreeCamera && view.bFocused;
				});
		}

		// Looking around starts with a right click into the view; the mouse moves of that frame
		// happened before, and the look context only comes on from the next frame
		bool bStartedLooking = false;
		if (target && !looking && target->bHovered && lookButton.bStarted)
		{
			LookingViewId = target->SceneViewId;
			looking = target;
			bStartedLooking = true;
		}

		// Why the contexts are on, for traces and the debug window
		std::string flyReason;
		std::string lookReason;
		if (target)
		{
			const std::string viewName = "Scene view " + std::to_string(target->SceneViewId);
			flyReason = viewName + (looking ? ": looking around" : " hovered");
			if (!looking && !target->bHovered)
			{
				flyReason = viewName + " focused";
			}
			lookReason = looking ? viewName + ": right mouse button held" : "";
		}
		user.SetContextActive(FlyCameraContext, target != nullptr, { .Reason = std::move(flyReason) });
		user.SetContextActive(MouseLookContext, looking != nullptr, { .Reason = std::move(lookReason), .Window = looking ? looking->Window : nullptr });

		if (!target)
		{
			return;
		}

		glm::vec3 position{ 0.0f };
		glm::vec3 rotation{ 0.0f };
		if (!cameras.GetCamera(target->SceneViewId, position, rotation))
		{
			return;
		}

		const float speed = user.Get(CameraActions.Speed).Float();
		if (speed != 0.0f)
		{
			MoveSpeed = std::clamp(MoveSpeed * std::pow(CameraSettings.SpeedStep, speed), CameraSettings.MinSpeed, CameraSettings.MaxSpeed);
		}

		if (looking && !bStartedLooking)
		{
			const glm::vec2 look = user.Get(CameraActions.Look).Vec2();
			rotation.y -= look.x;
			rotation.x -= look.y;
			rotation.x = glm::clamp(rotation.x, glm::radians(-89.0f), glm::radians(89.0f));
		}

		const glm::mat4 orientation =
			glm::rotate(glm::mat4(1.0f), rotation.y, glm::vec3(0.0f, 1.0f, 0.0f)) *
			glm::rotate(glm::mat4(1.0f), rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));

		const glm::vec3 forward = glm::normalize(glm::vec3(orientation * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
		const glm::vec3 right = glm::normalize(glm::vec3(orientation * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)));
		const glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);

		const glm::vec3 move = user.Get(CameraActions.Move).Vec3();
		position += (right * move.x + up * move.y + forward * move.z) * MoveSpeed * deltaTime;

		cameras.SetCamera(target->SceneViewId, position, rotation);
	}
}
