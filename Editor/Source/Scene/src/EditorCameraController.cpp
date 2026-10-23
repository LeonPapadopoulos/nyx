#include "EditorCameraController.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace Nyx::Editor
{
	using namespace Nyx::Input;

	EditorCameraController::EditorCameraController(InputSystem& input)
		: Fly(input, { .FlyContextName = "EditorFlyCamera", .MouseLookContextName = "EditorMouseLook", .Priority = FlyCameraPriority })
		, MoveSpeed(CameraSettings.StartSpeed)
	{
	}

	void EditorCameraController::ReportView(const ViewState& view)
	{
		Views.push_back(view);
	}

	void EditorCameraController::Update(float deltaTime, IEditorCameras& cameras)
	{
		const FlyCameraInput::Values input = Fly.Read();

		// The panels report while they are drawn, after this; collect this frame's anew
		const std::vector<ViewState> views = std::move(Views);
		Views.clear();

		const auto findView = [&views](auto predicate) -> const ViewState*
		{
			const auto found = std::find_if(views.begin(), views.end(), predicate);
			return found != views.end() ? &*found : nullptr;
		};

		// Looking around ends with the button, or when its view is gone or no longer free
		const ViewState* looking = findView(
			[this](const ViewState& view)
			{
				return LookingViewId != 0 && view.SceneViewId == LookingViewId && view.bFreeCamera;
			});
		if (!looking || !input.bLookButtonHeld)
		{
			LookingViewId = 0;
			looking = nullptr;
		}

		// The view the input goes to: the one being looked around with, else the hovered one,
		// else the focused one
		const ViewState* target = looking;
		if (!target)
		{
			target = findView(
				[](const ViewState& view)
				{
					return view.bFreeCamera && view.bHovered;
				});
		}
		if (!target)
		{
			target = findView(
				[](const ViewState& view)
				{
					return view.bFreeCamera && view.bFocused;
				});
		}

		// Looking around starts with a right click into the view; the mouse look context comes on
		// from the next frame, so the click's own mouse moves don't turn the camera
		if (target && !looking && target->bHovered && input.bLookButtonStarted)
		{
			LookingViewId = target->SceneViewId;
			looking = target;
		}

		// Why the contexts are on, for traces and the debug window
		std::string reason;
		if (target)
		{
			reason = "Scene view " + std::to_string(target->SceneViewId);
			if (looking)
			{
				reason += ": right mouse button held";
			}
			else
			{
				reason += target->bHovered ? " hovered" : " focused";
			}
		}
		Fly.SetActive(target != nullptr, looking != nullptr, std::move(reason), looking ? looking->Window : nullptr);

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

		if (input.SpeedSteps != 0.0f)
		{
			MoveSpeed = std::clamp(MoveSpeed * std::pow(CameraSettings.SpeedStep, input.SpeedSteps), CameraSettings.MinSpeed, CameraSettings.MaxSpeed);
		}

		rotation.y -= input.Look.x;
		rotation.x = glm::clamp(rotation.x + input.Look.y, glm::radians(-89.0f), glm::radians(89.0f));

		const glm::mat4 orientation =
			glm::rotate(glm::mat4(1.0f), rotation.y, glm::vec3(0.0f, 1.0f, 0.0f)) *
			glm::rotate(glm::mat4(1.0f), rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));

		const glm::vec3 forward = glm::normalize(glm::vec3(orientation * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
		const glm::vec3 right = glm::normalize(glm::vec3(orientation * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)));
		const glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);

		position += (right * input.Move.x + up * input.Move.y + forward * input.Move.z) * MoveSpeed * deltaTime;

		cameras.SetCamera(target->SceneViewId, position, rotation);
	}
}
