#pragma once

#include "DiffUtil.h"
#include "InspectorTargetId.h"

#include <glm/glm.hpp>
#include <optional>

namespace Nyx::Editor
{
	struct PropertyEditTransactionState
	{
		bool bEditing = false;
		InspectorTargetId TargetId{};
		std::optional<DiffUtil> PendingDiff;
	};

	struct TransformRotationEditState : PropertyEditTransactionState
	{
		glm::vec3 CachedDegrees{ 0.0f };
	};

	struct InspectorDrawContext
	{
		TransactionHistory* History = nullptr;
		InspectorTargetId CurrentTargetId{};

		PropertyEditTransactionState TransformPositionEdit;
		TransformRotationEditState TransformRotationEdit;
		PropertyEditTransactionState TransformScaleEdit;
	};
}