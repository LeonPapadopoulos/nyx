#pragma once

#include "AssetResolver.h"
#include "Entity.h"
#include "FlyCameras.h"
#include "GameWorld.h"
#include "InputDebugView.h"
#include "InputHistory.h"
#include "InputSystem.h"
#include "Layer.h"
#include "OrbitCameraInput.h"
#include "PlayerController.h"
#include "SceneSerializationTypes.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>

namespace Nyx
{
	class IRenderer;
	class IEditorRenderer;
}

namespace Nyx::Game
{
	// The whole game for now: loads one scene into its world, runs the world's systems each fixed
	// step, and shows it through the scene's primary camera (or the renderer's default camera if it
	// has none), filling the window. The game module adds the systems.
	class GameLayer : public Nyx::ILayer
	{
	public:
		explicit GameLayer(std::filesystem::path scenePath);

		void OnAttach(Nyx::Application& application) override;
		void OnDetach() override;
		void OnFixedUpdate(const Nyx::StepTime& step) override;
		void OnUpdate(float deltaTime) override;
		void OnLateUpdate(float deltaTime) override;
		void OnUI() override;

		// The world the game runs, e.g. for the editor's live edits. It stays at its address.
		Nyx::Registry& GetWorld()
		{
			return World.GetRegistry();
		}

		// What components need after they were loaded or changed, e.g. to load the mesh a
		// MeshRenderer refers to. Valid once the layer is attached.
		Nyx::ScenePostLoadContext GetPostLoadContext() const
		{
			return Nyx::ScenePostLoadContext{ AssetResolver.get() };
		}

	private:
		// With the physics debug view on, where the ray through the cursor hits a body, and the
		// surface's normal there (Debug builds)
		void DrawCursorPick();

		// The ray from the scene's primary camera through a pixel of the game's picture, from its top
		// left corner; empty without a camera or a picture
		std::optional<Nyx::Ray> MakeViewRay(const glm::vec2& pixel) const;
		// Through the cursor, while it is free and over the picture
		std::optional<Nyx::Ray> MakeCursorRay() const;

		std::filesystem::path ScenePath;

		Nyx::IRenderer* Renderer = nullptr;
		// The same renderer's UI part, for ImGui::Image() of the game's view
		Nyx::IEditorRenderer* UIRenderer = nullptr;
		std::unique_ptr<Nyx::AssetResolver> AssetResolver;

		// All entities of the loaded scene, and the systems that run them
		Nyx::GameWorld World;
		// The game module's player, turning input into the world's commands each frame
		// (GameModule::CreatePlayerController); without a module that has one, Nyx's own
		// (ThirdPersonPlayer)
		std::unique_ptr<Nyx::IPlayerController> Player;

		uint64_t GameViewId = 0;
		// Where the game view's image was last shown, in ImGui's screen coordinates, and whether the
		// cursor was over it
		glm::vec2 GameViewMin{ 0.0f };
		glm::vec2 GameViewSize{ 0.0f };
		bool bGameViewHovered = false;

		Nyx::Input::InputSystem* Input = nullptr;
		// For the factor the world is drawn by, which the orbit cameras follow it at
		const Nyx::FixedStepClock* SimulationClock = nullptr;

		// The fly camera's actions and contexts, for the world's FlyCameraComponents
		// (UpdateFlyCameras()); made in OnAttach
		std::unique_ptr<Nyx::Input::FlyCameraInput> FlyCameraInput;
		// The orbit camera's, for the world's OrbitCameraComponents (Input::UpdateOrbitCameras())
		std::unique_ptr<Nyx::Input::OrbitCameraInput> OrbitCameraInput;

		// Debug builds: Ctrl+Alt+Shift+C crashes the game on purpose, to try the editor's crash card;
		// F3 shows the physics debug view; Ctrl+Alt+Shift+I shows the input overlay, the editor's
		// Window > Input for the game
		Nyx::Input::ContextHandle DebugContext;
		Nyx::Input::ActionHandle CrashOnPurpose;
		Nyx::Input::ActionHandle TogglePhysicsDebugView;
		Nyx::Input::ActionHandle ToggleInputOverlay;
		bool bShowInputOverlay = false;
		Nyx::Input::InputHistory InputHistory;
		Nyx::Input::InputDebugView InputView;
	};
}
