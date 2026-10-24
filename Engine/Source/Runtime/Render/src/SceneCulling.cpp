#include "NyxPCH.h"
#include "SceneCulling.h"

#include "MeshInstancesComponent.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Nyx
{
	namespace
	{
		// The matrix's row `index`, as clip space's coordinates are made from it
		glm::vec4 Row(const glm::mat4& matrix, int index)
		{
			return glm::vec4(matrix[0][index], matrix[1][index], matrix[2][index], matrix[3][index]);
		}

		glm::vec4 NormalizePlane(const glm::vec4& plane)
		{
			const float length = glm::length(glm::vec3(plane));
			return length > 0.0f ? plane / length : plane;
		}

		// The 3 x 3 part of the matrix with every element made positive: how far a box's half size
		// reaches along each axis once transformed
		glm::mat3 AbsoluteLinear(const glm::mat4& transform)
		{
			glm::mat3 result(transform);
			for (int column = 0; column < 3; ++column)
			{
				result[column] = glm::abs(result[column]);
			}
			return result;
		}
	}

	BoundingBox TransformBox(const BoundingBox& box, const glm::mat4& transform)
	{
		const glm::vec3 center = (box.Min + box.Max) * 0.5f;
		const glm::vec3 halfSize = (box.Max - box.Min) * 0.5f;
		const glm::vec3 movedCenter = glm::vec3(transform * glm::vec4(center, 1.0f));
		const glm::vec3 movedHalfSize = AbsoluteLinear(transform) * halfSize;
		return BoundingBox{ movedCenter - movedHalfSize, movedCenter + movedHalfSize };
	}

	BoundingBox ExpandBox(const BoundingBox& box, float margin)
	{
		return BoundingBox{ box.Min - glm::vec3(margin), box.Max + glm::vec3(margin) };
	}

	BoundingBox UniteBoxes(const BoundingBox& a, const BoundingBox& b)
	{
		return BoundingBox{ glm::min(a.Min, b.Min), glm::max(a.Max, b.Max) };
	}

	Frustum MakeFrustum(const glm::mat4& viewProjection)
	{
		const glm::vec4 x = Row(viewProjection, 0);
		const glm::vec4 y = Row(viewProjection, 1);
		const glm::vec4 z = Row(viewProjection, 2);
		const glm::vec4 w = Row(viewProjection, 3);

		Frustum frustum;
		frustum.Planes[0] = NormalizePlane(w + x);
		frustum.Planes[1] = NormalizePlane(w - x);
		frustum.Planes[2] = NormalizePlane(w + y);
		frustum.Planes[3] = NormalizePlane(w - y);
		// Depth from 0 to w: one of the two is the near plane, the other the far one
		frustum.Planes[4] = NormalizePlane(z);
		frustum.Planes[5] = NormalizePlane(w - z);
		return frustum;
	}

	EFrustumOverlap ClassifyBox(const Frustum& frustum, const BoundingBox& box)
	{
		bool bStraddling = false;
		for (const glm::vec4& plane : frustum.Planes)
		{
			const glm::vec3 normal(plane);
			// The box's corner furthest along the normal, and the one furthest against it
			const glm::vec3 furthest(normal.x >= 0.0f ? box.Max.x : box.Min.x, normal.y >= 0.0f ? box.Max.y : box.Min.y, normal.z >= 0.0f ? box.Max.z : box.Min.z);
			const glm::vec3 nearest(normal.x >= 0.0f ? box.Min.x : box.Max.x, normal.y >= 0.0f ? box.Min.y : box.Max.y, normal.z >= 0.0f ? box.Min.z : box.Max.z);
			if (glm::dot(normal, furthest) + plane.w < 0.0f)
			{
				return EFrustumOverlap::Outside;
			}
			if (glm::dot(normal, nearest) + plane.w < 0.0f)
			{
				bStraddling = true;
			}
		}
		return bStraddling ? EFrustumOverlap::Straddling : EFrustumOverlap::Inside;
	}

	std::vector<InstanceCullCell> MakeInstanceCullCells(const MeshInstances& instances, const BoundingBox& meshBounds, std::string* outProblem)
	{
		const uint32_t transformCount = static_cast<uint32_t>(instances.Transforms.size());
		std::vector<MeshInstanceCell> given = instances.Cells;
		if (given.empty() && transformCount > 0)
		{
			// One over all of them, with no bounds of its own: the instances' make them
			const glm::vec3 origin(instances.Transforms.front()[3]);
			given.push_back(MeshInstanceCell{ .BoundsMin = origin, .BoundsMax = origin, .First = 0, .Count = transformCount });
		}

		std::string problem;
		std::vector<InstanceCullCell> cells;
		cells.reserve(given.size());
		const glm::vec3 meshCenter = (meshBounds.Min + meshBounds.Max) * 0.5f;
		const glm::vec3 meshHalfSize = (meshBounds.Max - meshBounds.Min) * 0.5f;
		for (size_t index = 0; index < given.size(); ++index)
		{
			const MeshInstanceCell& cell = given[index];
			const uint32_t first = std::min(cell.First, transformCount);
			const uint32_t count = std::min(cell.Count, transformCount - first);
			if (count != cell.Count && problem.empty())
			{
				problem = "cell " + std::to_string(index) + " covers transforms " + std::to_string(cell.First) + " to " +
					std::to_string(static_cast<uint64_t>(cell.First) + cell.Count) + ", of " + std::to_string(transformCount);
			}
			if (count == 0)
			{
				continue;
			}

			BoundingBox bounds{ glm::min(cell.BoundsMin, cell.BoundsMax), glm::max(cell.BoundsMin, cell.BoundsMax) };
			for (uint32_t instance = first; instance < first + count; ++instance)
			{
				// TransformBox() of the mesh's bounds, without the call
				const glm::mat4& transform = instances.Transforms[instance];
				const glm::vec3 center = glm::vec3(transform * glm::vec4(meshCenter, 1.0f));
				const glm::vec3 halfSize = AbsoluteLinear(transform) * meshHalfSize;
				bounds.Min = glm::min(bounds.Min, center - halfSize);
				bounds.Max = glm::max(bounds.Max, center + halfSize);
			}
			cells.push_back(InstanceCullCell{ .Bounds = bounds, .First = first, .Count = count });
		}

		if (outProblem)
		{
			*outProblem = std::move(problem);
		}
		return cells;
	}

	uint32_t AppendInstanceRuns(std::span<const InstanceCullCell> cells, const Frustum* frustum, std::vector<InstanceRun>& outRuns)
	{
		uint32_t drawn = 0;
		const size_t firstRun = outRuns.size();
		for (const InstanceCullCell& cell : cells)
		{
			if (frustum && !IsBoxVisible(*frustum, cell.Bounds))
			{
				continue;
			}
			++drawn;
			// Only runs appended here are joined
			if (outRuns.size() > firstRun && outRuns.back().First + outRuns.back().Count == cell.First)
			{
				outRuns.back().Count += cell.Count;
			}
			else
			{
				outRuns.push_back(InstanceRun{ .First = cell.First, .Count = cell.Count });
			}
		}
		return drawn;
	}
}
