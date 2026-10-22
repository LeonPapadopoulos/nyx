#pragma once

#include "TransformComponent.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Nyx
{
	// Where an entity moved only in fixed steps was before the last step, and where that step left it,
	// so the renderer can draw it between the two (IRenderer::SetWorldInterpolation()). Runtime only:
	// not reflected, so never saved, edited or sent. PhysicsBodies keeps it for dynamic bodies that
	// moved in the last step; still entities have none.
	struct PreviousTransformComponent
	{
		// Before the last step
		glm::vec3 Position{ 0.0f };
		glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		// Where the last step left it. A transform that differs was set from outside since, a
		// teleport, and is drawn where it is.
		glm::vec3 StepPosition{ 0.0f };
		glm::quat StepRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
	};

	// Marks the entity as moved by a step from `before` to where its transform is now
	void SetPreviousTransform(PreviousTransformComponent& previous, const glm::vec3& beforePosition,
		const glm::quat& beforeRotation, const TransformComponent& now);

	// The transform to draw `factor` (0 to 1) of the way from the previous pose to the current one:
	// positions mixed, rotations slerped the short way round, the current scale. A transform set from
	// outside since the step is drawn as it is, not smeared from where the entity was.
	TransformComponent InterpolateTransform(const PreviousTransformComponent& previous, const TransformComponent& current, float factor);
}
