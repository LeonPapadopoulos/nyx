#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Nyx
{
	struct MeshInstances;

	// What the renderer culls by: boxes against a camera's frustum. Plain functions, so the scene
	// views, and later the shadows' cascades, cull the same way, and tests need no GPU.

	// A box along the axes of the space it is in
	struct BoundingBox
	{
		glm::vec3 Min{ 0.0f };
		glm::vec3 Max{ 0.0f };
	};

	// The box around `box` taken into another space by `transform`
	BoundingBox TransformBox(const BoundingBox& box, const glm::mat4& transform);
	// The box around both
	BoundingBox UniteBoxes(const BoundingBox& a, const BoundingBox& b);

	// The box `margin` larger on every side, e.g. by how far a material's vertex function moves a
	// vertex at most
	BoundingBox ExpandBox(const BoundingBox& box, float margin);

	// How far `point` is from the nearest point of the box: 0 inside it
	float GetBoxDistance(const BoundingBox& box, const glm::vec3& point);

	// How far from a camera something is drawn (MeshRendererComponent::DrawDistance): a box farther
	// from `From` than `Distance` isn't
	struct DrawDistanceLimit
	{
		glm::vec3 From{ 0.0f };
		float Distance = 0.0f;
	};

	inline bool IsBoxInDrawDistance(const DrawDistanceLimit& limit, const BoundingBox& box)
	{
		return GetBoxDistance(box, limit.From) <= limit.Distance;
	}

	// What a camera sees: six planes facing inwards (left, right, bottom, top, and the two of the
	// depth range), each of unit normal, so a point p is on its inner side where
	// dot(plane.xyz, p) + plane.w >= 0. Made from the camera's view and projection; what is wholly
	// outside one of the planes can't be seen.
	struct Frustum
	{
		std::array<glm::vec4, 6> Planes{};
	};

	// From Vulkan's clip space as MakePerspectiveProjection() makes it: x and y from -w to w, depth
	// from 0 to w, whichever end is near, so the reversed depth too. A plane that a projection
	// leaves at infinity has no normal and holds nothing out.
	Frustum MakeFrustum(const glm::mat4& viewProjection);

	enum class EFrustumOverlap : uint8_t
	{
		Outside,
		// Partly inside; also a box near the frustum's edges that is outside it, but on the inner
		// side of every plane, as the planes alone can't tell it apart
		Straddling,
		Inside,
	};

	EFrustumOverlap ClassifyBox(const Frustum& frustum, const BoundingBox& box);

	inline bool IsBoxVisible(const Frustum& frustum, const BoundingBox& box)
	{
		return ClassifyBox(frustum, box) != EFrustumOverlap::Outside;
	}

	// A cell of instances as the renderer culls it: the box around the whole mesh at every one of
	// its instances, and the transforms it draws
	struct InstanceCullCell
	{
		BoundingBox Bounds;
		uint32_t First = 0;
		uint32_t Count = 0;
	};

	// The cells of `instances` (MeshInstancesComponent) to cull by, in the entity's space, for a
	// mesh with `meshBounds` in its own space: each cell's bounds of its instances' origins, with
	// the mesh added at each instance, turned and scaled as the instance is. The bounds a cell is
	// given are kept even when they are too small: so a cell is never culled while one of its
	// instances is in view. Without cells, one over all of the transforms. A cell that reaches past
	// the transforms is cut short and one without instances left out; `outProblem`, if given, says
	// so (empty when nothing was wrong).
	std::vector<InstanceCullCell> MakeInstanceCullCells(const MeshInstances& instances, const BoundingBox& meshBounds, std::string* outProblem = nullptr);

	// Transforms First to First + Count - 1, drawn in one draw
	struct InstanceRun
	{
		uint32_t First = 0;
		uint32_t Count = 0;
	};

	// The runs of instances to draw, appended to `outRuns`: those of every cell, or with a frustum
	// those of the cells not wholly outside it (`cells` in the frustum's space), and with a limit
	// those within its distance. A cell that follows on from the one before in the transforms joins
	// its run. Returns how many cells are drawn.
	uint32_t AppendInstanceRuns(std::span<const InstanceCullCell> cells, const Frustum* frustum, std::vector<InstanceRun>& outRuns,
		const DrawDistanceLimit* limit = nullptr);
}
