#include "EditorLayer.h"

#include "Assertions.h"
#include "Renderer.h"
#include "MeshRendererComponent.h"
#include "TransformComponent.h"

// @todo: include once its moved out of SceneDocument.h
//#include "NameComponent.h"
#include "SceneDocument.h"

#include <GLFW/glfw3.h>
#include "imgui.h"
#include <glm/glm.hpp>

namespace Nyx::Editor
{
	EditorLayer::EditorLayer(Nyx::IRenderer& inRenderer)
		: Renderer(&inRenderer)
	{
		ASSERT(Renderer != nullptr);
	}

	void EditorLayer::Initialize()
	{
		ASSERT(Renderer != nullptr);

		Renderer->SetWorld(&ActiveScene.GetRegistry());

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
	}

	void EditorLayer::Shutdown()
	{
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

	void EditorLayer::DrawPanels()
	{
		const float deltaTime = ComputeDeltaTime();
		TickScene(deltaTime);

		DrawSceneOutliner();
		DrawSceneViews();

		ImGui::ShowDemoWindow();
	}

	float EditorLayer::ComputeDeltaTime()
	{
		const double currentTime = glfwGetTime();

		if (LastFrameTimeSeconds == 0.0)
		{
			LastFrameTimeSeconds = currentTime;
			return 0.0f;
		}

		const float deltaTime = static_cast<float>(currentTime - LastFrameTimeSeconds);
		LastFrameTimeSeconds = currentTime;
		return deltaTime;
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
					transform.RotationRadians.y += deltaTime;
				}
			}
		);
	}

	void EditorLayer::DrawSceneOutliner()
	{
		if (!bShowSceneOutliner)
		{
			return;
		}

		if (!ImGui::Begin("Scene Outliner", &bShowSceneOutliner))
		{
			ImGui::End();
			return;
		}

		auto& world = ActiveScene.GetRegistry();
		auto& selection = ActiveScene.GetSelection();

		if (ImGui::Button("Add Entity"))
		{
			Nyx::Engine::Entity newEntity = ActiveScene.CreateEntity("New Entity");
			selection = newEntity;
		}

		ImGui::SameLine();

		const bool bHasSelection = selection.has_value();
		if (!bHasSelection)
		{
			ImGui::BeginDisabled();
		}

		if (ImGui::Button("Delete Selected") && bHasSelection)
		{
			ActiveScene.DestroyEntity(selection.value());
		}

		if (!bHasSelection)
		{
			ImGui::EndDisabled();
		}

		ImGui::Separator();

		world.Each<Nyx::Engine::NameComponent>(
			[&](Nyx::Engine::Entity entity, const Nyx::Engine::NameComponent& name)
			{
				const bool bSelected = selection.has_value() && selection.value() == entity;

				std::string label = name.Name + "##" + std::to_string(entity.Index());

				if (ImGui::Selectable(label.c_str(), bSelected))
				{
					selection = entity;
				}
			}
		);

		if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered())
		{
			selection.reset();
		}

		ImGui::End();
	}

	void EditorLayer::DrawSceneViews()
	{
		if (bShowSceneView && MainSceneViewId != 0)
		{
			if (ImGui::Begin("Scene", &bShowSceneView))
			{
				const bool bHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
				const bool bFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

				Renderer->SetSceneViewHovered(MainSceneViewId, bHovered);
				Renderer->SetSceneViewFocused(MainSceneViewId, bFocused);

				const ImVec2 avail = ImGui::GetContentRegionAvail();
				Renderer->SetSceneViewSize(
					MainSceneViewId,
					static_cast<uint32_t>(avail.x),
					static_cast<uint32_t>(avail.y)
				);

				if (!Renderer->WasSceneViewRecreatedThisFrame(MainSceneViewId))
				{
					ImGui::Image(Renderer->GetSceneViewTextureId(MainSceneViewId), avail);
				}
				else
				{
					ImGui::Dummy(avail);
				}
			}
			ImGui::End();
		}

		if (bShowSecondarySceneView && SecondarySceneViewId != 0)
		{
			if (ImGui::Begin("Scene 2", &bShowSecondarySceneView))
			{
				const bool bHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
				const bool bFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

				Renderer->SetSceneViewHovered(SecondarySceneViewId, bHovered);
				Renderer->SetSceneViewFocused(SecondarySceneViewId, bFocused);

				const ImVec2 avail = ImGui::GetContentRegionAvail();
				Renderer->SetSceneViewSize(
					SecondarySceneViewId,
					static_cast<uint32_t>(avail.x),
					static_cast<uint32_t>(avail.y)
				);

				if (!Renderer->WasSceneViewRecreatedThisFrame(SecondarySceneViewId))
				{
					ImGui::Image(Renderer->GetSceneViewTextureId(SecondarySceneViewId), avail);
				}
				else
				{
					ImGui::Dummy(avail);
				}
			}
			ImGui::End();
		}
	}

	void EditorLayer::SpawnTestScene()
	{
		auto& world = ActiveScene.GetRegistry();

		// @todo: Update Asset calls once demo assets are stripped from the Renderer implementation
		{
			Nyx::Engine::Entity e = ActiveScene.CreateEntity("Textured Cube");

			world.Add<Nyx::Engine::TransformComponent>(
				e,
				Nyx::Engine::TransformComponent{
					.Position = glm::vec3(-2.0f, 0.0f, 0.0f),
					.RotationRadians = glm::vec3(0.0f),
					.Scale = glm::vec3(1.0f)
				}
			);

			world.Add<Nyx::Engine::MeshRendererComponent>(
				e,
				Nyx::Engine::MeshRendererComponent{
					.MeshAsset = Renderer->GetCubeMesh(),
					.MaterialAsset = Renderer->GetTexturedMaterial(),
					.bVisible = true
				}
			);
		}

		{
			Nyx::Engine::Entity e = ActiveScene.CreateEntity("Reflective Cube");

			world.Add<Nyx::Engine::TransformComponent>(
				e,
				Nyx::Engine::TransformComponent{
					.Position = glm::vec3(0.0f, 0.0f, 0.0f),
					.RotationRadians = glm::vec3(0.0f),
					.Scale = glm::vec3(1.0f)
				}
			);

			world.Add<Nyx::Engine::MeshRendererComponent>(
				e,
				Nyx::Engine::MeshRendererComponent{
					.MeshAsset = Renderer->GetCubeMesh(),
					.MaterialAsset = Renderer->GetReflectiveMaterial(),
					.bVisible = true
				}
			);
		}

		{
			Nyx::Engine::Entity e = ActiveScene.CreateEntity("Untextured Cube");

			world.Add<Nyx::Engine::TransformComponent>(
				e,
				Nyx::Engine::TransformComponent{
					.Position = glm::vec3(2.0f, 0.0f, 0.0f),
					.RotationRadians = glm::vec3(0.0f),
					.Scale = glm::vec3(1.0f)
				}
			);

			world.Add<Nyx::Engine::MeshRendererComponent>(
				e,
				Nyx::Engine::MeshRendererComponent{
					.MeshAsset = Renderer->GetCubeMesh(),
					.MaterialAsset = Renderer->GetUntexturedMaterial(),
					.bVisible = true
				}
			);
		}
	}
}