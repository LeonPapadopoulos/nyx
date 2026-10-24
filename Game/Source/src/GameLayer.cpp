#include "GameLayer.h"

#include "Application.h"
#include "CameraComponent.h"
#include "CameraProjection.h"
#include "ImGuiInput.h"
#include "InputAsset.h"
#include "Log.h"
#include "EditorRenderer.h"
#include "OrbitCameras.h"
#include "PhysicsPick.h"
#include "ScatterInstances.h"
#include "SceneSerializer.h"
#include "ThirdPersonPlayer.h"
#include "UIChrome.h"

#include <imgui.h>

namespace Nyx::Game
{
	GameLayer::GameLayer(std::filesystem::path scenePath)
		: ScenePath(std::move(scenePath))
	{
	}

	void GameLayer::OnAttach(Nyx::Application& application)
	{
		Renderer = &application.GetRenderer();
		UIRenderer = &application.GetEditorRenderer();
		Input = &application.GetInput();
		SimulationClock = &application.GetSimulationClock();
		// With the player's tuning; without the asset, the cameras' built-in bindings
		Nyx::Input::LoadDefaultInput(*Input, false, true);
		FlyCameraInput = std::make_unique<Nyx::Input::FlyCameraInput>(
			*Input, Nyx::Input::FlyCameraInput::Settings{ .FlyContextName = "FlyCamera", .MouseLookContextName = "FlyCameraMouseLook" });
		OrbitCameraInput = std::make_unique<Nyx::Input::OrbitCameraInput>(*Input, Nyx::Input::OrbitCameraInput::Settings{});

#if defined(ENGINE_DEBUG)
		CrashOnPurpose = Input->AddAction({ "CrashOnPurpose", Nyx::Input::EActionValue::Button });
		Nyx::Input::InputBinding crash =
			Nyx::Input::InputBinding::ForControl(CrashOnPurpose, Nyx::Input::Key(Nyx::Input::EKey::C), Nyx::Input::ETriggerKind::Pressed);
		crash.Modifiers = Nyx::Input::EModifierKeys::Ctrl | Nyx::Input::EModifierKeys::Alt | Nyx::Input::EModifierKeys::Shift;
		TogglePhysicsDebugView = Input->AddAction({ "TogglePhysicsDebugView", Nyx::Input::EActionValue::Button });
		const Nyx::Input::InputBinding physicsDebugView = Nyx::Input::InputBinding::ForControl(
			TogglePhysicsDebugView, Nyx::Input::Key(Nyx::Input::EKey::F3), Nyx::Input::ETriggerKind::Pressed);
		ToggleCullingDebug = Input->AddAction({ "ToggleCullingDebug", Nyx::Input::EActionValue::Button });
		const Nyx::Input::InputBinding cullingDebug =
			Nyx::Input::InputBinding::ForControl(ToggleCullingDebug, Nyx::Input::Key(Nyx::Input::EKey::F4), Nyx::Input::ETriggerKind::Pressed);
		ToggleInputOverlay = Input->AddAction({ "ToggleInputOverlay", Nyx::Input::EActionValue::Button });
		Nyx::Input::InputBinding overlay =
			Nyx::Input::InputBinding::ForControl(ToggleInputOverlay, Nyx::Input::Key(Nyx::Input::EKey::I), Nyx::Input::ETriggerKind::Pressed);
		overlay.Modifiers = crash.Modifiers;
		DebugContext = Input->AddContext({ .Name = "Debug", .Priority = 800, .Bindings = { crash, physicsDebugView, cullingDebug, overlay } });
		Input->GetUser().SetContextActive(DebugContext, true, { .Reason = "Debug build" });
#endif

		// The game has no window layout to remember, and must not overwrite the editor's imgui.ini:
		// both programs run from the same folder.
		ImGui::GetIO().IniFilename = nullptr;

		AssetResolver = std::make_unique<Nyx::AssetResolver>(*Renderer);

		Nyx::ScenePostLoadContext postLoadContext{};
		postLoadContext.AssetResolver = AssetResolver.get();

		if (Nyx::SceneSerializer::LoadFromFile(ScenePath, World.GetRegistry(), postLoadContext))
		{
			LOG_INFO("Running scene '{0}'", ScenePath.string());
		}
		else
		{
			LOG_ERROR("Failed to load scene '{0}'", ScenePath.string());
		}

		bool bHasPrimaryCamera = false;
		World.GetRegistry().Each<Nyx::CameraComponent>(
			[&](Nyx::Entity /*entity*/, Nyx::CameraComponent& camera)
			{
				bHasPrimaryCamera = bHasPrimaryCamera || camera.bPrimary;
			});

		if (!bHasPrimaryCamera)
		{
			LOG_WARNING("The scene has no primary camera; showing it from the renderer's default camera");
		}

		Renderer->SetWorld(&World.GetRegistry());
		// What the world's systems draw, in the game's view
		Renderer->SetWorldDebugDraw(&World.GetRegistry(), &World.GetDebugDraw());
		// Moving bodies drawn between their last two steps, by the simulation clock
		Renderer->SetWorldInterpolation(&World.GetRegistry(), &application.GetSimulationClock());

		// The game module's systems, in their order; they run from the first fixed step on
		if (const auto createSystems = application.GetGameModule().CreateSystems)
		{
			createSystems(World);
			LOG_INFO("{0} simulation system(s)", World.GetSystems().size());
		}

		// The game module's player: its actions and contexts go on the input system now. Without one,
		// Nyx's own, which walks a character an orbit camera follows, through the same commands.
		if (const auto createPlayer = application.GetGameModule().CreatePlayerController)
		{
			Player = createPlayer(*Input);
		}
		else
		{
			Player = std::make_unique<Nyx::ThirdPersonPlayer>(*Input);
		}

		// The view the player sees: through the scene's camera, without the editor's grid and outlines
		GameViewId = Renderer->CreateSceneView();
		Renderer->SetSceneViewCameraMode(GameViewId, Nyx::EViewportCameraMode::ScenePrimaryCamera);
	}

	void GameLayer::OnDetach()
	{
		Renderer->DestroySceneView(GameViewId);
		Renderer->SetWorldDebugDraw(&World.GetRegistry(), nullptr);
		Renderer->SetWorldInterpolation(&World.GetRegistry(), nullptr);
		Renderer->SetWorld(nullptr);
	}

	void GameLayer::OnFixedUpdate(const Nyx::StepTime& step)
	{
		World.Step(step);
#if defined(ENGINE_DEBUG)
		// After the step, so it stays drawn until the next one
		DrawCursorPick();
#endif
	}

	void GameLayer::DrawCursorPick()
	{
		if (!World.GetPhysicsDebugView())
		{
			return;
		}
		const std::optional<Nyx::Ray> ray = MakeCursorRay();
		const std::optional<Nyx::WorldCamera> camera = Nyx::FindPrimaryCamera(World.GetRegistry());
		if (!ray || !camera)
		{
			return;
		}

		if (const std::optional<Nyx::PhysicsPick> pick = Nyx::PickPhysicsBody(World, *ray, camera->Camera.FarPlane))
		{
			const glm::vec3 color(0.1f, 0.9f, 1.0f);
			World.GetDebugDraw().Cross(pick->Point, 0.1f, color);
			World.GetDebugDraw().Arrow(pick->Point, pick->Point + pick->Normal * 0.5f, color);
		}
	}

	std::optional<Nyx::Ray> GameLayer::MakeViewRay(const glm::vec2& pixel) const
	{
		const std::optional<Nyx::WorldCamera> camera = Nyx::FindPrimaryCamera(World.GetRegistry());
		if (!camera || GameViewSize.x <= 0.0f || GameViewSize.y <= 0.0f)
		{
			return std::nullopt;
		}
		return Nyx::ScreenPointToRay(camera->Transform, camera->Camera, GameViewSize, pixel);
	}

	std::optional<Nyx::Ray> GameLayer::MakeCursorRay() const
	{
		if (!bGameViewHovered || Input->GetUser().GetCursor().Mode != Nyx::Input::ECursorMode::Free)
		{
			return std::nullopt;
		}
		const ImVec2 mouse = ImGui::GetMousePos();
		return MakeViewRay(glm::vec2(mouse.x, mouse.y) - GameViewMin);
	}

	void GameLayer::OnUpdate(float deltaTime)
	{
		// The cameras first, so the player's rays come from where they look this frame. The entities
		// with an OrbitCameraComponent, then those with a FlyCameraComponent; none: their contexts
		// stay off. An orbit camera wins over a fly camera.
		if (Nyx::Input::UpdateOrbitCameras(World.GetRegistry(), *OrbitCameraInput))
		{
			FlyCameraInput->SetActive(false, false, "The orbit camera has the input");
		}
		else
		{
			Nyx::Input::UpdateFlyCameras(World.GetRegistry(), *FlyCameraInput, deltaTime);
		}

		// The player's commands for this frame's steps, from where the camera looks now: a fly camera
		// as it just moved, an orbit camera as the picture on screen shows it (it follows its target
		// after the steps, in OnLateUpdate())
		if (Player)
		{
			Player->Update(Nyx::PlayerFrame{ .World = World,
				.Input = *Input,
				.Commands = World.GetCommands(),
				.CursorRay = MakeCursorRay(),
				.ViewCenterRay = MakeViewRay(GameViewSize * 0.5f),
				.DeltaSeconds = deltaTime });
		}

#if defined(ENGINE_DEBUG)
		InputHistory.Record(*Input);
		if (Input->GetUser().ConsumeTriggered(ToggleInputOverlay))
		{
			bShowInputOverlay = !bShowInputOverlay;
		}

		// Through a volatile pointer, so the compiler keeps the write
		if (Input->GetUser().ConsumeTriggered(CrashOnPurpose))
		{
			LOG_WARNING("Crashing on purpose (Ctrl+Alt+Shift+C)");
			volatile int* nowhere = nullptr;
			*nowhere = 1;
		}

		// F3 shows the bodies' shapes, contacts and constraints, and where the cursor points at one
		// (Debug builds only)
		if (Input->GetUser().ConsumeTriggered(TogglePhysicsDebugView))
		{
			const bool bShow = !World.GetPhysicsDebugView();
			World.SetPhysicsDebugView(bShow ? std::optional(Nyx::PhysicsDebugOptions{}) : std::nullopt);
			LOG_INFO("Physics debug view {0} (F3)", bShow ? "on" : "off");
		}

		// F4 shows the bounds the game view culls by, green drawn and red culled; again, it freezes
		// the culling where the camera is, to walk round and see what it left out; again, neither
		if (Input->GetUser().ConsumeTriggered(ToggleCullingDebug))
		{
			CullingDebug = CullingDebug == Nyx::ESceneViewCullingDebug::Off ? Nyx::ESceneViewCullingDebug::Bounds
				: CullingDebug == Nyx::ESceneViewCullingDebug::Bounds       ? Nyx::ESceneViewCullingDebug::Frozen
																			: Nyx::ESceneViewCullingDebug::Off;
			Renderer->SetSceneViewCullingDebug(GameViewId, CullingDebug);
			LOG_INFO("Culling debug view {0} (F4)", CullingDebug == Nyx::ESceneViewCullingDebug::Off ? "off" : CullingDebug == Nyx::ESceneViewCullingDebug::Bounds ? "showing the bounds" : "frozen");
		}
#endif
	}

	void GameLayer::OnLateUpdate(float deltaTime)
	{
		// On their targets as they are drawn this frame, after the frame's steps
		Nyx::UpdateOrbitCameras(World.GetRegistry(), deltaTime, static_cast<float>(SimulationClock->GetDrawFactor()));
		// Scatters follow their surfaces as they are after the steps and the live edits, also while
		// the world is paused
		Nyx::UpdateScatters(World.GetRegistry());
	}

	void GameLayer::OnUI()
	{
		// One window without any decoration that covers the whole application window
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize);
		ImGui::SetNextWindowViewport(viewport->ID);

		// Never in front of the input overlay, even when clicked
		const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking |
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::Begin("Game", nullptr, windowFlags);
		ImGui::PopStyleVar(2);
		// In front of the dock space's backdrop, which the flag above would leave over it
		Nyx::UI::ShowAboveBackdrop();

		// The game's picture: hovering it doesn't give the mouse to ImGui
		Nyx::Input::MarkAsView();

		// The game view always matches the window size
		const ImVec2 size = ImGui::GetContentRegionAvail();
		Renderer->SetSceneViewSize(GameViewId, static_cast<uint32_t>(size.x), static_cast<uint32_t>(size.y));

		const ImVec2 imageMin = ImGui::GetCursorScreenPos();
		GameViewMin = glm::vec2(imageMin.x, imageMin.y);
		GameViewSize = glm::vec2(size.x, size.y);
		bGameViewHovered = false;

		// Like the editor's scene views: skip the image in the frame the view was resized and its texture replaced
		if (!Renderer->WasSceneViewRecreatedThisFrame(GameViewId))
		{
			ImGui::Image(UIRenderer->GetSceneViewTextureId(GameViewId), size);
			bGameViewHovered = ImGui::IsItemHovered();
		}

		// The player's UI over the picture, in this window, so the input overlay stays in front
		if (Player)
		{
			Player->DrawUI(Nyx::PlayerView{
				.World = World, .PictureMin = GameViewMin, .PictureSize = GameViewSize, .Camera = Nyx::FindPrimaryCamera(World.GetRegistry()) });
		}

		ImGui::End();

#if defined(ENGINE_DEBUG)
		// Over the game: the mouse over it goes to ImGui (the game's window is a view, this isn't)
		if (bShowInputOverlay)
		{
			ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 20.0f, viewport->WorkPos.y + 20.0f), ImGuiCond_FirstUseEver);
			ImGui::SetNextWindowSize(ImVec2(640.0f, 560.0f), ImGuiCond_FirstUseEver);
			ImGui::SetNextWindowBgAlpha(0.92f);
			if (ImGui::Begin("Input (Ctrl+Alt+Shift+I)", &bShowInputOverlay, ImGuiWindowFlags_NoSavedSettings))
			{
				InputView.Draw(*Input, InputHistory);
			}
			ImGui::End();
		}
#endif
	}
}
