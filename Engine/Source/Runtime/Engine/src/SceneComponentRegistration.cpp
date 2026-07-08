#include "SceneComponentRegistration.h"

#include "IAssetResolver.h"
#include "MeshRendererComponent.h"
#include "NameComponent.h"
#include "SceneComponentTypeRegistry.h"
#include "TransformComponent.h"

namespace Nyx::Engine
{
	namespace
	{
		static void PostLoadResolveMeshRendererComponent(
			MeshRendererComponent& component,
			ScenePostLoadContext& context)
		{
			if (!context.AssetResolver)
			{
				component.MeshAsset = nullptr;
				component.MaterialAsset = nullptr;
				return;
			}

			component.MeshAsset =
				component.Mesh.IsValid() ? context.AssetResolver->ResolveMesh(component.Mesh.Path) : nullptr;

			component.MaterialAsset =
				component.Material.IsValid() ? context.AssetResolver->ResolveMaterial(component.Material.Path) : nullptr;
		}
	}

	void RegisterDefaultSceneComponentTypes()
	{
		static bool bRegistered = false;
		if (bRegistered)
		{
			return;
		}
		bRegistered = true;

		SceneComponentTypeRegistry::Get().Register(
			MakeSceneComponentTypeOps<NameComponent>());

		SceneComponentTypeRegistry::Get().Register(
			MakeSceneComponentTypeOps<TransformComponent>());

		SceneComponentTypeRegistry::Get().Register(
			MakeSceneComponentTypeOps<MeshRendererComponent>(
				"Nyx::Engine::MeshRendererComponent",
				&PostLoadResolveMeshRendererComponent));
	}
}