#include "FlyCameras.h"

#include "Entity.h"
#include "FlyCameraComponent.h"
#include "TransformComponent.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace Nyx::Input
{
	void UpdateFlyCameras(Registry& world, FlyCameraInput& input, float deltaTime)
	{
		const FlyCameraInput::Values values = input.Read();

		bool bAnyCamera = false;
		bool bAnyLooking = false;
		world.Each<FlyCameraComponent>(
			[&](Entity entity, FlyCameraComponent& fly)
			{
				if (!world.Has<TransformComponent>(entity))
				{
					return;
				}
				bAnyCamera = true;

				// The mouse looks around while the right button is held; the look context comes on from
				// the next frame, so the click's own mouse moves don't turn the camera
				fly.bLooking = fly.bLooking ? values.bLookButtonHeld : values.bLookButtonStarted;
				bAnyLooking = bAnyLooking || fly.bLooking;

				if (values.SpeedSteps != 0.0f && fly.MoveSpeed > 0.0f)
				{
					fly.SpeedScale = std::clamp(fly.SpeedScale * std::pow(1.2f, values.SpeedSteps), 0.25f / fly.MoveSpeed, 100.0f / fly.MoveSpeed);
				}

				if (values.Move == glm::vec3(0.0f) && values.Look == glm::vec2(0.0f))
				{
					return;
				}

				TransformComponent& transform = world.Get<TransformComponent>(entity);

				// Yaw and pitch from where the camera looks (-Z), so whatever set the rotation last counts
				const glm::vec3 looksAlong = transform.Rotation * glm::vec3(0.0f, 0.0f, -1.0f);
				float yaw = std::atan2(-looksAlong.x, -looksAlong.z);
				float pitch = std::asin(std::clamp(looksAlong.y, -1.0f, 1.0f));

				const glm::vec2 look = values.Look * fly.LookSensitivity;
				yaw -= look.x;
				pitch = std::clamp(pitch + look.y, glm::radians(-89.0f), glm::radians(89.0f));
				transform.Rotation = glm::normalize(
					glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f)) * glm::angleAxis(pitch, glm::vec3(1.0f, 0.0f, 0.0f)));

				const glm::vec3 forward = transform.Rotation * glm::vec3(0.0f, 0.0f, -1.0f);
				const glm::vec3 right = transform.Rotation * glm::vec3(1.0f, 0.0f, 0.0f);
				const glm::vec3 up(0.0f, 1.0f, 0.0f);
				transform.Position +=
					(right * values.Move.x + up * values.Move.y + forward * values.Move.z) * fly.MoveSpeed * fly.SpeedScale * deltaTime;
			});

		input.SetActive(bAnyCamera, bAnyLooking,
			bAnyLooking      ? "Right mouse button held"
				: bAnyCamera ? "The world has a fly camera"
							 : "No fly camera in the world");
	}
}
