#include "EditorLayer.h"

#include "Application.h"
#include "Assertions.h"
#include "DefaultScene.h"
#include "ImGuiSource.h"
#include "Log.h"
#include "MeshRendererComponent.h"
#include "Paths.h"
#include "PropertyWidgetRegistry.h"
#include "ReflectionSourceRegistry.h"
#include "Renderer.h"
#include "SceneSerializer.h"

#include "imgui.h"

#include <glm/glm.hpp>

namespace Nyx::Editor
{
	void EditorLayer::OnAttach(Nyx::Engine::Application& application)
	{
		Window = &application.GetWindow();
		Renderer = &Window->GetRenderer();
		AssetResolver = std::make_unique<Nyx::Engine::BuiltinAssetResolver>(*Renderer);
		if (!Preferences.Load(EditorPreferences::GetUserFile()))
		{
			LOG_WARNING("Could not read editor preferences; using defaults.");
		}
		RegisterRuntimeReflectedSources();
		SourceInspector.Attach(
			[this](SourceLocation location)
			{
				PreferencesPanel.OpenSource(location, Preferences.SourceEditor);
			});

		Window->SetStartupStatus("Preparing the scene and editor panels");
		Window->SetTitlebarMenu(
			[this](float buttonHeight)
			{
				DrawTitlebarMenu(buttonHeight);
			});

		Renderer->SetWorld(&Editing.Scene.GetRegistry());

		{
			std::filesystem::create_directories(Nyx::Paths::GetAssetsDir());
			std::filesystem::create_directories(Nyx::Paths::GetScenesDir());
			std::filesystem::create_directories(Nyx::Paths::GetMeshesDir());
			std::filesystem::create_directories(Nyx::Paths::GetMaterialsDir());
			std::filesystem::create_directories(Nyx::Paths::GetTexturesDir());

			AssetDb.SetAssetRoot(Nyx::Paths::GetAssetsDir());
			AssetDb.Rescan();

			AssetActivationContext = std::make_unique<EditorAssetActivationContext>(*this);
			AssetBrowser.SetActivationContext(AssetActivationContext.get());
			AssetBrowser.SetDatabase(&AssetDb);
			AssetBrowser.SetCurrentDirectory(std::filesystem::path("Scenes"));
		}

		RegisterDefaultPropertyWidgets();

		MainSceneView.CreateView(*Renderer);
		SecondarySceneView.CreateView(*Renderer);

		{
			// @todo: Allow the user to switch Camera Modes on demand via editor UI and on per-view basis
			const uint64_t secondaryViewId = SecondarySceneView.GetViewId();
			Renderer->SetSceneViewCameraMode(secondaryViewId, EViewportCameraMode::EditorFreeCamera);
			// Give the 2nd camera a different starting position & rotation to make it distinctly different
			glm::vec3 camPosition = glm::vec3(6.0f, 2.0f, 0.0f);
			glm::vec3 camRotationRadians = glm::vec3(-0.25f, 0.5 * 3.1415 /* PI */, 0.0f);
			Renderer->SetSceneViewEditorCameraTransform(secondaryViewId, camPosition, camRotationRadians);
		}

		SpawnDefaultScene(Editing.Scene);
		ResolveSceneRuntimeAssets();

		{
			const std::filesystem::path testScenePath = "TestScene.nyxscene";

			if (SaveCurrentScene(testScenePath))
			{
				const bool bLoaded = LoadCurrentScene(testScenePath);
				ASSERT(bLoaded && "Scene round-trip load failed.");
			}
		}

		// Edits, undo and redo load the assets they need, as loading a scene does
		Editing.AssetLoader.SetPostLoadContext(Nyx::Engine::ScenePostLoadContext{ .AssetResolver = AssetResolver.get() });
		Editing.Transactions.Subscribe(&Editing.AssetLoader);

		// Edits show up live in the game started with Play
		Editing.Transactions.Subscribe(&Games.GetEditSubscriber());
	}

	void EditorLayer::OnDetach()
	{
		// Closing the editor also ends the games it started
		Games.StopBeforeEditorCloses();

		SourceInspector.Detach();
		// The menu calls into this layer, so it must not outlive it.
		Window->SetTitlebarMenu(nullptr);

		MainSceneView.DestroyView();
		SecondarySceneView.DestroyView();
		PainterTest.Release();
	}

	void EditorLayer::OnUpdate(float /*deltaTime*/)
	{
		DebugTools.BeforeFrame();

		Games.Update();

		// A game that crashed gets a crash card, with its last log lines
		for (GameCrash& crash : Games.TakeCrashes())
		{
			std::string lastLogLines = GameLinkWindow.GetLastLogLines(crash.GameName, CrashCardsPanel::LogLineCount);
			CrashCards.Add(std::move(crash), std::move(lastLogLines));
		}

		// Keep renderer-facing selection state up to date before rendering
		Renderer->SetSelectedEntity(Editing.Scene.GetSelection());
	}

	void EditorLayer::OnUI()
	{
		if (!SourceInspector.IsActive()) Editing.Gizmo.TickHotkeys();

		ApplyPendingPickResults();

		const SceneFileRequest sceneFileRequest = SceneFiles.Draw(AssetDb);
		if (sceneFileRequest.Load)
		{
			LoadCurrentScene(*sceneFileRequest.Load);
		}
		if (sceneFileRequest.SaveAs)
		{
			SaveSceneAs(*sceneFileRequest.SaveAs);
		}

		if (bAssetBrowserVisible)
		{
			AssetBrowser.Draw();
		}

		Outliner.Draw(Editing);
		Details.Draw(Editing);
		MainSceneView.Draw(Editing);
		SecondarySceneView.Draw(Editing);

		// Values being dragged or typed reach a running game every frame, not only when the edit ends
		Editing.DetailsContext.PreviewPendingEdits();
		Editing.Gizmo.PreviewActiveEdit(Editing.Transactions);

		if (!SourceInspector.IsActive()) Editing.HandleHotkeys();
		PreferencesPanel.Draw(Preferences, SourceInspector);

		if (bShowGameLink)
		{
			GameLinkWindow.Draw(bShowGameLink);
		}

		Play.DrawSetupsWindow(Preferences);

		if (const std::optional<GameCrash> restart = CrashCards.Draw())
		{
			Games.Restart(*restart);
		}

		DebugTools.DrawWindows();
		PainterTest.Draw(*Renderer);
	}

	void EditorLayer::DrawTitlebarMenu(float buttonHeight)
	{
		if (NYX_UI(ImGui::Button("File", ImVec2(56.0f, buttonHeight))))
		{
			ImGui::OpenPopup("##TitlebarFileMenu");
		}

		if (Nyx::UI::BeginPopup("##TitlebarFileMenu"))
		{
			if (NYX_UI(ImGui::MenuItem("New Scene")))
			{
				NewScene();
			}

			if (NYX_UI(ImGui::MenuItem("Save Scene")))
			{
				SaveScene();
			}

			if (NYX_UI(ImGui::MenuItem("Save Scene As")))
			{
				SceneFiles.RequestSaveAs();
			}

			if (NYX_UI(ImGui::MenuItem("Load Scene")))
			{
				SceneFiles.RequestLoad();
			}

			ImGui::EndPopup();
		}

		ImGui::SameLine();

		if (NYX_UI(ImGui::Button("Window", ImVec2(76.0f, buttonHeight))))
		{
			ImGui::OpenPopup("##TitlebarWindowMenu");
		}

		if (Nyx::UI::BeginPopup("##TitlebarWindowMenu"))
		{
			if (NYX_UI(ImGui::MenuItem("Inspect UI source", "F8", SourceInspector.IsActive())))
			{
				SourceInspector.RequestToggle();
			}
			if (NYX_UI(ImGui::MenuItem("Preferences"))) PreferencesPanel.bOpen = true;

			if (NYX_UI(ImGui::MenuItem("Asset Browser")))
			{
				bAssetBrowserVisible = !bAssetBrowserVisible;
			}

			// The game's log and the editor link's messages
			if (NYX_UI(ImGui::MenuItem("Game Link", nullptr, bShowGameLink)))
			{
				bShowGameLink = !bShowGameLink;
			}

			// Window shapes for Play and Play All
			if (NYX_UI(ImGui::MenuItem("Play Setups", nullptr, Play.bShowSetups)))
			{
				Play.bShowSetups = !Play.bShowSetups;
			}

			// Shows "Scene 2" the way the game sees the scene: through its primary camera, without editor overlays
			if (NYX_UI(ImGui::MenuItem("Game View in Scene 2", nullptr, bSecondaryViewShowsGameView)))
			{
				bSecondaryViewShowsGameView = !bSecondaryViewShowsGameView;
				SecondarySceneView.SetShowsGameView(bSecondaryViewShowsGameView);
			}

			ImGui::EndPopup();
		}

		ImGui::SameLine();
		if (NYX_UI(ImGui::Button("Debug", ImVec2(68.0f, buttonHeight))))
		{
			ImGui::OpenPopup("##TitlebarDebugMenu");
		}
		if (Nyx::UI::BeginPopup("##TitlebarDebugMenu"))
		{
			DebugTools.DrawMenu();
			ImGui::Separator();
			NYX_UI(ImGui::TextDisabled("Nyx"));
			NYX_UI(ImGui::MenuItem("Painter Test", nullptr, &PainterTest.bOpen));
			ImGui::EndPopup();
		}

		ImGui::SameLine();
		Play.DrawButtons(Games, Preferences, buttonHeight);

		// Name of the current scene, vertically centered next to the buttons
		ImGui::SameLine();
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (buttonHeight - ImGui::GetTextLineHeight()) * 0.5f);
		NYX_UI(ImGui::TextDisabled("| %s", GetCurrentSceneDisplayName().c_str()));
		if (SourceInspector.IsActive())
		{
			ImGui::SameLine();
			NYX_UI(ImGui::TextUnformatted("| Inspect UI: click source, Shift-click widget, Esc cancels"));
		}
	}

	bool EditorLayer::SaveCurrentScene(const std::filesystem::path& path)
	{
		if (!Nyx::Engine::SceneSerializer::SaveToFile(Editing.Scene.GetRegistry(), path))
		{
			LOG_ERROR("Failed to save scene to '{0}'", path.string());
			return false;
		}

		LOG_INFO("Saved scene to '{0}'", path.string());
		return true;
	}

	bool EditorLayer::LoadCurrentScene(const std::filesystem::path& path)
	{
		Nyx::Engine::ScenePostLoadContext postLoadContext{};
		postLoadContext.AssetResolver = AssetResolver.get();

		// Keep the current scene and its pending edits until the replacement is ready.
		Nyx::Engine::Registry loadedWorld;
		if (!Nyx::Engine::SceneSerializer::LoadFromFile(path, loadedWorld, postLoadContext))
		{
			LOG_ERROR("Failed to load scene from '{0}'", path.string());
			return false;
		}

		ForgetPreviousScene();
		Editing.Scene.GetRegistry() = std::move(loadedWorld);
		Renderer->SetWorld(&Editing.Scene.GetRegistry());
		CurrentScenePath = path;

		LOG_INFO("Loaded scene from '{0}'", path.string());
		return true;
	}

	bool EditorLayer::SaveScene()
	{
		if (CurrentScenePath.empty())
		{
			SceneFiles.RequestSaveAs();
			return false;
		}

		return SaveCurrentScene(CurrentScenePath);
	}

	bool EditorLayer::SaveSceneAs(const std::filesystem::path& path)
	{
		if (SaveCurrentScene(path))
		{
			CurrentScenePath = path;
			AssetDb.Rescan();
			return true;
		}

		return false;
	}

	bool EditorLayer::NewScene()
	{
		ForgetPreviousScene();
		Editing.Scene.GetRegistry().Clear();
		Renderer->SetWorld(&Editing.Scene.GetRegistry());
		CurrentScenePath.clear();
		return true;
	}

	void EditorLayer::ForgetPreviousScene()
	{
		Editing.ForgetScene();

		// The games are still running the previous scene
		Games.OnSceneReplaced();
	}

	void EditorLayer::ResolveSceneRuntimeAssets()
	{
		Editing.Scene.GetRegistry().Each<Nyx::Engine::MeshRendererComponent>(
			[this](Nyx::Engine::Entity /*entity*/, Nyx::Engine::MeshRendererComponent& component)
			{
				component.MeshAsset =
					component.Mesh.IsValid() ? AssetResolver->ResolveMesh(component.Mesh.Path) : nullptr;

				component.MaterialAsset =
					component.Material.IsValid() ? AssetResolver->ResolveMaterial(component.Material.Path) : nullptr;
			});
	}

	std::string EditorLayer::GetCurrentSceneDisplayName() const
	{
		if (CurrentScenePath.empty())
		{
			return "Untitled Scene";
		}

		return CurrentScenePath.filename().string();
	}

	void EditorLayer::ApplyPendingPickResults()
	{
		MainSceneView.ApplyPickResult(Editing);
		SecondarySceneView.ApplyPickResult(Editing);
	}
}