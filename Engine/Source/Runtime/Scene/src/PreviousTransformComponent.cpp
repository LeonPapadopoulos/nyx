#include "NyxPCH.h"
#include "PreviousTransformComponent.h"

#include <algorithm>

namespace Nyx
{
	void SetPreviousTransform(PreviousTransformComponent& previous, const glm::vec3& beforePosition,
		const glm::quat& beforeRotation, const TransformComponent& now)
	{
		previous.Position = beforePosition;
		previous.Rotation = beforeRotation;
		previous.StepPosition = now.Position;
		previous.StepRotation = now.Rotation;
	}

	TransformComponent InterpolateTransform(const PreviousTransformComponent& previous, const TransformComponent& current, float factor)
	{
		// Compared exactly: only what the step wrote matches
		if (current.Position != previous.StepPosition || current.Rotation != previous.StepRotation)
		{
			return current;
		}

		const float t = std::clamp(factor, 0.0f, 1.0f);
		TransformComponent drawn = current;
		drawn.Position = glm::mix(previous.Position, current.Position, t);
		// glm's slerp turns the short way round, whichever sign the two quaternions have
		drawn.Rotation = glm::normalize(glm::slerp(previous.Rotation, current.Rotation, t));
		return drawn;
	}
}
