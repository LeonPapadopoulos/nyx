#include "TransformHierarchySubscriber.h"

#include "ParentComponent.h"
#include "SceneDocument.h"

namespace Nyx::Editor
{
	TransformHierarchySubscriber::TransformHierarchySubscriber(SceneDocument& scene)
		: Scene(scene)
	{
	}

	void TransformHierarchySubscriber::Update()
	{
		Nyx::Registry& world = Scene.GetRegistry();

		// Most scenes have no children: then not even the guid lookup is made
		bool bHasChildren = false;
		world.Each<Nyx::ParentComponent>(
			[&](Nyx::Entity, const Nyx::ParentComponent&)
			{
				bHasChildren = true;
			});
		if (!bHasChildren)
		{
			return;
		}

		Nyx::RefreshEntityGuidLookup(world, Guids);
		Nyx::UpdateTransformHierarchy(world, Guids, &Moved);

		if (!Moved.empty() && OnMoved)
		{
			OnMoved(Moved);
		}
	}

	void TransformHierarchySubscriber::OnTransactionCommitted(const Transaction& /*transaction*/)
	{
		Update();
	}

	void TransformHierarchySubscriber::OnTransactionApplied(const Transaction& /*transaction*/, bool /*bWasUndo*/)
	{
		Update();
	}

	void TransformHierarchySubscriber::OnTransactionPreviewed(const Transaction& /*transaction*/)
	{
		Update();
	}
}
