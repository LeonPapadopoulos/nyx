#include "NyxPCH.h"
#include "CameraProjection.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Nyx
{
	std::optional<WorldCamera> FindPrimaryCamera(const Registry& world)
	{
		std::optional<WorldCamera> found;
		world.Each<CameraComponent>(
			[&](Entity entity, const CameraComponent& camera)
			{
				if (found || !camera.bPrimary)
				{
					return;
				}
				const TransformComponent transform =
					world.Has<TransformComponent>(entity) ? world.Get<TransformComponent>(entity) : TransformComponent{};
				found = WorldCamera{ entity, transform, camera };
			});
		return found;
	}

	float GetAspectRatio(float width, float height)
	{
		return height > 0.0f ? width / height : 1.0f;
	}

	glm::mat4 MakePerspectiveProjection(float fovYRadians, float aspectRatio, float nearPlane, float farPlane)
	{
		// Depth within Vulkan's 0 to 1, reversed: the near and far planes swapped give 1 at the near
		// plane and 0 at the far one. Named, not left to GLM_FORCE_DEPTH_ZERO_TO_ONE: OpenGL's -1 to 1
		// cut off what was nearer than about twice the near plane.
		glm::mat4 projection = glm::perspectiveRH_ZO(fovYRadians, aspectRatio, farPlane, nearPlane);

		// Vulkan's Y points down the picture
		projection[1][1] *= -1.0f;
		return projection;
	}

	glm::mat4 GetCameraView(const TransformComponent& transform)
	{
		return glm::inverse(transform.ToMatrix());
	}

	glm::mat4 GetCameraProjection(const CameraComponent& camera, float aspectRatio)
	{
		return MakePerspectiveProjection(glm::radians(camera.FovYDegrees), aspectRatio, camera.NearPlane, camera.FarPlane);
	}

	Ray ScreenPointToRay(const TransformComponent& transform, const CameraComponent& camera,
		const glm::vec2& viewportSize, const glm::vec2& pixel)
	{
		const glm::vec2 ndc = (pixel / viewportSize) * 2.0f - 1.0f;

		// Back through the projection to view space, at the near and the far plane, then into the
		// world by the transform the view is the inverse of, rather than by inverting the view again
		const glm::mat4 inverseProjection =
			glm::inverse(GetCameraProjection(camera, GetAspectRatio(viewportSize.x, viewportSize.y)));
		const glm::mat4 cameraToWorld = transform.ToMatrix();
		const auto toWorld = [&](float depth)
		{
			const glm::vec4 view = inverseProjection * glm::vec4(ndc, depth, 1.0f);
			return glm::vec3(cameraToWorld * glm::vec4(glm::vec3(view) / view.w, 1.0f));
		};

		const glm::vec3 nearPoint = toWorld(NearPlaneDepth);
		const glm::vec3 farPoint = toWorld(FarPlaneDepth);
		return Ray{ nearPoint, glm::normalize(farPoint - nearPoint) };
	}

	std::optional<glm::vec2> WorldToScreenPoint(const TransformComponent& transform, const CameraComponent& camera,
		const glm::vec2& viewportSize, const glm::vec3& point)
	{
		const glm::mat4 viewProjection =
			GetCameraProjection(camera, GetAspectRatio(viewportSize.x, viewportSize.y)) * GetCameraView(transform);
		const glm::vec4 clip = viewProjection * glm::vec4(point, 1.0f);
		if (clip.w <= 0.0f)
		{
			return std::nullopt;
		}
		const glm::vec2 ndc = glm::vec2(clip) / clip.w;
		return (ndc + 1.0f) * 0.5f * viewportSize;
	}
}
