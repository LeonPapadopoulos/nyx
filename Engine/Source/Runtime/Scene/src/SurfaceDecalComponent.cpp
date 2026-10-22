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
}
