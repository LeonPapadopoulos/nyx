#pragma once

#include "ReflectionMacros.h"
#include "AssetReference.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

// What the renderer draws the component with; the scene only holds them
namespace Nyx
{
	class Mesh;
	struct Material;
	class IAssetResolver;
	class Registry;
}

namespace Nyx
{
	// A material slot after the first, for a mesh of more than one section (MeshSection)
	NYX_REFLECT()
	struct MeshMaterialSlot
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (AssetType = Material, Tooltip = "What the sections of this slot are drawn with; none: the first slot's material"))
		Nyx::AssetReference Material;
	};

	NYX_REFLECT(Component, meta = (DisplayName = "Mesh Renderer Component"))
	struct MeshRendererComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, ReadOnly, meta = (Category = "Rendering", AssetType = Mesh, Tooltip = "The built-in mesh drawn; meshes become assets later. A mesh made by code while the game runs takes its place"))
		Nyx::AssetReference Mesh;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Rendering", AssetType = Material, Tooltip = "What the mesh is drawn with: pick a material, or drag one from the asset browser. For a mesh of sections, the first slot's"))
		Nyx::AssetReference Material;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Rendering", Tooltip = "Whether the mesh is rendered"))
		bool bVisible = true;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Rendering", DisplayName = "Cast Shadows", Tooltip = "Whether the mesh casts shadows from the primary light; a hidden mesh casts none"))
		bool bCastShadows = true;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Rendering", DisplayName = "Draw Distance", DragSpeed = 1.0, Tooltip = "Farther from the camera than this, in metres, it isn't drawn, nor its shadow; drawn instanced, each cell of its instances by itself, so a scatter of small plants kilometres across costs only its near part. 0: drawn at any distance"))
		float DrawDistance = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Rendering", Tooltip = "The materials of slots 1, 2 and on, for a mesh whose sections ask for them, such as a tree's leaves beside its trunk"))
		std::vector<Nyx::MeshMaterialSlot> ExtraMaterials;

		// What the materials get as the instance's custom numbers (SurfaceInput.InstanceCustom) where
		// the entity is drawn once, as each instance of a MeshInstancesComponent has its own, so one
		// material works both ways. Cheap to change every frame, e.g. by a system: it goes to the GPU
		// with the entity's transform, never with its mesh.
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Rendering", DisplayName = "Custom Data", DragSpeed = 0.01, Tooltip = "Four numbers for the materials, as an instance's own: its sway's phase, its age, whatever they read. Not used when the entity is drawn instanced, whose instances have their own"))
		glm::vec4 Custom{ 0.0f };

		Nyx::Mesh* MeshAsset = nullptr;
		Nyx::Material* MaterialAsset = nullptr;
		// ExtraMaterials' materials, slot 1 first; null for a slot without one
		std::vector<Nyx::Material*> ExtraMaterialAssets;

		// The paths the assets above were looked up for (ResolveMeshRendererAssets()), so a renderer
		// whose paths a game's step set is looked up again
		std::string ResolvedMeshPath;
		std::string ResolvedMaterialPath;
		std::vector<std::string> ResolvedExtraMaterialPaths;
	};

	// Looks up the assets of its paths, and remembers them; without a resolver, none
	void ResolveMeshRendererAssets(MeshRendererComponent& component, IAssetResolver* resolver);

	// Looks up the assets of every renderer whose paths aren't the ones they were looked up for:
	// one a game's step added, or gave another mesh or material, e.g. a glowing bar it puts up or a
	// material it changes as something burns. Call once a frame, after the steps.
	void ResolveChangedMeshRenderers(Registry& world, IAssetResolver& resolver);

	// What the sections of `slot` are drawn with: slot 0's is MaterialAsset; a later slot's is its
	// own, or MaterialAsset for a slot without one
	Nyx::Material* GetSlotMaterial(const MeshRendererComponent& component, uint32_t slot);
}

#include "Generated/Runtime/MeshRendererComponent.reflect.h"
