#pragma once

#include "InspectorTargetId.h"
#include "TransactionDiffUtil.h"
#include "TransactionSystem.h"

#include <glm/glm.hpp>
#include <optional>

namespace Nyx::Editor
{
	struct PropertyEditTransactionState
	{
		bool bEditing = false;
		InspectorTargetId TargetId{};
		std::optional<TransactionDiffUtil> PendingDiff;
	};

	struct TransformRotationEditState : PropertyEditTransactionState
	{
		glm::vec3 CachedDegrees{ 0.0f };
	};

	struct InspectorDrawContext
	{
		TransactionSystem* Transactions = nullptr;
		InspectorTargetId CurrentTargetId{};

		PropertyEditTransactionState TransformPositionEdit;
		TransformRotationEditState TransformRotationEdit;
		PropertyEditTransactionState TransformScaleEdit;
	};
}