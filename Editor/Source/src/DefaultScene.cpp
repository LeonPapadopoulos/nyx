#include "DefaultScene.h"

#include "CameraComponent.h"
#include "DirectionalLightComponent.h"
#include "MeshRendererComponent.h"
#include "SceneDocument.h"
#include "TransformComponent.h"

#include <glm/glm.hpp>

namespace Nyx::Editor
{
	void SpawnDefaultScene(Nyx::SceneDocument& scene)
	{
		auto& world = scene.GetRegistry();

		{
			Nyx::Engine::Entity e = scene.CreateEntity("Textured Cube");

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
			Nyx::Engine::Entity e = scene.CreateEntity("Reflective Cube");

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
			Nyx::Engine::Entity e = scene.CreateEntity("Untextured Cube");

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
			Nyx::Engine::Entity e = scene.CreateEntity("Main Camera");

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
			Nyx::Engine::Entity e = scene.CreateEntity("Sun");

			world.Add<Nyx::Engine::TransformComponent>(
				e,
				Nyx::Engine::TransformComponent{
					.Position = glm::vec3(0.0f, 5.0f, 0.0f),
					.Rotation = glm::quat(glm::radians(glm::vec3(-65.0f, 65.0f, 0.0f))),
					.Scale = glm::vec3(1.0f) });

			world.Add<Nyx::Engine::DirectionalLightComponent>(e, Nyx::Engine::DirectionalLightComponent{});
		}
	}
}
