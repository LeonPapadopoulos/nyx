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
		// Discard snapshots without reading or restoring the objects they referred to.
		void CancelPendingEdits();

		TransactionSystem* Transactions = nullptr;

		InspectorTargetId CurrentTargetId{};
		ObjectRef CurrentObjectRef{};

		// Where the object being drawn is inside CurrentObjectRef: empty while drawing a component's
		// own properties, set while drawing a struct inside one. Edits record it, so undo finds the struct.
		SubobjectPath CurrentLocation;

		// Generic reflected property editing state
		PropertyEditTransactionState GenericPropertyEdit;

		// Special-case only for quaternion-as-degrees UI
		TransformRotationEditState TransformRotationEdit;
	};
}
