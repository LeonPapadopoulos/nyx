#pragma once

#include "TransactionDiffUtil.h"
#include "TransactionObjectRef.h"
#include "TransactionSystem.h"

#include <glm/glm.hpp>
#include <optional>

namespace Nyx::Editor
{
	struct PropertyEditTransactionState
	{
		bool bEditing = false;
		ObjectRef Target{};
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
		ObjectRef CurrentObjectRef{};

		// Generic reflected property editing state
		PropertyEditTransactionState GenericPropertyEdit;

		// Special-case only for quaternion-as-degrees UI
		TransformRotationEditState TransformRotationEdit;
	};
}