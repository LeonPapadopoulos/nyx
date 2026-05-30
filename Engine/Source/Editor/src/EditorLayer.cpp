#include "EditorLayer.h"

#include "Assertions.h"
#include "Renderer.h"
#include "MeshRendererComponent.h"
#include "TransformComponent.h"
#include "NameComponent.h"
#include "ComponentInspectorRegistry.h"

#include "imgui.h"

#include <cstring>
#include <string>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

namespace
{
	bool InputTextString(const char* label, std::string& value)
	{
		char buffer[256]{};
		const size_t copyLength = std::min(value.size(), sizeof(buffer) - 1);
		std::memcpy(buffer, value.data(), copyLength);
		buffer[copyLength] = '\0';

		if (ImGui::InputText(label, buffer, sizeof(buffer)))
		{
			value = buffer;
			return true;
		}

		return false;
	}
}

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

	void EditorLayer::Tick(float deltaTime)
	{
		TickScene(deltaTime);

		// Keep renderer-facing selection state up to date before rendering
		Renderer->SetSelectedEntity(ActiveScene.GetSelection());
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

	void EditorLayer::DrawPanels()
	{
		TransformGizmoInstance.TickHotkeys();

		ApplyPendingPickResults();

		DrawSceneOutliner();
		DrawDetailsPanel();
		DrawSceneViews();

		ImGui::ShowDemoWindow();
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
			extent.Width > 0 ? extent.Width - 1 : 0u
		);

		outPickY = std::min(
			static_cast<uint32_t>(normalizedY * static_cast<float>(extent.Height)),
			extent.Height > 0 ? extent.Height - 1 : 0u
		);
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

	void EditorLayer::DrawDetailsPanel()
	{
		if (!bShowDetailsPanel)
		{
			return;
		}

		if (!ImGui::Begin("Details", &bShowDetailsPanel))
		{
			ImGui::End();
			return;
		}

		auto& selection = ActiveScene.GetSelection();
		auto& world = ActiveScene.GetRegistry();

		if (!selection.has_value())
		{
			ImGui::TextUnformatted("No entity selected.");
			ImGui::End();
			return;
		}

		const Nyx::Engine::Entity entity = selection.value();

		if (!world.IsAlive(entity))
		{
			ImGui::TextUnformatted("Selected entity is no longer valid.");
			selection.reset();
			ImGui::End();
			return;
		}

		ImGui::Text("Entity: %u", entity.Index());
		ImGui::Separator();

		// @todo: Move away from manually hardcoding the visuals of 
		// specific components (and fields) here; Consider DetailsPanel-, 
		// and Property-Customizations like Unreal does it. Also, look
		// into code generation for the needed field meta data

		// Scope all the displayed details to that particular entity via its ID / TypedHandle
		ImGui::PushID(static_cast<int>(entity.Value));

		// @todo: Find a more robust (and automated) naming approach,
		// so we can easily, and reliably avoid Naming collisions among UI elements.
		// (Currently being dodged by using '##SomeSubInfo')

		for (const ComponentInspectorEntry& inspector : GetDefaultComponentInspectors())
		{
			if (inspector.HasComponent(world, entity))
			{
				inspector.DrawComponent(world, entity);
			}
		}

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

		if (!ImGui::Begin(title, &bOpen))
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
			static_cast<uint32_t>(avail.y)
		);

		if (!Renderer->WasSceneViewRecreatedThisFrame(sceneViewId))
		{
			ImGui::Image(Renderer->GetSceneViewTextureId(sceneViewId), avail);

			const ImVec2 imageMin = ImGui::GetItemRectMin();
			const ImVec2 imageSize = ImGui::GetItemRectSize();
			const bool bImageHovered = ImGui::IsItemHovered();

			const bool bGizmoConsumedInteraction =
				TransformGizmoInstance.TickAndDraw(
					*Renderer,
					ActiveScene,
					sceneViewId,
					imageMin,
					imageSize,
					bImageHovered
				);

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
						pickY
					);

					Renderer->RequestPick(sceneViewId, pickX, pickY);
				}
			}
		}
		else
		{
			ImGui::Dummy(avail);
		}

		ImGui::End();
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

			selection = result.HitEntity;
		}
	}
}