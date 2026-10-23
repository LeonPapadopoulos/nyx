#pragma once

#include "Entity.h"
#include "EntityGuid.h"
#include "TransactionDiffUtil.h"
#include "TransactionObjectRef.h"
#include "TransactionSystem.h"

#include <glm/glm.hpp>
#include <optional>
#include <string>

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

	// An entity reference's eyedropper, waiting for the next entity clicked in a scene view
	struct EntityPickState
	{
		// The reference that picks: the object, where in it, and the property
		ObjectRef Target{};
		SubobjectPath Location;
		const Nyx::Reflection::PropertyMetadata* Property = nullptr;

		// The entity clicked, set by the scene view; the reference takes it when it is next drawn
		std::optional<Nyx::EntityGuid> Picked;

		// Whether the reference drew this frame; one that didn't (its component closed, another
		// entity selected) stops picking
		bool bDrawn = false;

		bool IsWaiting() const
		{
			return Property != nullptr;
		}
	};

	struct InspectorDrawContext
	{
		// Discard snapshots without reading or restoring the objects they referred to.
		void CancelPendingEdits();

		// Previews the value being dragged or typed, if any, so a running game shows it before the
		// edit ends (TransactionDiffUtil::PreviewChanges). Once per frame, after drawing.
		void PreviewPendingEdits();

		TransactionSystem* Transactions = nullptr;
		// The assets an asset reference can name (its picker lists them); none without
		const class AssetDatabase* Assets = nullptr;

		// The entities an entity reference can name (its combo lists them); none without
		const Nyx::Registry* Entities = nullptr;

		// An entity reference's Select button: the entity to select once the panel is drawn
		std::optional<Nyx::Entity> EntityToSelect;

		// What is typed in the filter of the entity reference whose combo is open
		std::string EntityFilter;

		EntityPickState EntityPick;

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
