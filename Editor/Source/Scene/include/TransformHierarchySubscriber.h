#pragma once

#include "Entity.h"
#include "EntityGuidLookup.h"
#include "TransactionDomain.h"

#include <functional>
#include <utility>
#include <vector>

namespace Nyx::Editor
{
	class SceneDocument;

	// After every edit, undo, redo and drag, places every child again relative to its parent
	// (ParentComponent, UpdateTransformHierarchy()). A child's world transform is derived, never
	// recorded: undo brings back what was edited, a parent's transform or a child's local values, and
	// this puts the children where those say. Subscribed after the game link, so a running game gets
	// the edit first and then where it moved the children (SetOnMoved()).
	class TransformHierarchySubscriber final : public ITransactionSubscriber
	{
	public:
		explicit TransformHierarchySubscriber(SceneDocument& scene);

		// Told the entities whose world transforms moved, parents before their children, whenever
		// some did
		void SetOnMoved(std::function<void(const std::vector<Nyx::Entity>&)> onMoved)
		{
			OnMoved = std::move(onMoved);
		}

		// Places every child; also after a scene is loaded or replaced
		void Update();

		void OnTransactionCommitted(const Transaction& transaction) override;
		void OnTransactionApplied(const Transaction& transaction, bool bWasUndo) override;
		void OnTransactionPreviewed(const Transaction& transaction) override;

	private:
		SceneDocument& Scene;
		std::function<void(const std::vector<Nyx::Entity>&)> OnMoved;

		// Kept between updates, so a drag allocates nothing once the scene stops growing
		Nyx::EntityGuidLookup Guids;
		std::vector<Nyx::Entity> Moved;
	};
}
