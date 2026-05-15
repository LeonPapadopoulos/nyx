#include "NyxPCH.h"
#include "EditorLayer.h"

#include "SceneDocument.h"
//#include "NameComponent.h"
#include "TransformComponent.h"
#include "MeshRendererComponent.h"
#include "Entity.h"
#include "imgui.h"

namespace Nyx::Editor
{
	void EditorLayer::Initialize()
	{
		// @todo: Establish access to the Renderer
		//Renderer->Initialize("NyxEditor", Window);

		//MainSceneViewId = Renderer->CreateSceneView();
		//SecondarySceneViewId = Renderer->CreateSceneView();

		//// @todo:
		//Renderer->SetWorld(&ActiveScene.GetRegistry());

		SpawnTestScene();
	}

	void EditorLayer::Shutdown()
	{
		// @todo:
	}

	void EditorLayer::SpawnTestScene()
	{
		auto& world = ActiveScene.GetRegistry();

		//@todo: Establish access to the Renderer

		//{
		//	Nyx::Engine::Entity e = ActiveScene.CreateEntity("Textured Cube");
		//	world.Add<Nyx::Engine::TransformComponent>(e,
		//		Nyx::Engine::TransformComponent{
		//			.Position = glm::vec3(-2.0f, 0.0f, 0.0f),
		//			.RotationRadians = glm::vec3(0.0f),
		//			.Scale = glm::vec3(1.0f)
		//		}
		//	);

		//	world.Add<Nyx::Engine::MeshRendererComponent>(e,
		//		Nyx::Engine::MeshRendererComponent{
		//			.MeshAsset = Renderer->GetCubeMesh(),
		//			.MaterialAsset = Renderer->GetTexturedMaterial(),
		//			.bVisible = true
		//		}
		//	);
		//}

		//{
		//	Nyx::Engine::Entity e = ActiveScene.CreateEntity("Reflective Cube");
		//	world.Add<Nyx::Engine::TransformComponent>(e,
		//		Nyx::Engine::TransformComponent{
		//			.Position = glm::vec3(0.0f, 0.0f, 0.0f),
		//			.RotationRadians = glm::vec3(0.0f),
		//			.Scale = glm::vec3(1.0f)
		//		}
		//	);

		//	world.Add<Nyx::Engine::MeshRendererComponent>(e,
		//		Nyx::Engine::MeshRendererComponent{
		//			.MeshAsset = Renderer->GetCubeMesh(),
		//			.MaterialAsset = Renderer->GetReflectiveMaterial(),
		//			.bVisible = true
		//		}
		//	);
		//}

		//{
		//	Nyx::Engine::Entity e = ActiveScene.CreateEntity("Untextured Cube");
		//	world.Add<Nyx::Engine::TransformComponent>(e,
		//		Nyx::Engine::TransformComponent{
		//			.Position = glm::vec3(2.0f, 0.0f, 0.0f),
		//			.RotationRadians = glm::vec3(0.0f),
		//			.Scale = glm::vec3(1.0f)
		//		}
		//	);

		//	world.Add<Nyx::Engine::MeshRendererComponent>(e,
		//		Nyx::Engine::MeshRendererComponent{
		//			.MeshAsset = Renderer->GetCubeMesh(),
		//			.MaterialAsset = Renderer->GetUntexturedMaterial(),
		//			.bVisible = true
		//		}
		//	);
		//}
	}

	void EditorLayer::DrawUserInterface()
	{
		DrawSceneOutliner();

		// @todo: other panels
	}

	void EditorLayer::DrawSceneOutliner()
	{
		ImGui::Begin("Scene Outliner");

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

		// Optional: click empty space to clear selection
		if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered())
		{
			selection.reset();
		}

		ImGui::End();
	}
}
