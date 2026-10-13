#include "EditorCameraController.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace Nyx::Editor
{
	using Nyx::Input::EKey;
	using Nyx::Input::EMouseControl;
	using Nyx::Input::Key;
	using Nyx::Input::Mouse;

	EditorCameraController::EditorCameraController()
		: MoveSpeed(CameraSettings.StartSpeed)
	{
	}

	void EditorCameraController::ReportView(const ViewState& view)
	{
		Views.push_back(view);
	}

	void EditorCameraController::Update(Nyx::Input::InputSystem& input, float deltaTime, const Blockers& blockers, IEditorCameras& cameras)
	{
		const Nyx::Input::InputFrame& frame = input.GetFrame();

		// The panels report while they are drawn, after this; collect this frame's anew
		const std::vector<ViewState> views = std::move(Views);
		Views.clear();

		const auto findView = [&views](auto predicate) -> const ViewState*
		{
			const auto found = std::find_if(views.begin(), views.end(), predicate);
			return found != views.end() ? &*found : nullptr;
		};

		const bool bLookButtonDown = frame.IsDown(Mouse(EMouseControl::Right));

		// Looking around ends with the button, or when its view is gone or no longer free
		const ViewState* looking = findView(
			[this](const ViewState& view)
			{
				return LookingViewId != 0 && view.SceneViewId == LookingViewId && view.bFreeCamera;
			});
		if (!looking || !bLookButtonDown || blockers.bInspectingSources)
		{
			LookingViewId = 0;
			looking = nullptr;
		}

		if (blockers.bInspectingSources)
		{
			return;
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
		if (!target)
		{
			return;
		}

		// Looking around starts with a right click into the view; the mouse moves of that frame
		// happened before, so they don't turn the camera yet
		bool bStartedLooking = false;
		if (!looking && target->bHovered && bLookButtonDown && frame.WasPressed(Mouse(EMouseControl::Right)))
		{
			LookingViewId = target->SceneViewId;
			looking = target;
			bStartedLooking = true;
		}

		if (looking)
		{
			input.RequestCursor({ Nyx::Input::ECursorMode::Captured, looking->Window, "Editor fly camera" });
		}

		glm::vec3 position{ 0.0f };
		glm::vec3 rotation{ 0.0f };
		if (!cameras.GetCamera(target->SceneViewId, position, rotation))
		{
			return;
		}

		const float wheel = frame.GetValue(Mouse(EMouseControl::Wheel)).y;
		if (wheel != 0.0f && (target->bHovered || looking))
		{
			MoveSpeed = std::clamp(
				MoveSpeed * std::pow(CameraSettings.SpeedStep, wheel), CameraSettings.MinSpeed, CameraSettings.MaxSpeed);
		}

		if (looking && !bStartedLooking)
		{
			const glm::vec2 delta = frame.GetValue(Mouse(EMouseControl::Delta));
			rotation.y -= delta.x * CameraSettings.LookSensitivity;
			rotation.x -= delta.y * CameraSettings.LookSensitivity;
			rotation.x = glm::clamp(rotation.x, glm::radians(-89.0f), glm::radians(89.0f));
		}

		if (!blockers.bTypingText)
		{
			const glm::mat4 orientation =
				glm::rotate(glm::mat4(1.0f), rotation.y, glm::vec3(0.0f, 1.0f, 0.0f)) *
				glm::rotate(glm::mat4(1.0f), rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));

			const glm::vec3 forward = glm::normalize(glm::vec3(orientation * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
			const glm::vec3 right = glm::normalize(glm::vec3(orientation * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)));
			const glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);

			glm::vec3 move{ 0.0f };
			move += forward * (frame.IsDown(Key(EKey::W)) ? 1.0f : 0.0f);
			move -= forward * (frame.IsDown(Key(EKey::S)) ? 1.0f : 0.0f);
			move += right * (frame.IsDown(Key(EKey::D)) ? 1.0f : 0.0f);
			move -= right * (frame.IsDown(Key(EKey::A)) ? 1.0f : 0.0f);
			move += up * (frame.IsDown(Key(EKey::E)) ? 1.0f : 0.0f);
			move -= up * (frame.IsDown(Key(EKey::Q)) ? 1.0f : 0.0f);

			position += move * MoveSpeed * deltaTime;
		}

		cameras.SetCamera(target->SceneViewId, position, rotation);
	}
}
