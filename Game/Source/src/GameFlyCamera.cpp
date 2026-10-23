#include "GameFlyCamera.h"

#include "CameraComponent.h"
#include "TransformComponent.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace Nyx::Game
{
	using namespace Nyx::Input;

	GameFlyCamera::GameFlyCamera(InputSystem& input)
		: Fly(input, { .FlyContextName = "FlyCamera", .MouseLookContextName = "FlyCameraMouseLook" })
	{
	}

	void GameFlyCamera::Update(Nyx::Registry& world, float deltaTime)
	{
		const FlyCameraInput::Values input = Fly.Read();

		// The mouse looks around while the right button is held; the look context comes on from the
		// next frame, so the click's own mouse moves don't turn the camera
		bLooking = bLooking ? input.bLookButtonHeld : input.bLookButtonStarted;
		Fly.SetActive(true, bLooking, bLooking ? "Right mouse button held" : "The game flies its camera");

		if (input.SpeedSteps != 0.0f)
		{
			MoveSpeed = std::clamp(MoveSpeed * std::pow(1.2f, input.SpeedSteps), 0.25f, 100.0f);
		}

		if (input.Move == glm::vec3(0.0f) && input.Look == glm::vec2(0.0f))
		{
			return;
		}

		std::optional<Nyx::Entity> camera;
		world.Each<Nyx::CameraComponent>(
			[&](Nyx::Entity entity, Nyx::CameraComponent& component)
			{
				if (!camera && component.bPrimary && world.Has<Nyx::TransformComponent>(entity))
				{
					camera = entity;
				}
			});
		if (!camera)
		{
			return;
		}

		Nyx::TransformComponent& transform = world.Get<Nyx::TransformComponent>(*camera);

		// Yaw and pitch from where the camera looks (-Z), so whatever set the rotation last counts
		const glm::vec3 looksAlong = transform.Rotation * glm::vec3(0.0f, 0.0f, -1.0f);
		float yaw = std::atan2(-looksAlong.x, -looksAlong.z);
		float pitch = std::asin(std::clamp(looksAlong.y, -1.0f, 1.0f));

		yaw -= input.Look.x;
		pitch = std::clamp(pitch + input.Look.y, glm::radians(-89.0f), glm::radians(89.0f));
		transform.Rotation = glm::normalize(
			glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f)) * glm::angleAxis(pitch, glm::vec3(1.0f, 0.0f, 0.0f)));

		const glm::vec3 forward = transform.Rotation * glm::vec3(0.0f, 0.0f, -1.0f);
		const glm::vec3 right = transform.Rotation * glm::vec3(1.0f, 0.0f, 0.0f);
		const glm::vec3 up(0.0f, 1.0f, 0.0f);
		transform.Position += (right * input.Move.x + up * input.Move.y + forward * input.Move.z) * MoveSpeed * deltaTime;
	}
}
