#pragma once

#include "AssetReference.h"
#include "CameraProjection.h"
#include "EntityGuidLookup.h"
#include "ReflectionMacros.h"
#include "SurfaceAnchor.h"

#include <glm/glm.hpp>

#include <array>
#include <optional>

// What the renderer draws the decal with; the scene only holds it
namespace Nyx
{
	struct Material;
}

namespace Nyx
{
	// A material drawn flat on another entity's surface, moving with it, such as a glowing mark on a
	// crate: a quad on its anchor's frame, blended over what is drawn there. The anchor's host names
	// the entity; without one, the quad is in world space. The host's scale moves the anchor, but the
	// quad keeps its size in metres. A decal whose host is gone isn't drawn.
	NYX_REFLECT(Component, meta = (DisplayName = "Surface Decal"))
	struct SurfaceDecalComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Surface Decal", Tooltip = "Where the decal is: a point, normal and tangent on its host"))
		SurfaceAnchor Anchor;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Surface Decal", DragSpeed = 0.01, Tooltip = "In metres: the width along the anchor's tangent, the height along its bitangent"))
		glm::vec2 Size{ 0.5f, 0.5f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Surface Decal", AssetType = Material, Tooltip = "What the decal is drawn with, e.g. Materials/MaskedDecal.nyxmat"))
		Nyx::AssetReference Material;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Surface Decal", UI = Color))
		glm::vec3 Color{ 1.0f, 1.0f, 1.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Surface Decal", DragSpeed = 0.05, Tooltip = "What the colour is multiplied by: above 1, it glows"))
		float Intensity = 4.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Surface Decal", DragSpeed = 0.01, Tooltip = "0 to 1: how much of the decal is drawn, e.g. as a wipe across it"))
		float Reveal = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Surface Decal", DragSpeed = 0.01, Tooltip = "0 to 1: how strongly the decal throbs"))
		float Pulse = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Surface Decal", Tooltip = "Whether the decal is rendered"))
		bool bVisible = true;

		Nyx::Material* MaterialAsset = nullptr;
	};

	// How far a decal's quad is lifted off its surface along the normal, so that it doesn't fight the
	// surface for depth: 3 mm
	constexpr float SurfaceDecalLift = 0.003f;

	// From the unit square of the plane mesh (x and z from -0.5 to 0.5, facing +y) to the decal's quad
	// in the world: on the frame, lifted by SurfaceDecalLift, size.x along the tangent and size.y
	// along the bitangent
	glm::mat4 MakeSurfaceDecalMatrix(const SurfaceFrame& frame, const glm::vec2& size);

	// The quad's corners in the world, in the order of the plane mesh's UVs: (0, 0), (1, 0), (1, 1),
	// (0, 1)
	std::array<glm::vec3, 4> GetSurfaceDecalCorners(const SurfaceFrame& frame, const glm::vec2& size);

	// Where a ray met a decal's quad
	struct SurfaceDecalHit
	{
		// The entity with the SurfaceDecalComponent
		Entity Decal;
		glm::vec3 Point{ 0.0f };
		float Distance = 0.0f;
		// On the quad, as the plane mesh's UVs: (0, 0) to (1, 1)
		glm::vec2 UV{ 0.0f };
	};

	// The nearest decal whose quad the ray passes through from the front, the side its normal faces,
	// within `maxDistance`: e.g. what the cursor points at, in front of the surface it is on. Decals
	// not visible or whose host is gone are left out; what the material draws, its mask and the
	// Reveal don't count. The quads are where the hosts' transforms are, not where they are drawn
	// between steps.
	std::optional<SurfaceDecalHit> RaycastSurfaceDecals(
		const Registry& world, const EntityGuidLookup& lookup, const Ray& ray, float maxDistance);
}

#include "Generated/Runtime/SurfaceDecalComponent.reflect.h"
