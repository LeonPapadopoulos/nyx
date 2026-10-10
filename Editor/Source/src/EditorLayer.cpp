#include "EditorLayer.h"

#include "Application.h"
#include "Assertions.h"
#include "Renderer.h"
#include "CameraComponent.h"
#include "DirectionalLightComponent.h"
#include "MeshRendererComponent.h"
#include "TransformComponent.h"
#include "NameComponent.h"
#include "ComponentTypeRegistry.h"
#include "GameLaunchOptions.h"
#include "PlayWall.h"
#include "TransactionObjectRef.h"
#include "TransactionObjectRefHelpers.h"
#include "RootObjectSnapshotUtils.h"
#include "PropertyWidgetRegistry.h"
#include "ReflectedPropertyDrawer.h"
#include "SceneSerializer.h"
#include "Paths.h"
#include "SourceNavigation.h"
#include "ImGuiSource.h"
#include "ReflectionSourceRegistry.h"

#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <string>
#include <glm/glm.hpp>
#include <InspectorTargetIdHelpers.h>

#include "ReflectedPropertyRef.h"
#include "ReflectionTypes.h"
#include "Log.h"

void PrintTransformMetadata()
{
	const Nyx::Reflection::TypeMetadata& type =
		Nyx::Reflection::GetTypeMetadata<Nyx::Engine::TransformComponent>();

	CORE_LOG_INFO("Reflected type: {}", type.Name);

	for (size_t i = 0; i < type.PropertyCount; ++i)
	{
		const auto& prop = type.Properties[i];
		CORE_LOG_INFO("  Property {}: {}", static_cast<int>(i), prop.Name);
	}
}

namespace
{
	bool InputTextString(const char* label, std::string& value)
	{
		char buffer[256]{};
		const size_t copyLength = std::min(value.size(), sizeof(buffer) - 1);
		std::memcpy(buffer, value.data(), copyLength);
		buffer[copyLength] = '\0';

		if (NYX_UI(ImGui::InputText(label, buffer, sizeof(buffer))))
		{
			value = buffer;
			return true;
		}

		return false;
	}
}

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
		SourceInspector.Attach([this](SourceLocation location) { OpenUISource(location); });

		Window->SetStartupStatus("Preparing the scene and editor panels");
		Window->SetTitlebarMenu(
			[this](float buttonHeight)
			{
				DrawTitlebarMenu(buttonHeight);
			});

		Renderer->SetWorld(&ActiveScene.GetRegistry());

		{
			std::filesystem::create_directories(Nyx::Paths::GetAssetsDir());
			std::filesystem::create_directories(Nyx::Paths::GetScenesDir());
			std::filesystem::create_directories(Nyx::Paths::GetMeshesDir());
			std::filesystem::create_directories(Nyx::Paths::GetMaterialsDir());
			std::filesystem::create_directories(Nyx::Paths::GetTexturesDir());

			AssetDb.SetAssetRoot(Nyx::Paths::GetAssetsDir());
			AssetDb.Rescan();

			AssetActivationContext = std::make_unique<Nyx::Editor::EditorAssetActivationContext>(*this);
			AssetBrowser.SetActivationContext(AssetActivationContext.get());
			AssetBrowser.SetDatabase(&AssetDb);
			AssetBrowser.SetCurrentDirectory(std::filesystem::path("Scenes"));
		}

		Nyx::Editor::RegisterDefaultPropertyWidgets();

		MainSceneViewId = Renderer->CreateSceneView();
		SecondarySceneViewId = Renderer->CreateSceneView();

		{
			// @todo: Allow the user to switch Camera Modes on demand via editor UI and on per-view basis
			Renderer->SetSceneViewCameraMode(SecondarySceneViewId, EViewportCameraMode::EditorFreeCamera);
			// Give the 2nd camera a different starting position & rotation to make it distinctly different
			glm::vec3 camPosition = glm::vec3(6.0f, 2.0f, 0.0f);
			glm::vec3 camRotationRadians = glm::vec3(-0.25f, 0.5 * 3.1415 /* PI */, 0.0f);
			Renderer->SetSceneViewEditorCameraTransform(SecondarySceneViewId, camPosition, camRotationRadians);
		}

		SpawnTestScene();
		ResolveSceneRuntimeAssets();

		{
			const std::filesystem::path testScenePath = "TestScene.nyxscene";

			if (SaveCurrentScene(testScenePath))
			{
				const bool bLoaded = LoadCurrentScene(testScenePath);
				ASSERT(bLoaded && "Scene round-trip load failed.");
			}
		}

		// Example Code for accessig reflected Property Data on a given Entity
		{
			auto& world = ActiveScene.GetRegistry();
			Nyx::Engine::Entity entity = ActiveScene.CreateEntity("EditorLayer::OnAttach()");

			Nyx::Engine::TransformComponent& transform = world.Add<Nyx::Engine::TransformComponent>(
				entity,
				Nyx::Engine::TransformComponent{
					.Position = glm::vec3(-2.0f, 0.0f, 0.0f),
					.Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
					.Scale = glm::vec3(1.0f) });

			//Nyx::Engine::TransformComponent& transform =
			//	world.Get<Nyx::Engine::TransformComponent>(entity);

			Nyx::Reflection::ReflectedPropertyRef positionRef =
				Nyx::Reflection::FindReflectedProperty(transform, "Position");

			if (positionRef.IsValid())
			{
				glm::vec3& position = positionRef.Access<glm::vec3>();
				const char* category = positionRef.FindMetadataValue("Category");

				LOG_INFO(
					"EditorLayer::OnAttach() Entity: Position ({0},{1},{2}), Category {3}",
					position.x,
					position.y,
					position.z,
					category);
			}
		}

		Transactions.RegisterDomain(Nyx::Editor::EObjectDomain::SceneEntity, &SceneEntityDomain);

		// @todo: Remove once Reflection System debugging finished
		PrintTransformMetadata();
		// @todo: Remove once Undo/Redo Notifications have been tested
		Transactions.Subscribe(&TransactionSubscriber);

		// Edits, undo and redo load the assets they need, as loading a scene does
		AssetLoader.SetPostLoadContext(Nyx::Engine::ScenePostLoadContext{ .AssetResolver = AssetResolver.get() });
		Transactions.Subscribe(&AssetLoader);

		// Edits show up live in the game started with Play
		Transactions.Subscribe(&GameEdits);
	}

	void EditorLayer::OnDetach()
	{
		// Closing the editor also ends the games it started
		StopGamesBeforeEditorCloses();

		SourceInspector.Detach();
		// The menu calls into this layer, so it must not outlive it.
		Window->SetTitlebarMenu(nullptr);

		if (Renderer)
		{
			if (MainSceneViewId != 0)
			{
				Renderer->DestroySceneView(MainSceneViewId);
				MainSceneViewId = 0;
			}

			if (SecondarySceneViewId != 0)
			{
				Renderer->DestroySceneView(SecondarySceneViewId);
				SecondarySceneViewId = 0;
			}
		}
	}

	void EditorLayer::OnUpdate(float deltaTime)
	{
		DebugTools.BeforeFrame();
		UpdateGames();
		TickScene(deltaTime);

		// Keep renderer-facing selection state up to date before rendering
		Renderer->SetSelectedEntity(ActiveScene.GetSelection());
	}

	void EditorLayer::OnUI()
	{
		if (!SourceInspector.IsActive()) TransformGizmoInstance.TickHotkeys();

		ApplyPendingPickResults();

		DrawSceneFilePopups();
		if (bAssetBrowserVisible)
		{
			AssetBrowser.Draw();
		}

		DrawSceneOutliner();
		DrawDetailsPanel();
		DrawSceneViews();

		if (!SourceInspector.IsActive()) HandleUndoRedoHotkeys();
		DrawSourceTools();

		if (bShowGameLink)
		{
			GameLinkWindow.Draw(bShowGameLink);
		}

		DrawPlaySetupsWindow();

		DebugTools.DrawWindows();
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
				RequestSaveSceneAsPopup();
			}

			if (NYX_UI(ImGui::MenuItem("Load Scene")))
			{
				RequestLoadScenePopup();
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
			if (NYX_UI(ImGui::MenuItem("Preferences"))) bShowPreferences = true;

			if (NYX_UI(ImGui::MenuItem("Asset Browser")))
			{
				ToggleAssetBrowser();
			}

			// The game's log and the editor link's messages
			if (NYX_UI(ImGui::MenuItem("Game Link", nullptr, bShowGameLink)))
			{
				bShowGameLink = !bShowGameLink;
			}

			// Window shapes for Play and Play All
			if (NYX_UI(ImGui::MenuItem("Play Setups", nullptr, bShowPlaySetups)))
			{
				bShowPlaySetups = !bShowPlaySetups;
			}

			// Shows "Scene 2" the way the game sees the scene: through its primary camera, without editor overlays
			if (NYX_UI(ImGui::MenuItem("Game View in Scene 2", nullptr, bSecondaryViewShowsGameView)))
			{
				bSecondaryViewShowsGameView = !bSecondaryViewShowsGameView;

				Renderer->SetSceneViewCameraMode(
					SecondarySceneViewId,
					bSecondaryViewShowsGameView ? EViewportCameraMode::ScenePrimaryCamera : EViewportCameraMode::EditorFreeCamera);
				Renderer->SetSceneViewShowEditorOverlays(SecondarySceneViewId, !bSecondaryViewShowsGameView);
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
			ImGui::EndPopup();
		}

		ImGui::SameLine();

		// Play / Stop, green while stopped and red while any game runs. While games were asked to
		// quit, it says "Stopping", and a click ends them at once. Play has its own ID, so a click
		// that starts on Stop or Stopping can't end on Play when the games exit meanwhile.
		const bool bPlaying = AreGamesRunning();
		const bool bQuitting = std::any_of(Games.begin(), Games.end(),
			[](const std::unique_ptr<GameInstance>& game)
			{
				return game->IsQuitting();
			});
		const ImVec4 playButtonColor = bPlaying ? ImVec4(0.55f, 0.18f, 0.18f, 1.0f) : ImVec4(0.18f, 0.42f, 0.22f, 1.0f);
		const char* playButtonLabel = !bPlaying ? "Play" : (bQuitting ? "Stopping###Stop" : "Stop###Stop");

		ImGui::PushStyleColor(ImGuiCol_Button, playButtonColor);
		if (NYX_UI(ImGui::Button(playButtonLabel, ImVec2(64.0f, buttonHeight))))
		{
			if (bPlaying)
			{
				StopGames();
			}
			else
			{
				StartGames(false);
			}
		}
		ImGui::PopStyleColor();

		const PlaySetup& firstSetup = Preferences.PlaySetups.front();
		if (bPlaying && bQuitting)
		{
			ImGui::SetItemTooltip("The games were asked to quit. Click to end them at once.\nEditor link:%s", GetGameLinkStatus().c_str());
		}
		else if (bPlaying)
		{
			ImGui::SetItemTooltip("Asks the games to quit, or ends those that aren't linked.\nEditor link:%s", GetGameLinkStatus().c_str());
		}
		else
		{
			ImGui::SetItemTooltip("Runs the open scene in NyxGame, unsaved changes included, as %s (%ux%u).\nRight-click for options.",
				firstSetup.Name.c_str(), firstSetup.Width, firstSetup.Height);
		}

		if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
		{
			ImGui::OpenPopup("##PlayOptions");
		}

		// Play All, next to Play. Disabled while games run rather than hidden, so the buttons stay in place.
		ImGui::SameLine();
		const size_t playAllCount = static_cast<size_t>(std::count_if(Preferences.PlaySetups.begin(), Preferences.PlaySetups.end(),
			[](const PlaySetup& setup)
			{
				return setup.bInPlayAll;
			}));

		ImGui::BeginDisabled(bPlaying);
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.42f, 0.22f, 1.0f));
		if (NYX_UI(ImGui::Button("Play All", ImVec2(72.0f, buttonHeight))))
		{
			StartGames(true);
		}
		ImGui::PopStyleColor();
		ImGui::EndDisabled();

		ImGui::SetItemTooltip("Runs the open scene in %zu games side by side, one per play setup marked for Play All.\n"
							  "Every edit reaches all of them. Right-click for options.",
			playAllCount);

		if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
		{
			ImGui::OpenPopup("##PlayOptions");
		}

		DrawPlayOptionsMenu();

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

	void EditorLayer::OpenUISource(SourceLocation location)
	{
		if (PendingSourceOpen.valid())
		{
			SourceNavigationStatus = "A source editor is already opening. Please wait.";
			return;
		}
		SourceNavigationStatus = "Opening source editor...";
		PendingSourceOpen = std::async(std::launch::async, OpenSourceLocation, location, Preferences.SourceEditor);
	}

	void EditorLayer::DrawSourceTools()
	{
		if (PendingSourceOpen.valid() && PendingSourceOpen.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
		{
			SourceNavigationStatus = PendingSourceOpen.get();
			if (!SourceNavigationStatus.empty())
			{
				LOG_ERROR("{0}", SourceNavigationStatus);
				bShowPreferences = true;
			}
		}
		if (!bShowPreferences) return;

		// Keep preferences independently resizable, including when an older layout docked it.
		ImGui::SetNextWindowSize(ImVec2(640.0f, 300.0f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSizeConstraints(ImVec2(360.0f, 180.0f), ImVec2(FLT_MAX, FLT_MAX));
		if (Nyx::UI::Begin("Preferences", &bShowPreferences))
		{
			NYX_UI(ImGui::TextUnformatted("Source navigation"));
			int selectedEditor = static_cast<int>(Preferences.SourceEditor);
			if (NYX_UI(ImGui::Combo("Source editor", &selectedEditor, "Visual Studio\0Visual Studio Code\0")))
			{
				Preferences.SourceEditor = static_cast<ESourceEditor>(selectedEditor);
				if (!Preferences.Save(EditorPreferences::GetUserFile()))
					SourceNavigationStatus = "Could not save user preferences.";
			}
			ImGui::PushTextWrapPos(0.0f);
			NYX_UI(ImGui::TextUnformatted("F8: inspect UI. Click: declaration. Shift-click: widget code. Escape: cancel."));
			NYX_UI(ImGui::TextUnformatted("Open menus before inspecting them. Finish active edits before pressing F8."));
			NYX_UI(ImGui::TextUnformatted("Source locations match the compiled code; rebuild after changing source lines."));
			NYX_UI(ImGui::TextDisabled("Preferences: %s", EditorPreferences::GetUserFile().string().c_str()));
			ImGui::PopTextWrapPos();
			if (!SourceNavigationStatus.empty()) NYX_UI(ImGui::TextWrapped("%s", SourceNavigationStatus.c_str()));
			if (!SourceInspector.GetStatus().empty()) NYX_UI(ImGui::TextWrapped("%s", SourceInspector.GetStatus().c_str()));
		}
		ImGui::End();
	}

	bool EditorLayer::SaveCurrentScene(const std::filesystem::path& path)
	{
		if (!Nyx::Engine::SceneSerializer::SaveToFile(ActiveScene.GetRegistry(), path))
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
		ActiveScene.GetRegistry() = std::move(loadedWorld);
		Renderer->SetWorld(&ActiveScene.GetRegistry());
		CurrentScenePath = path;

		LOG_INFO("Loaded scene from '{0}'", path.string());
		return true;
	}

	bool EditorLayer::SaveScene()
	{
		if (CurrentScenePath.empty())
		{
			bOpenSaveSceneAsPopup = true;

			const std::string defaultName = "UntitledScene";
			std::fill(SaveSceneAsBuffer.begin(), SaveSceneAsBuffer.end(), '\0');
			std::memcpy(
				SaveSceneAsBuffer.data(),
				defaultName.c_str(),
				(std::min)(defaultName.size(), SaveSceneAsBuffer.size() - 1));

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
		ActiveScene.GetRegistry().Clear();
		Renderer->SetWorld(&ActiveScene.GetRegistry());
		CurrentScenePath.clear();
		return true;
	}

	void EditorLayer::ForgetPreviousScene()
	{
		// Cancel before destroying the components captured by these unfinished edits.
		DetailsPanelContext.CancelPendingEdits();
		TransformGizmoInstance.CancelInteraction();

		// Undo steps and the selection refer to entities by handle. In the new scene, the same
		// handles name other entities, so undo would change the wrong ones.
		ActiveScene.GetSelection().reset();
		Transactions.Clear();
		++SceneRevision;

		// The games are still running the previous scene. A game that is quitting already gets the
		// rest of its time.
		for (const std::unique_ptr<GameInstance>& game : Games)
		{
			if (!game->IsQuitting())
			{
				game->Stop();
			}
		}
		GameEdits.EndSession();
	}

	void EditorLayer::StartGames(bool bPlayAll)
	{
		// Play All tiles the games on the monitor the editor is on
		const PlayWallScreen screen = GetPlayWallScreen(ImGui::GetMainViewport()->PlatformHandleRaw);
		const GamePlan plan = PlanGames(Preferences, bPlayAll, screen);
		if (plan.Games.empty())
		{
			LOG_WARNING("Play All: no play setup is marked for Play All (Play's right-click menu, or Window > Play Setups)");
			return;
		}

		if (!plan.bFits)
		{
			LOG_WARNING("Play All: {0} games don't fit on this monitor even at {1}%; some windows go past its bottom edge",
				plan.Games.size(), static_cast<int>(MinPlayWallScale * 100.0f));
		}
		else if (plan.Scale < 1.0f)
		{
			LOG_INFO("Play All: the games are shown at {0}% of their setups' sizes, to fit on this monitor",
				static_cast<int>(plan.Scale * 100.0f));
		}

		// The games run a copy of the open scene, saved next to the executables. That includes
		// unsaved changes and leaves the scene's own file alone. Each game reads it as it starts.
		const std::filesystem::path playSessionScene = Nyx::Paths::GetExecutableDir() / "PlaySession.nyxscene";
		if (!Nyx::Engine::SceneSerializer::SaveToFile(ActiveScene.GetRegistry(), playSessionScene))
		{
			LOG_ERROR("Couldn't save the scene for the game to '{0}'", playSessionScene.string());
			return;
		}

		// NyxGame is built next to the editor, since building the editor builds it too
		const std::filesystem::path gameExecutable = Nyx::Paths::GetExecutableDir() / "NyxGame.exe";

		GameLinkWindow.OnPlayStarted();

		for (const PlannedGame& planned : plan.Games)
		{
			Nyx::Engine::GameLaunchOptions launchOptions = planned.Options;
			launchOptions.ScenePath = playSessionScene;
			launchOptions.bWaitForDebugger = bGameWaitsForDebugger;

			auto game = std::make_unique<GameInstance>(planned.Name,
				[this](const std::string& gameName, Nyx::Engine::ELinkDirection direction, const Nyx::Net::Message& message)
				{
					GameLinkWindow.AddMessage(gameName, direction, message);
				});

			if (game->Start(gameExecutable, launchOptions, planned.bConsoleWindow))
			{
				Games.push_back(std::move(game));
			}
		}

		// Edits from now on are kept for the games that can take them, and sent once each is linked
		const bool bAnyTakesEdits = std::any_of(Games.begin(), Games.end(),
			[](const std::unique_ptr<GameInstance>& game)
			{
				return game->TakesEdits();
			});

		if (bAnyTakesEdits)
		{
			GameEdits.StartSession();
		}
	}

	void EditorLayer::StopGames()
	{
		// Stop pressed again while games are quitting: ends them all at once
		const bool bAnyQuitting = std::any_of(Games.begin(), Games.end(),
			[](const std::unique_ptr<GameInstance>& game)
			{
				return game->IsQuitting();
			});

		if (bAnyQuitting)
		{
			EndGamesNow();
			return;
		}

		for (const std::unique_ptr<GameInstance>& game : Games)
		{
			game->Stop();
		}

		// Edits from now on, e.g. to a scene opened meanwhile, aren't for these games
		GameEdits.EndSession();
	}

	void EditorLayer::EndGamesNow()
	{
		GameEdits.EndSession();

		for (const std::unique_ptr<GameInstance>& game : Games)
		{
			game->EndNow();
		}

		Games.clear();
	}

	void EditorLayer::StopGamesBeforeEditorCloses()
	{
		// All games are asked first, so they quit at the same time, and share one deadline
		const auto deadline = std::chrono::steady_clock::now() + GameInstance::QuitTimeLimit;

		for (const std::unique_ptr<GameInstance>& game : Games)
		{
			game->AskToQuitBeforeEditorCloses();
		}

		for (const std::unique_ptr<GameInstance>& game : Games)
		{
			game->FinishBeforeEditorCloses(deadline);
		}

		EndGamesNow();
	}

	void EditorLayer::UpdateGames()
	{
		// Each game gets every edit, in its own queue: one that sits in the debugger or crashed
		// only stops itself from getting edits
		const std::vector<Nyx::Net::Message> edits = GameEdits.TakeMessages();

		for (const std::unique_ptr<GameInstance>& game : Games)
		{
			game->QueueEdits(edits);
			game->Update();
		}

		// A game that exited, or was ended, logged it; it doesn't come back
		std::erase_if(Games,
			[](const std::unique_ptr<GameInstance>& game)
			{
				return !game->IsRunning();
			});

		const bool bAnyTakesEdits = std::any_of(Games.begin(), Games.end(),
			[](const std::unique_ptr<GameInstance>& game)
			{
				return game->TakesEdits();
			});

		if (!bAnyTakesEdits && GameEdits.IsSessionActive())
		{
			GameEdits.EndSession();
		}
	}

	std::string EditorLayer::GetGameLinkStatus() const
	{
		std::string status;

		for (const std::unique_ptr<GameInstance>& game : Games)
		{
			status += "\n" + game->GetName() + ": " + game->GetLinkStatus();
		}

		return status;
	}

	void EditorLayer::DrawPlayOptionsMenu()
	{
		if (!Nyx::UI::BeginPopup("##PlayOptions"))
		{
			return;
		}

		if (NYX_UI(ImGui::MenuItem("Play All", nullptr, false, !AreGamesRunning())))
		{
			StartGames(true);
		}

		// Checkboxes rather than menu items, so the menu stays open while choosing several
		bool bChanged = false;
		NYX_UI(ImGui::SeparatorText("In Play All"));
		for (size_t i = 0; i < Preferences.PlaySetups.size(); ++i)
		{
			PlaySetup& setup = Preferences.PlaySetups[i];
			ImGui::PushID(static_cast<int>(i));
			const std::string label = setup.Name + " (" + std::to_string(setup.Width) + "x" + std::to_string(setup.Height) + ")";
			bChanged |= NYX_UI(ImGui::Checkbox(label.c_str(), &setup.bInPlayAll));
			if (i == 0)
			{
				ImGui::SameLine();
				ImGui::TextDisabled("Play");
			}
			ImGui::PopID();
		}

		if (NYX_UI(ImGui::MenuItem("Play Setups...")))
		{
			bShowPlaySetups = true;
		}

		NYX_UI(ImGui::Separator());
		bChanged |= NYX_UI(ImGui::Checkbox("Console Windows in Play All", &Preferences.bConsoleWindowsInPlayAll));
		ImGui::SetItemTooltip("Play always shows the game's console. Their log reaches the Game Link window either way.");
		NYX_UI(ImGui::MenuItem("Game Waits for Debugger", nullptr, &bGameWaitsForDebugger));

		if (bChanged)
		{
			SavePreferences();
		}

		ImGui::EndPopup();
	}

	void EditorLayer::DrawPlaySetupsWindow()
	{
		if (!bShowPlaySetups)
		{
			return;
		}

		ImGui::SetNextWindowSize(ImVec2(560.0f, 420.0f), ImGuiCond_FirstUseEver);
		if (!Nyx::UI::Begin("Play Setups", &bShowPlaySetups))
		{
			ImGui::End();
			return;
		}

		ImGui::PushTextWrapPos(0.0f);
		NYX_UI(ImGui::TextUnformatted("Play uses the first setup. Play All starts a game for each setup marked in the first column, "
									  "side by side on the monitor the editor is on. Sizes are the game's picture, without titlebar and borders."));
		ImGui::PopTextWrapPos();

		std::vector<PlaySetup>& setups = Preferences.PlaySetups;
		bool bChanged = false;

		// Applied after the table, so the rows don't change while it is drawn
		std::optional<size_t> moveUp;
		std::optional<size_t> remove;

		const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
		if (ImGui::BeginTable("##PlaySetups", 5, flags))
		{
			ImGui::TableSetupColumn("Play All", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Width", ImGuiTableColumnFlags_WidthFixed, 90.0f);
			ImGui::TableSetupColumn("Height", ImGuiTableColumnFlags_WidthFixed, 90.0f);
			ImGui::TableSetupColumn("##Actions", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableHeadersRow();

			for (size_t i = 0; i < setups.size(); ++i)
			{
				PlaySetup& setup = setups[i];
				ImGui::PushID(static_cast<int>(i));
				ImGui::TableNextRow();

				ImGui::TableNextColumn();
				bChanged |= NYX_UI(ImGui::Checkbox("##InPlayAll", &setup.bInPlayAll));

				// Saved when the field is left, not with every key
				ImGui::TableNextColumn();
				std::array<char, 128> name{};
				std::memcpy(name.data(), setup.Name.data(), (std::min)(setup.Name.size(), name.size() - 1));
				ImGui::SetNextItemWidth(-FLT_MIN);
				if (NYX_UI(ImGui::InputText("##Name", name.data(), name.size())))
				{
					setup.Name = name.data();
				}
				bChanged |= ImGui::IsItemDeactivatedAfterEdit();

				for (uint32_t* side : { &setup.Width, &setup.Height })
				{
					ImGui::TableNextColumn();
					ImGui::PushID(side == &setup.Width ? "Width" : "Height");
					int value = static_cast<int>(*side);
					ImGui::SetNextItemWidth(-FLT_MIN);
					if (NYX_UI(ImGui::InputInt("##Side", &value, 0, 0)))
					{
						*side = static_cast<uint32_t>(std::clamp(value, 1, 16384));
					}
					bChanged |= ImGui::IsItemDeactivatedAfterEdit();
					ImGui::PopID();
				}

				ImGui::TableNextColumn();
				ImGui::BeginDisabled(i == 0);
				if (NYX_UI(ImGui::SmallButton("Up")))
				{
					moveUp = i;
				}
				ImGui::EndDisabled();
				ImGui::SameLine();
				ImGui::BeginDisabled(setups.size() == 1);
				if (NYX_UI(ImGui::SmallButton("Delete")))
				{
					remove = i;
				}
				ImGui::EndDisabled();

				ImGui::PopID();
			}

			ImGui::EndTable();
		}

		if (moveUp)
		{
			std::swap(setups[*moveUp], setups[*moveUp - 1]);
			bChanged = true;
		}

		// Play needs a setup, so the last one stays
		if (remove && setups.size() > 1)
		{
			setups.erase(setups.begin() + static_cast<std::ptrdiff_t>(*remove));
			bChanged = true;
		}

		if (NYX_UI(ImGui::Button("Add Setup")))
		{
			PlaySetup setup = setups.back();
			setup.Name = "New Setup";
			setups.push_back(setup);
			bChanged = true;
		}

		ImGui::SameLine();
		if (NYX_UI(ImGui::Button("Reset to Defaults")))
		{
			setups = GetDefaultPlaySetups();
			bChanged = true;
		}

		if (bChanged)
		{
			SavePreferences();
		}

		// What Play All would do now: the monitor's work area, and the games' windows on it
		NYX_UI(ImGui::SeparatorText("Play All on this monitor"));
		const PlayWallScreen screen = GetPlayWallScreen(ImGui::GetMainViewport()->PlatformHandleRaw);
		const GamePlan plan = PlanGames(Preferences, true, screen);

		if (plan.Games.empty())
		{
			NYX_UI(ImGui::TextDisabled("No setup is marked for Play All."));
		}
		else if (!plan.bFits)
		{
			NYX_UI(ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 1.0f), "%zu games don't fit, even at %d%%.", plan.Games.size(),
				static_cast<int>(MinPlayWallScale * 100.0f)));
		}
		else if (plan.Scale < 1.0f)
		{
			NYX_UI(ImGui::Text("%zu games, shown at %d%% of their sizes to fit.", plan.Games.size(), static_cast<int>(plan.Scale * 100.0f)));
		}
		else
		{
			NYX_UI(ImGui::Text("%zu games, at their full sizes.", plan.Games.size()));
		}

		const ScreenRect& area = screen.WorkArea;
		const ImVec2 available = ImGui::GetContentRegionAvail();
		if (area.Width > 0 && area.Height > 0 && available.x > 0.0f && available.y > 20.0f)
		{
			const float scale = (std::min)(available.x / static_cast<float>(area.Width), available.y / static_cast<float>(area.Height));
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			const auto toPreview = [&](int x, int y)
			{
				return ImVec2(origin.x + static_cast<float>(x - area.X) * scale, origin.y + static_cast<float>(y - area.Y) * scale);
			};

			ImDrawList* drawList = ImGui::GetWindowDrawList();
			drawList->AddRectFilled(toPreview(area.X, area.Y), toPreview(area.X + area.Width, area.Y + area.Height),
				ImGui::GetColorU32(ImGuiCol_FrameBg));

			// The visible windows, as they will sit on the screen
			for (const PlannedGame& game : plan.Games)
			{
				const Nyx::Engine::GameWindowPosition position = *game.Options.WindowPosition;
				const Nyx::Engine::GameWindowSize size = *game.Options.WindowSize;
				const int visibleX = position.X + screen.Frame.InvisibleLeft;
				const int visibleY = position.Y + screen.Frame.InvisibleTop;
				const int visibleWidth = static_cast<int>(size.Width) + screen.Frame.Left + screen.Frame.Right;
				const int visibleHeight = static_cast<int>(size.Height) + screen.Frame.Top + screen.Frame.Bottom;

				const ImVec2 min = toPreview(visibleX, visibleY);
				const ImVec2 max = toPreview(visibleX + visibleWidth, visibleY + visibleHeight);
				drawList->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_Button));
				drawList->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_Border));
				drawList->PushClipRect(min, max, true);
				drawList->AddText(ImVec2(min.x + 4.0f, min.y + 2.0f), ImGui::GetColorU32(ImGuiCol_Text), game.Name.c_str());
				drawList->PopClipRect();
			}

			ImGui::Dummy(ImVec2(static_cast<float>(area.Width) * scale, static_cast<float>(area.Height) * scale));
		}

		ImGui::End();
	}

	void EditorLayer::SavePreferences()
	{
		if (!Preferences.Save(EditorPreferences::GetUserFile()))
		{
			LOG_WARNING("Couldn't save the editor preferences to '{0}'", EditorPreferences::GetUserFile().string());
		}
	}

	void EditorLayer::RequestLoadScenePopup()
	{
		bOpenLoadScenePopup = true;
	}

	void EditorLayer::RequestSaveSceneAsPopup()
	{
		bOpenSaveSceneAsPopup = true;

		const std::string defaultName = "UntitledScene";
		std::fill(SaveSceneAsBuffer.begin(), SaveSceneAsBuffer.end(), '\0');
		std::memcpy(
			SaveSceneAsBuffer.data(),
			defaultName.c_str(),
			(std::min)(defaultName.size(), SaveSceneAsBuffer.size() - 1));
	}

	void EditorLayer::ResolveMeshRendererAssets(Nyx::Engine::MeshRendererComponent& component)
	{
		component.MeshAsset =
			component.Mesh.IsValid() ? AssetResolver->ResolveMesh(component.Mesh.Path) : nullptr;

		component.MaterialAsset =
			component.Material.IsValid() ? AssetResolver->ResolveMaterial(component.Material.Path) : nullptr;
	}

	void EditorLayer::ResolveSceneRuntimeAssets()
	{
		auto& world = ActiveScene.GetRegistry();

		world.Each<Nyx::Engine::MeshRendererComponent>(
			[this](Nyx::Engine::Entity /*entity*/, Nyx::Engine::MeshRendererComponent& component)
			{
				ResolveMeshRendererAssets(component);
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

	void EditorLayer::MapSceneImageMouseToPickPixel(
		const ImVec2& imageMin,
		const ImVec2& imageSize,
		const ImVec2& mousePos,
		const Nyx::Extent2D& extent,
		uint32_t& outPickX,
		uint32_t& outPickY)
	{
		const float localMouseX = mousePos.x - imageMin.x;
		const float localMouseY = mousePos.y - imageMin.y;

		const float normalizedX = std::clamp(localMouseX / imageSize.x, 0.0f, 1.0f);
		const float normalizedY = std::clamp(localMouseY / imageSize.y, 0.0f, 1.0f);

		outPickX = std::min(
			static_cast<uint32_t>(normalizedX * static_cast<float>(extent.Width)),
			extent.Width > 0 ? extent.Width - 1 : 0u);

		outPickY = std::min(
			static_cast<uint32_t>(normalizedY * static_cast<float>(extent.Height)),
			extent.Height > 0 ? extent.Height - 1 : 0u);
	}

	void EditorLayer::TickScene(float deltaTime)
	{
		auto& world = ActiveScene.GetRegistry();

		world.Each<Nyx::Engine::MeshRendererComponent>(
			[&](Nyx::Engine::Entity entity, Nyx::Engine::MeshRendererComponent& meshRenderer)
			{
				if (!meshRenderer.bVisible)
				{
					return;
				}

				if (!meshRenderer.MeshAsset || !meshRenderer.MaterialAsset)
				{
					return;
				}

				if (world.Has<Nyx::Engine::TransformComponent>(entity))
				{
					auto& transform = world.Get<Nyx::Engine::TransformComponent>(entity);
					//transform.RotationRadians.y += deltaTime;
				}
			});
	}

	void EditorLayer::DrawSceneOutliner()
	{
		if (!bShowSceneOutliner)
		{
			return;
		}

		if (!Nyx::UI::Begin("Scene Outliner", &bShowSceneOutliner))
		{
			ImGui::End();
			return;
		}

		auto& world = ActiveScene.GetRegistry();
		auto& selection = ActiveScene.GetSelection();

		TransactionContext.ActiveScene = &ActiveScene;

		if (NYX_UI(ImGui::Button("Add Entity")))
		{
			Nyx::Engine::Entity newEntity = ActiveScene.CreateEntity("New Entity");
			selection = newEntity;

			const ObjectRef rootRef = MakeSceneEntityRef(ActiveScene, newEntity);
			RootObjectSnapshot snapshot =
				CaptureRootObjectSnapshot(SceneEntityDomain, TransactionContext, rootRef);

			Change change{};
			change.Kind = EChangeKind::AddObject;
			change.Payload = AddObjectChange{
				.Target = rootRef,
				.AfterCreate = snapshot
			};

			Transaction transaction{};
			transaction.Label = "Create Entity";
			transaction.Changes.push_back(std::move(change));

			Transactions.Push(std::move(transaction));
		}

		ImGui::SameLine();

		const bool bHasSelection = selection.has_value();
		if (!bHasSelection)
		{
			ImGui::BeginDisabled();
		}

		if (NYX_UI(ImGui::Button("Delete Selected")) && bHasSelection)
		{
			const Nyx::Engine::Entity entityToDelete = selection.value();
			const ObjectRef rootRef = MakeSceneEntityRef(ActiveScene, entityToDelete);

			RootObjectSnapshot snapshot =
				CaptureRootObjectSnapshot(SceneEntityDomain, TransactionContext, rootRef);

			if (ActiveScene.DestroyEntity(entityToDelete))
			{
				Change change{};
				change.Kind = EChangeKind::DeleteObject;
				change.Payload = DeleteObjectChange{
					.Target = rootRef,
					.BeforeDelete = snapshot
				};

				Transaction transaction{};
				transaction.Label = "Delete Entity";
				transaction.Changes.push_back(std::move(change));

				Transactions.Push(std::move(transaction));
				selection.reset();
			}
		}

		if (!bHasSelection)
		{
			ImGui::EndDisabled();
		}

		NYX_UI(ImGui::Separator());

		world.Each<Nyx::Engine::NameComponent>(
			[&](Nyx::Engine::Entity entity, const Nyx::Engine::NameComponent& name)
			{
				const bool bSelected = selection.has_value() && selection.value() == entity;

				std::string label = name.Name + "##" + std::to_string(entity.Index());

				if (NYX_UI(ImGui::Selectable(label.c_str(), bSelected)))
				{
					selection = entity;
				}
			});

		if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered())
		{
			selection.reset();
		}

		ImGui::End();
	}

	void EditorLayer::DrawDetailsPanel()
	{
		if (!bShowDetailsPanel)
		{
			return;
		}

		if (!Nyx::UI::Begin("Details", &bShowDetailsPanel))
		{
			ImGui::End();
			return;
		}

		auto& selection = ActiveScene.GetSelection();
		auto& world = ActiveScene.GetRegistry();

		if (!selection.has_value())
		{
			NYX_UI(ImGui::TextUnformatted("No entity selected."));
			ImGui::End();
			return;
		}

		const Nyx::Engine::Entity selectedEntity = selection.value();

		if (!world.IsAlive(selectedEntity))
		{
			NYX_UI(ImGui::TextUnformatted("Selected entity is no longer valid."));
			selection.reset();
			ImGui::End();
			return;
		}

		NYX_UI(ImGui::Text("Entity: %u", selectedEntity.Index()));
		NYX_UI(ImGui::Separator());

		// @todo: Move away from manually hardcoding the visuals of
		// specific components (and fields) here; Consider DetailsPanel-,
		// and Property-Customizations like Unreal does it. Also, look
		// into code generation for the needed field meta data

		// A replacement scene can reuse entity handles. Give its widgets fresh IDs so ImGui
		// cannot carry an active drag or a cached text edit over from the previous scene.
		ImGui::PushID(std::to_string(SceneRevision).c_str());
		ImGui::PushID(static_cast<int>(selectedEntity.Value));

		// @todo: Find a more robust (and automated) naming approach,
		// so we can easily, and reliably avoid Naming collisions among UI elements.
		// (Currently being dodged by using '##SomeSubInfo')

		DetailsPanelContext.Transactions = &Transactions;
		DetailsPanelContext.CurrentTargetId = Nyx::Editor::MakeInspectorTargetId(selectedEntity);
		DetailsPanelContext.CurrentObjectRef = Nyx::Editor::MakeSceneEntityRef(ActiveScene, selectedEntity);

		// One collapsible section per component of the entity, showing its reflected properties
		for (const Nyx::Engine::ComponentTypeOps& componentType : Nyx::Engine::ComponentTypeRegistry::Get().GetAll())
		{
			void* component = componentType.Get(world, selectedEntity);
			if (!component)
			{
				continue;
			}

			const char* displayName = componentType.TypeMetadata->DisplayName;
			ImGui::PushID(displayName);
			UI::SourceDeclarationScope componentSource(ReflectionSourceRegistry::Get().Find(*componentType.TypeMetadata));

			if (NYX_UI(ImGui::CollapsingHeader(displayName, ImGuiTreeNodeFlags_DefaultOpen)))
			{
				// The component's own properties; the drawer extends the location for structs inside it
				DetailsPanelContext.CurrentLocation = {};
				Nyx::Editor::DrawReflectedTypeTable(component, *componentType.TypeMetadata, DetailsPanelContext);
			}

			ImGui::PopID();
		}

		ImGui::PopID();
		ImGui::PopID();
		ImGui::End();
	}

	void EditorLayer::DrawSceneViews()
	{
		if (bShowSceneView)
		{
			DrawSceneViewWindow("Scene", MainSceneViewId, bShowSceneView);
		}

		if (bShowSecondarySceneView)
		{
			DrawSceneViewWindow("Scene 2", SecondarySceneViewId, bShowSecondarySceneView);
		}
	}

	void EditorLayer::DrawSceneViewWindow(const char* title, uint64_t sceneViewId, bool& bOpen)
	{
		if (sceneViewId == 0)
		{
			return;
		}

		if (!Nyx::UI::Begin(title, &bOpen))
		{
			ImGui::End();
			return;
		}

		const bool bHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
		const bool bFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		Renderer->SetSceneViewHovered(sceneViewId, bHovered);
		Renderer->SetSceneViewFocused(sceneViewId, bFocused);

		const ImVec2 avail = ImGui::GetContentRegionAvail();
		Renderer->SetSceneViewSize(
			sceneViewId,
			static_cast<uint32_t>(avail.x),
			static_cast<uint32_t>(avail.y));

		if (!Renderer->WasSceneViewRecreatedThisFrame(sceneViewId))
		{
			NYX_UI(ImGui::Image(Renderer->GetSceneViewTextureId(sceneViewId), avail));

			const ImVec2 imageMin = ImGui::GetItemRectMin();
			const ImVec2 imageSize = ImGui::GetItemRectSize();
			const bool bImageHovered = ImGui::IsItemHovered();

			const bool bGizmoConsumedInteraction =
				TransformGizmoInstance.TickAndDraw(
					*Renderer,
					ActiveScene,
					Transactions,
					sceneViewId,
					imageMin,
					imageSize,
					bImageHovered);

			if (!bGizmoConsumedInteraction &&
				bImageHovered &&
				ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				const ImVec2 mousePos = ImGui::GetMousePos();

				Nyx::SceneViewCameraData viewData{};
				if (Renderer->GetSceneViewCameraData(sceneViewId, viewData) &&
					imageSize.x > 0.0f &&
					imageSize.y > 0.0f)
				{
					uint32_t pickX = 0;
					uint32_t pickY = 0;

					MapSceneImageMouseToPickPixel(
						imageMin,
						imageSize,
						mousePos,
						viewData.Extent,
						pickX,
						pickY);

					Renderer->RequestPick(sceneViewId, pickX, pickY);
				}
			}
		}
		else
		{
			NYX_UI(ImGui::Dummy(avail));
		}

		ImGui::End();
	}

	void EditorLayer::DrawSceneFilePopups()
	{
		if (bOpenLoadScenePopup)
		{
			ImGui::OpenPopup("Load Scene");
			bOpenLoadScenePopup = false;
		}

		if (bOpenSaveSceneAsPopup)
		{
			ImGui::OpenPopup("Save Scene As");
			bOpenSaveSceneAsPopup = false;
		}

		if (Nyx::UI::BeginPopupModal("Load Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			NYX_UI(ImGui::TextUnformatted("Scenes"));
			NYX_UI(ImGui::Separator());

			const std::vector<Nyx::Editor::AssetEntry> sceneEntries =
				AssetDb.GetChildren(std::filesystem::path("Scenes"));

			for (const Nyx::Editor::AssetEntry& entry : sceneEntries)
			{
				if (entry.bIsDirectory)
				{
					continue;
				}

				if (entry.TypeId != "Scene")
				{
					continue;
				}

				if (NYX_UI(ImGui::Selectable(entry.Name.c_str())))
				{
					LoadCurrentScene(entry.AbsolutePath);
					ImGui::CloseCurrentPopup();
				}
			}

			ImGui::Spacing();

			if (NYX_UI(ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))))
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}

		if (Nyx::UI::BeginPopupModal("Save Scene As", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			NYX_UI(ImGui::TextUnformatted("Save scene into Assets/Scenes"));
			NYX_UI(ImGui::Separator());

			ImGui::SetNextItemWidth(320.0f);
			NYX_UI(ImGui::InputText("File Name", SaveSceneAsBuffer.data(), SaveSceneAsBuffer.size()));

			ImGui::Spacing();

			if (NYX_UI(ImGui::Button("Save", ImVec2(120.0f, 0.0f))))
			{
				std::string fileName = SaveSceneAsBuffer.data();
				if (!fileName.empty())
				{
					if (std::filesystem::path(fileName).extension() != ".nyxscene")
					{
						fileName += ".nyxscene";
					}

					const std::filesystem::path savePath = Nyx::Paths::GetScenesDir() / fileName;
					SaveSceneAs(savePath);
					ImGui::CloseCurrentPopup();
				}
			}

			ImGui::SameLine();

			if (NYX_UI(ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))))
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}
	}

	void EditorLayer::SpawnTestScene()
	{
		auto& world = ActiveScene.GetRegistry();

		{
			Nyx::Engine::Entity e = ActiveScene.CreateEntity("Textured Cube");

			world.Add<Nyx::Engine::TransformComponent>(
				e,
				Nyx::Engine::TransformComponent{
					.Position = glm::vec3(-2.0f, 0.0f, 0.0f),
					.Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
					.Scale = glm::vec3(1.0f) });

			world.Add<Nyx::Engine::MeshRendererComponent>(
				e,
				Nyx::Engine::MeshRendererComponent{
					.Mesh = Nyx::Engine::AssetReference{
						.Type = "Mesh",
						.Path = "Meshes/Cube.nyxmesh" },
					.Material = Nyx::Engine::AssetReference{ .Type = "Material", .Path = "Materials/Textured.nyxmat" },
					.bVisible = true });
		}

		{
			Nyx::Engine::Entity e = ActiveScene.CreateEntity("Reflective Cube");

			world.Add<Nyx::Engine::TransformComponent>(
				e,
				Nyx::Engine::TransformComponent{
					.Position = glm::vec3(0.0f, 0.0f, 0.0f),
					.Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
					.Scale = glm::vec3(1.0f) });

			world.Add<Nyx::Engine::MeshRendererComponent>(
				e,
				Nyx::Engine::MeshRendererComponent{
					.Mesh = Nyx::Engine::AssetReference{
						.Type = "Mesh",
						.Path = "Meshes/Cube.nyxmesh" },
					.Material = Nyx::Engine::AssetReference{ .Type = "Material", .Path = "Materials/Reflective.nyxmat" },
					.bVisible = true });
		}

		{
			Nyx::Engine::Entity e = ActiveScene.CreateEntity("Untextured Cube");

			world.Add<Nyx::Engine::TransformComponent>(
				e,
				Nyx::Engine::TransformComponent{
					.Position = glm::vec3(2.0f, 0.0f, 0.0f),
					.Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
					.Scale = glm::vec3(1.0f) });

			world.Add<Nyx::Engine::MeshRendererComponent>(
				e,
				Nyx::Engine::MeshRendererComponent{
					.Mesh = Nyx::Engine::AssetReference{
						.Type = "Mesh",
						.Path = "Meshes/Cube.nyxmesh" },
					.Material = Nyx::Engine::AssetReference{ .Type = "Material", .Path = "Materials/Untextured.nyxmat" },
					.bVisible = true });
		}

		{
			// In front of the cubes, looking slightly down at them
			Nyx::Engine::Entity e = ActiveScene.CreateEntity("Main Camera");

			world.Add<Nyx::Engine::TransformComponent>(
				e,
				Nyx::Engine::TransformComponent{
					.Position = glm::vec3(0.0f, 2.0f, 6.0f),
					.Rotation = glm::quat(glm::radians(glm::vec3(-15.0f, 0.0f, 0.0f))),
					.Scale = glm::vec3(1.0f) });

			world.Add<Nyx::Engine::CameraComponent>(e, Nyx::Engine::CameraComponent{});
		}

		{
			// Shines down from the same direction as the fixed light of the editor views
			Nyx::Engine::Entity e = ActiveScene.CreateEntity("Sun");

			world.Add<Nyx::Engine::TransformComponent>(
				e,
				Nyx::Engine::TransformComponent{
					.Position = glm::vec3(0.0f, 5.0f, 0.0f),
					.Rotation = glm::quat(glm::radians(glm::vec3(-65.0f, 65.0f, 0.0f))),
					.Scale = glm::vec3(1.0f) });

			world.Add<Nyx::Engine::DirectionalLightComponent>(e, Nyx::Engine::DirectionalLightComponent{});
		}
	}

	void EditorLayer::ApplyPendingPickResults()
	{
		auto& selection = ActiveScene.GetSelection();

		for (uint64_t sceneViewId : { MainSceneViewId, SecondarySceneViewId })
		{
			if (sceneViewId == 0)
			{
				continue;
			}

			const Nyx::IRenderer::PickResult result = Renderer->ConsumeLastPickResult(sceneViewId);
			if (!result.bHasNewResult)
			{
				continue;
			}

			if (!result.HitEntity.has_value() || ActiveScene.GetRegistry().IsAlive(*result.HitEntity))
			{
				selection = result.HitEntity;
			}
		}
	}

	void EditorLayer::HandleUndoRedoHotkeys()
	{
		TransactionContext.ActiveScene = &ActiveScene;

		const bool bTextInputActive = ImGui::GetIO().WantTextInput;
		if (bTextInputActive)
		{
			return;
		}

		// The assets of what undo and redo changed are loaded by AssetLoader (ComponentPostLoadSubscriber).
		// Undo and redo can bring an entity back in another slot, so the selection follows its guid
		auto& selection = ActiveScene.GetSelection();
		const Nyx::Engine::EntityGuid selectedGuid = selection ? ActiveScene.GetGuid(*selection) : Nyx::Engine::EntityGuid{};
		const auto KeepSelection = [&]()
		{
			if (selectedGuid.IsValid())
			{
				selection = ActiveScene.FindEntity(selectedGuid);
			}
		};

		if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z))
		{
			if (Transactions.Undo(TransactionContext))
			{
				KeepSelection();
			}
		}
		else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z) ||
			ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y))
		{
			if (Transactions.Redo(TransactionContext))
			{
				KeepSelection();
			}
		}
	}
}