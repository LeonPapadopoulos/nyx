#pragma once

#include "FlyCameraInput.h"

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

	// Flies the free cameras of the scene views, through the contexts of a FlyCameraInput:
	// - EditorFlyCamera, on while a view is hovered or focused: W/A/S/D and the left stick move,
	//   E/Q and the triggers go up and down, the right stick turns, the wheel changes the speed;
	// - EditorMouseLook, above it, on while the right mouse button is held after a click into a
	//   view: the mouse looks around, with the cursor captured in that view's window. Looking keeps
	//   flying that view until the button is let go, wherever the cursor would be.
	// ImGui's context takes the keys while typing and the mouse over other UI, so neither moves
	// the camera.
	//
	// The scene view panels report themselves while they are drawn (ReportView), and Update() uses
	// the reports of the last frame drawn. Contexts turned on in Update() count from the next frame.
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
		};

		static constexpr int FlyCameraPriority = 100;

		// Adds the camera's actions and contexts to the input system, which must outlive it
		explicit EditorCameraController(Nyx::Input::InputSystem& input);

		// Called by each scene view panel while it is drawn
		void ReportView(const ViewState& view);

		// Moves the camera of the view the input goes to. Call once per frame before rendering.
		void Update(float deltaTime, IEditorCameras& cameras);

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

		const Nyx::Input::FlyCameraInput& GetInput() const
		{
			return Fly;
		}

	private:
		Nyx::Input::FlyCameraInput Fly;
		Settings CameraSettings;

		// The panels' reports since the last Update(): of the last frame drawn
		std::vector<ViewState> Views;

		uint64_t LookingViewId = 0;
		float MoveSpeed = 0.0f;
	};
}
