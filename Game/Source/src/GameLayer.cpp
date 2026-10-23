#include "GameLayer.h"

#include "Application.h"
#include "CameraComponent.h"
#include "CameraProjection.h"
#include "ImGuiInput.h"
#include "Log.h"
#include "EditorRenderer.h"
#include "PhysicsPick.h"
#include "SceneSerializer.h"

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
		FlyCamera = std::make_unique<GameFlyCamera>(*Input);

#if defined(ENGINE_DEBUG)
		CrashOnPurpose = Input->AddAction({ "CrashOnPurpose", Nyx::Input::EActionValue::Button });
		Nyx::Input::InputBinding crash =
			Nyx::Input::InputBinding::ForControl(CrashOnPurpose, Nyx::Input::Key(Nyx::Input::EKey::C), Nyx::Input::ETriggerKind::Pressed);
		crash.Modifiers = Nyx::Input::EModifierKeys::Ctrl | Nyx::Input::EModifierKeys::Alt | Nyx::Input::EModifierKeys::Shift;
		TogglePhysicsDebugView = Input->AddAction({ "TogglePhysicsDebugView", Nyx::Input::EActionValue::Button });
		const Nyx::Input::InputBinding physicsDebugView = Nyx::Input::InputBinding::ForControl(
			TogglePhysicsDebugView, Nyx::Input::Key(Nyx::Input::EKey::F3), Nyx::Input::ETriggerKind::Pressed);
		DebugContext = Input->AddContext({ .Name = "Debug", .Priority = 800, .Bindings = { crash, physicsDebugView } });
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
		if (!World.GetPhysicsDebugView() || !bGameViewHovered)
		{
			return;
		}
		const std::optional<Nyx::WorldCamera> camera = Nyx::FindPrimaryCamera(World.GetRegistry());
		if (!camera)
		{
			return;
		}

		const ImVec2 mouse = ImGui::GetMousePos();
		const Nyx::Ray ray =
			Nyx::ScreenPointToRay(camera->Transform, camera->Camera, GameViewSize, glm::vec2(mouse.x, mouse.y) - GameViewMin);
		if (const std::optional<Nyx::PhysicsPick> pick = Nyx::PickPhysicsBody(World, ray, camera->Camera.FarPlane))
		{
			const glm::vec3 color(0.1f, 0.9f, 1.0f);
			World.GetDebugDraw().Cross(pick->Point, 0.1f, color);
			World.GetDebugDraw().Arrow(pick->Point, pick->Point + pick->Normal * 0.5f, color);
		}
	}

	void GameLayer::OnUpdate(float deltaTime)
	{
		FlyCamera->Update(World.GetRegistry(), deltaTime);

#if defined(ENGINE_DEBUG)
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
#endif
	}

	void GameLayer::OnUI()
	{
		// One window without any decoration that covers the whole application window
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize);
		ImGui::SetNextWindowViewport(viewport->ID);

		const ImGuiWindowFlags windowFlags =
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::Begin("Game", nullptr, windowFlags);
		ImGui::PopStyleVar(2);

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

		ImGui::End();
	}
}
