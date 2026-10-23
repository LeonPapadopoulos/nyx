#include "NyxPCH.h"
#include "SurfaceDecalComponent.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Nyx
{
	glm::mat4 MakeSurfaceDecalMatrix(const SurfaceFrame& frame, const glm::vec2& size)
	{
		const glm::mat4 lifted = glm::translate(frame.ToMatrix(), glm::vec3(0.0f, SurfaceDecalLift, 0.0f));
		return glm::scale(lifted, glm::vec3(size.x, 1.0f, size.y));
	}

	std::array<glm::vec3, 4> GetSurfaceDecalCorners(const SurfaceFrame& frame, const glm::vec2& size)
	{
		// The plane mesh's corners (MakePlane()): u along x, v along z
		const glm::mat4 matrix = MakeSurfaceDecalMatrix(frame, size);
		std::array<glm::vec3, 4> corners;
		const glm::vec2 uvs[4] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };
		for (size_t corner = 0; corner < corners.size(); ++corner)
		{
			corners[corner] = glm::vec3(matrix * glm::vec4(uvs[corner].x - 0.5f, 0.0f, uvs[corner].y - 0.5f, 1.0f));
		}
		return corners;
	}

	std::optional<SurfaceDecalHit> RaycastSurfaceDecals(
		const Registry& world, const EntityGuidLookup& lookup, const Ray& ray, float maxDistance)
	{
		std::optional<SurfaceDecalHit> nearest;
		world.Each<SurfaceDecalComponent>(
			[&](Entity entity, const SurfaceDecalComponent& decal)
			{
				const std::optional<SurfaceFrame> frame = decal.bVisible ? ResolveSurfaceAnchor(world, lookup, decal.Anchor) : std::nullopt;
				if (!frame || decal.Size.x <= 0.0f || decal.Size.y <= 0.0f)
				{
					return;
				}

				// Only from the front: towards the surface, against its normal
				const float towards = glm::dot(ray.Direction, frame->Normal);
				if (towards >= 0.0f)
				{
					return;
				}
				const glm::vec3 centre = frame->Position + frame->Normal * SurfaceDecalLift;
				const float distance = glm::dot(centre - ray.Origin, frame->Normal) / towards;
				if (distance < 0.0f || distance > maxDistance || (nearest && distance >= nearest->Distance))
				{
					return;
				}

				// As the plane mesh: x along the tangent, z along the bitangent, each from -0.5 to 0.5
				const glm::vec3 point = ray.Origin + ray.Direction * distance;
				const glm::vec2 uv(glm::dot(point - centre, frame->Tangent) / decal.Size.x + 0.5f,
					glm::dot(point - centre, frame->Bitangent) / decal.Size.y + 0.5f);
				if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f)
				{
					return;
				}
				nearest = SurfaceDecalHit{ entity, point, distance, uv };
			});
		return nearest;
	}
}
