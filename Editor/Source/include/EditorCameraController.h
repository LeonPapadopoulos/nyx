#pragma once

#include "InputSystem.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Nyx::Editor
{
	// Where the scene views' free cameras are kept: in the renderer, or a stand-in in tests
	class IEditorCameras
	{
	public:
		virtual ~IEditorCameras() = default;

		// False for a view that isn't there
		virtual bool GetCamera(uint64_t sceneViewId, glm::vec3& outPosition, glm::vec3& outRotationRadians) const = 0;
		virtual void SetCamera(uint64_t sceneViewId, const glm::vec3& position, const glm::vec3& rotationRadians) = 0;
	};

	// Flies the free cameras of the scene views. W/A/S/D move, Q/E down and up, while a view is
	// hovered or focused. Holding the right mouse button after clicking into a view looks around
	// with the mouse, with the cursor captured, and keeps flying that view until the button is let
	// go, wherever the cursor would be. The wheel over the view changes the speed.
	//
	// The scene view panels report themselves while they are drawn (ReportView), and Update() uses
	// the reports of the last frame drawn.
	class EditorCameraController
	{
	public:
		// A scene view panel, as it was drawn
		struct ViewState
		{
			uint64_t SceneViewId = 0;
			bool bHovered = false;
			bool bFocused = false;

			// Shows the scene through its own free camera, not through the game's
			bool bFreeCamera = true;

			// The OS window the panel is in, to capture the cursor there: a panel dragged out of
			// the editor has its own (ImGuiViewport::PlatformHandle, a GLFWwindow*)
			void* Window = nullptr;
		};

		struct Settings
		{
			// Metres per second; the wheel multiplies or divides it by SpeedStep per notch
			float StartSpeed = 5.0f;
			float SpeedStep = 1.2f;
			float MinSpeed = 0.25f;
			float MaxSpeed = 100.0f;

			// Radians per pixel the mouse moves
			float LookSensitivity = 0.003f;
		};

		// What keeps the cameras still in this frame
		struct Blockers
		{
			// The UI source inspector takes every click
			bool bInspectingSources = false;

			// ImGui has a text field active: keys are text, not movement
			bool bTypingText = false;
		};

		EditorCameraController();

		// Called by each scene view panel while it is drawn
		void ReportView(const ViewState& view);

		// Moves the camera of the view the input goes to. Call once per frame before rendering.
		void Update(Nyx::Input::InputSystem& input, float deltaTime, const Blockers& blockers, IEditorCameras& cameras);

		// The view being looked around with the mouse; 0 for none
		uint64_t GetLookingViewId() const
		{
			return LookingViewId;
		}

		float GetMoveSpeed() const
		{
			return MoveSpeed;
		}

		const Settings& GetSettings() const
		{
			return CameraSettings;
		}

	private:
		// The panels' reports since the last Update(): of the last frame drawn
		std::vector<ViewState> Views;

		uint64_t LookingViewId = 0;

		Settings CameraSettings;
		float MoveSpeed = 0.0f;
	};
}
