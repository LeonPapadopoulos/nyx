#include "ComponentRegistration.h"

#include "CameraComponent.h"
#include "CharacterComponent.h"
#include "CloudLayerComponent.h"
#include "CloudModifierComponent.h"
#include "ColorGradingComponent.h"
#include "ComponentTypeRegistry.h"
#include "ConstraintComponent.h"
#include "DirectionalLightComponent.h"
#include "EntityGuidLookup.h"
#include "EnvironmentComponent.h"
#include "FlyCameraComponent.h"
#include "GrassFieldComponent.h"
#include "GroundMesh.h"
#include "GuidComponent.h"
#include "IAssetResolver.h"
#include "LightningComponent.h"
#include "MeshInstancesComponent.h"
#include "MeshRendererComponent.h"
#include "NameComponent.h"
#include "OrbitCameraComponent.h"
#include "ParentComponent.h"
#include "ParticleEmitterComponent.h"
#include "PhysicsComponents.h"
#include "PointLightComponent.h"
#include "Prefab.h"
#include "RuntimeMeshComponent.h"
#include "ScatterBlockerComponent.h"
#include "ScatterInstances.h"
#include "SdfTerrainComponent.h"
#include "SurfaceDecalComponent.h"
#include "TerrainChunks.h"
#include "TerrainOcclusion.h"
#include "TerrainNavigation.h"
#include "TerrainPlacement.h"
#include "TerrainTraversal.h"
#include "TerrainWater.h"
#include "TimeOfDayComponent.h"
#include "TransformComponent.h"
#include "VolumetricCloudsComponent.h"
#include "WindComponent.h"

#include "Generated/Runtime/Runtime.reflect.init.h"

#include <format>

namespace Nyx
{
	namespace
	{
		// A loaded MeshRenderer only knows the paths of its mesh and materials; look up the assets.
		void ResolveMeshRendererAssetsAfterLoad(MeshRendererComponent& component, ScenePostLoadContext& context)
		{
			ResolveMeshRendererAssets(component, context.AssetResolver);
		}

		// A ground's mesh is derived from its values: made, or made again, wherever it is loaded, added or
		// edited, in the editor and in a game, and never saved. The way generated content is made.
		void MakeGroundMeshAfterLoad(GroundComponent& component, ScenePostLoadContext& context)
		{
			if (context.World)
			{
				UpdateGroundMesh(component, *context.World, context.Entity);
			}
		}

		// A scatter's instances are derived the same way, from its values and its surface's mesh. A
		// surface loaded after it, or changed by its own step, is found by UpdateScatters() each frame.
		// Where the context has a job system for the background, it is left to UpdateScatters() given
		// that, which its caller runs each frame: then a scene's scatters are made once, from the whole
		// scene, not first from the part read so far, without the blockers read after them.
		void ScatterAfterLoad(ScatterComponent& component, ScenePostLoadContext& context)
		{
			if (context.World && !context.Background)
			{
				UpdateScatter(component, *context.World, context.Entity);
			}
		}

		// Follow Sun from scenes saved before the sky had three modes, into the mode
		void FoldLegacySkyModeAfterLoad(EnvironmentComponent& component, ScenePostLoadContext&)
		{
			FoldLegacySkyMode(component);
		}

		// The same for a decal's material
		void ResolveSurfaceDecalAssets(SurfaceDecalComponent& component, ScenePostLoadContext& context)
		{
			component.MaterialAsset = context.AssetResolver && component.Material.IsValid()
				? context.AssetResolver->ResolveMaterial(component.Material.Path)
				: nullptr;
		}

		// The world's wind as it runs: how fast and which way it blows now, and whether it is turning
		std::vector<InspectedValue> DescribeWindDrive(const WindDriveComponent& component)
		{
			const WindDrive& drive = component.Drive;
			const double end = drive.Start + drive.Seconds;
			return {
				{ "Turning From", std::format("{:.2f} m/s at {:.1f} degrees", drive.From.bBlowing ? drive.From.Speed : 0.0f, drive.From.DirectionDegrees) },
				{ "Turning To", std::format("{:.2f} m/s at {:.1f} degrees", drive.To.bBlowing ? drive.To.Speed : 0.0f, drive.To.DirectionDegrees) },
				{ "Over", std::format("{:.1f} s to {:.1f} s of the world's time", drive.Start, end) },
			};
		}

		// A link's values read, from a file, an edit or the editor while a game runs: what keeps track
		// of the links (TransformFollowers) looks at them again
		void MarkTransformLinksChangedAfterLoad(ParentComponent&, ScenePostLoadContext& context)
		{
			if (context.World)
			{
				context.World->MarkChanged<ParentComponent>();
			}
		}

		// After a scene is read: children placed by their parents, among them the parts of prefab
		// instances, made before their root's own transform was read
		void PlaceChildrenAfterLoad(Registry& world)
		{
			UpdateTransformHierarchy(world, MakeEntityGuidLookup(world));
		}

		// Temporal from older scenes into Update; and a preset chosen for the volumetric clouds sets
		// their values, once
		void ApplyVolumetricCloudsPresetAfterLoad(VolumetricCloudsComponent& component, ScenePostLoadContext&)
		{
			FoldLegacyVolumetricCloudsUpdate(component);
			ApplyVolumetricCloudsPreset(component);
		}

		// Where the Details panel shows the engine's components: a hundred per kind, with room left in
		// each for a game module's. The parent link is drawn inside Transform, so it has no place.
		void SetDetailsOrders()
		{
			ComponentTypeRegistry& types = ComponentTypeRegistry::Get();

			// What every entity is and where
			types.SetDetailsOrder<TransformComponent>(100);
			types.SetDetailsOrder<NameComponent>(110);
			types.SetDetailsOrder<GuidComponent>(120);
			types.SetDetailsOrder<PrefabInstanceComponent>(200);
			types.SetDetailsOrder<PrefabPartComponent>(210);

			// What it looks like
			types.SetDetailsOrder<MeshRendererComponent>(300);
			types.SetDetailsOrder<SurfaceDecalComponent>(310);
			types.SetDetailsOrder<ParticleEmitterComponent>(320);
			types.SetDetailsOrder<PointLightComponent>(330);
			types.SetDetailsOrder<DirectionalLightComponent>(340);

			types.SetDetailsOrder<CameraComponent>(400);
			types.SetDetailsOrder<FlyCameraComponent>(410);
			types.SetDetailsOrder<OrbitCameraComponent>(420);

			// How it moves and collides: the body, then its shape, then what joins it to others
			types.SetDetailsOrder<CharacterComponent>(500);
			types.SetDetailsOrder<RigidBodyComponent>(510);
			types.SetDetailsOrder<PhysicsMaterialComponent>(520);
			types.SetDetailsOrder<BoxColliderComponent>(530);
			types.SetDetailsOrder<SphereColliderComponent>(540);
			types.SetDetailsOrder<CapsuleColliderComponent>(550);
			types.SetDetailsOrder<MeshColliderComponent>(560);
			types.SetDetailsOrder<ConstraintComponent>(570);

			// The land and what grows on it
			types.SetDetailsOrder<SdfTerrainComponent>(600);
			types.SetDetailsOrder<TerrainPlaceCopiesComponent>(610);
			types.SetDetailsOrder<GroundComponent>(620);
			types.SetDetailsOrder<ScatterComponent>(630);
			types.SetDetailsOrder<ScatterBlockerComponent>(640);
			types.SetDetailsOrder<GrassFieldComponent>(650);

			// The world round it: sky, weather, and last the picture as a whole
			types.SetDetailsOrder<EnvironmentComponent>(700);
			types.SetDetailsOrder<TimeOfDayComponent>(710);
			types.SetDetailsOrder<WindComponent>(720);
			types.SetDetailsOrder<CloudLayerComponent>(730);
			types.SetDetailsOrder<CloudModifierComponent>(740);
			types.SetDetailsOrder<VolumetricCloudsComponent>(750);
			types.SetDetailsOrder<LightningComponent>(760);
			types.SetDetailsOrder<ColorGradingComponent>(770);
		}
	}

	void RegisterComponentTypes()
	{
		// Generated by NyxHeaderTool: registers every NYX_REFLECT(Component) type.
		Nyx::Reflection::Generated::RegisterRuntimeReflectedTypes();

		SetDetailsOrders();

		// Components that need more than their reflected properties after being loaded.
		ComponentTypeRegistry::Get().SetPostLoad<MeshRendererComponent>(&ResolveMeshRendererAssetsAfterLoad);
		ComponentTypeRegistry::Get().SetPostLoad<SurfaceDecalComponent>(&ResolveSurfaceDecalAssets);
		ComponentTypeRegistry::Get().SetPostLoad<EnvironmentComponent>(&FoldLegacySkyModeAfterLoad);
		ComponentTypeRegistry::Get().SetPostLoad<VolumetricCloudsComponent>(&ApplyVolumetricCloudsPresetAfterLoad);
		ComponentTypeRegistry::Get().SetPostLoad<GroundComponent>(&MakeGroundMeshAfterLoad);
		ComponentTypeRegistry::Get().SetOnRemove<GroundComponent>(&RemoveGroundMesh);
		ComponentTypeRegistry::Get().SetPostLoad<ScatterComponent>(&ScatterAfterLoad);
		ComponentTypeRegistry::Get().SetPostLoad<ParentComponent>(&MarkTransformLinksChangedAfterLoad);
		ComponentTypeRegistry::Get().SetOnRemove<ScatterComponent>(&RemoveScatter);
		// An SDF terrain's chunks are made each frame round the cameras (UpdateTerrainChunks()), and
		// go with it
		ComponentTypeRegistry::Get().SetOnRemove<SdfTerrainComponent>(&RemoveTerrainChunks);
		ComponentTypeRegistry::Get().SetOnRemove<TerrainPlaceCopiesComponent>(&RemoveTerrainPlaceCopies);

		ComponentTypeRegistry::Get().AddSceneLoadStep(&PlaceChildrenAfterLoad);

		// Made while the world runs and never saved; the editor shows what they are
		ComponentTypeRegistry::Get().RegisterRuntime<RuntimeMeshComponent>("Nyx::RuntimeMeshComponent", "Mesh Made at Run Time", &DescribeRuntimeMesh);
		ComponentTypeRegistry::Get().RegisterRuntime<MeshInstancesComponent>("Nyx::MeshInstancesComponent", "Mesh Instances", &DescribeMeshInstances);
		ComponentTypeRegistry::Get().RegisterRuntime<WindDriveComponent>("Nyx::WindDriveComponent", "Wind Drive", &DescribeWindDrive);
		ComponentTypeRegistry::Get().RegisterRuntime<TerrainChunksComponent>("Nyx::TerrainChunksComponent", "Terrain Chunks", &DescribeTerrainChunks);
		ComponentTypeRegistry::Get().RegisterRuntime<TerrainCollisionComponent>("Nyx::TerrainCollisionComponent", "Terrain Collision", &DescribeTerrainCollision);
		ComponentTypeRegistry::Get().RegisterRuntime<TerrainWaterComponent>("Nyx::TerrainWaterComponent", "Terrain Water", &DescribeTerrainWater);
		ComponentTypeRegistry::Get().RegisterRuntime<TerrainNavigationComponent>("Nyx::TerrainNavigationComponent", "Terrain Navigation", &DescribeTerrainNavigation);
		ComponentTypeRegistry::Get().RegisterRuntime<TerrainPlacesComponent>("Nyx::TerrainPlacesComponent", "Terrain Places", &DescribeTerrainPlaces);
		ComponentTypeRegistry::Get().RegisterRuntime<TerrainTraversalComponent>("Nyx::TerrainTraversalComponent", "Terrain Traversal", &DescribeTerrainTraversal);
		ComponentTypeRegistry::Get().RegisterRuntime<TerrainOcclusionComponent>("Nyx::TerrainOcclusionComponent", "Terrain Occlusion", &DescribeTerrainOcclusion);
		ComponentTypeRegistry::Get().RegisterRuntime<TerrainEditActivationComponent>("Nyx::TerrainEditActivationComponent", "Terrain Edit Activation",
			&DescribeTerrainEditActivation);

		// Scenes saved before the world had a wind of its own: the environment's, for particles, moved
		// into one
		ComponentTypeRegistry::Get().AddSceneLoadStep(&MoveLegacyEnvironmentWind);
	}
}
