#include "NyxPCH.h"
#include "OrbitCameras.h"

#include "GuidComponent.h"
#include "OrbitCameraComponent.h"
#include "PreviousTransformComponent.h"
#include "TransformComponent.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace Nyx
{
	void UpdateOrbitCameras(Registry& world, float deltaTime, std::optional<float> interpolation)
	{
		world.Each<OrbitCameraComponent>(
			[&](Entity entity, OrbitCameraComponent& orbit)
			{
				if (!world.Has<TransformComponent>(entity))
				{
					return;
				}
				TransformComponent& transform = world.Get<TransformComponent>(entity);
				const float minDistance = std::max(orbit.MinDistance, 0.0f);
				const float maxDistance = std::max(orbit.MaxDistance, minDistance);
				const float minPitch = glm::radians(std::clamp(orbit.MinPitchDegrees, -89.0f, 89.0f));
				const float maxPitch = std::max(glm::radians(std::clamp(orbit.MaxPitchDegrees, -89.0f, 89.0f)), minPitch);

				if (!orbit.bStarted)
				{
					// From where the camera looks, as placed in the scene
					const glm::vec3 looksAlong = transform.Rotation * glm::vec3(0.0f, 0.0f, -1.0f);
					orbit.Yaw = std::atan2(-looksAlong.x, -looksAlong.z);
					orbit.Pitch = std::asin(std::clamp(looksAlong.y, -1.0f, 1.0f));
				}
				if (!orbit.bStarted || orbit.Distance != orbit.AppliedDistance)
				{
					orbit.CurrentDistance = orbit.Distance;
					orbit.AppliedDistance = orbit.Distance;
				}

				// Yaw stays in [-pi, pi], however far it turns
				const glm::vec2 look = orbit.LookDelta * orbit.LookSensitivity;
				orbit.Yaw = std::remainder(orbit.Yaw - look.x, glm::two_pi<float>());
				orbit.Pitch = std::clamp(orbit.Pitch + look.y, minPitch, maxPitch);
				orbit.CurrentDistance = std::clamp(orbit.CurrentDistance - orbit.ZoomDelta * orbit.ZoomStep, minDistance, maxDistance);
				orbit.LookDelta = glm::vec2(0.0f);
				orbit.ZoomDelta = 0.0f;

				const std::optional<Entity> target = orbit.Target.IsValid() ? FindEntityByGuid(world, orbit.Target) : std::nullopt;
				if (!target)
				{
					return;
				}

				// Where the target is drawn this frame, so the two move as one
				const glm::vec3 goal = GetDrawnTransform(world, *target, interpolation).Position + orbit.TargetOffset;
				if (!orbit.bStarted || orbit.FollowSmoothing <= 0.0f)
				{
					orbit.FollowPoint = goal;
				}
				else
				{
					// The same smoothing at any frame rate
					orbit.FollowPoint = glm::mix(orbit.FollowPoint, goal, 1.0f - std::exp(-std::max(deltaTime, 0.0f) / orbit.FollowSmoothing));
				}
				orbit.bStarted = true;

				transform.Rotation = glm::normalize(
					glm::angleAxis(orbit.Yaw, glm::vec3(0.0f, 1.0f, 0.0f)) * glm::angleAxis(orbit.Pitch, glm::vec3(1.0f, 0.0f, 0.0f)));
				transform.Position = orbit.FollowPoint - transform.Rotation * glm::vec3(0.0f, 0.0f, -1.0f) * orbit.CurrentDistance;
			});
	}
}
