#pragma once

#include "Entity.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace Nyx
{
	struct Material;

	// A surface decal (SurfaceDecalComponent) as a frame draws it: the plane mesh placed on its
	// anchor's frame, drawn with its material's decal pipeline, its values in the push constants'
	// custom data (DecalInput in nyx/surface.slang)
	struct RenderDecal
	{
		Nyx::Entity SourceEntity{};
		Material* MaterialAsset = nullptr;
		// From the plane mesh to the quad in the world (MakeSurfaceDecalMatrix()), where the scene
		// pass draws it: on its host where the host is drawn
		glm::mat4 Model{ 1.0f };
		// The same on its host where the host is, for picking and the editor's overlays, as a
		// RenderObject's WorldTransform; and the quad's corners there (GetSurfaceDecalCorners())
		glm::mat4 WorldModel{ 1.0f };
		std::array<glm::vec3, 4> WorldCorners{};
		// The colour, and the intensity it is multiplied by
		glm::vec4 CustomData{ 0.0f };
		// The reveal and the pulse, 0 to 1
		glm::vec2 CustomData2{ 0.0f };

		// Its entity's picking id in its world (RenderWorld::PickingIdToEntity); 0 until given one
		uint32_t PickingId = 0;
	};

	// The world's decals to draw, in the order the world holds them: those visible, with a material,
	// revealed above 0, and whose host is in the world. Their anchors are resolved through one guid
	// lookup, made once per call, on their hosts as the meshes are drawn (GetDrawnTransform()): with
	// `interpolation`, between the host's last two steps, so a decal stays on a moving body's mesh;
	// picking and the editor's overlays go by where the host is. No picking ids are given.
	void ExtractSurfaceDecals(const Registry& world, std::optional<float> interpolation, std::vector<RenderDecal>& outDecals);
}
