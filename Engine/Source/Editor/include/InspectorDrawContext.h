#pragma once

#include "InspectorTargetId.h"
#include "ReflectedDiffUtil.h"
#include "ReflectedTransactionSystem.h"

#include <glm/glm.hpp>
#include <optional>

namespace Nyx::Editor
{
	struct PropertyEditTransactionState
	{
		bool bEditing = false;
		InspectorTargetId TargetId{};
		std::optional<ReflectedDiffUtil> PendingDiff;
	};

	struct TransformRotationEditState : PropertyEditTransactionState
	{
		glm::vec3 CachedDegrees{ 0.0f };
	};

	struct InspectorDrawContext
	{
		ReflectedTransactionHistory* History = nullptr;
		InspectorTargetId CurrentTargetId{};

		PropertyEditTransactionState TransformPositionEdit;
		TransformRotationEditState TransformRotationEdit;
		PropertyEditTransactionState TransformScaleEdit;
	};
}