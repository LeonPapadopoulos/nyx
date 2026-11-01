#include "TransformParents.h"

#include "CharacterComponent.h"
#include "GuidComponent.h"
#include "ParentComponent.h"
#include "PhysicsComponents.h"
#include "RootObjectSnapshotUtils.h"
#include "SceneDocument.h"
#include "TransactionDiffUtil.h"
#include "TransactionObjectRefHelpers.h"
#include "TransactionSystem.h"
#include "TransformComponent.h"

#include <cstddef>
#include <utility>

namespace Nyx::Editor
{
	namespace
	{
		// Walks up from the entity, calling `visit(ancestor)` for each one above it until it returns
		// true or there is none left. A chain that goes round in a circle ends after as many steps as
		// the world has children.
		template <typename TVisit>
		bool AnyAbove(const Nyx::Registry& world, Nyx::Entity entity, TVisit visit)
		{
			size_t children = 0;
			world.Each<Nyx::ParentComponent>(
				[&](Nyx::Entity, const Nyx::ParentComponent&)
				{
					++children;
				});

			std::optional<Nyx::Entity> above = FindParent(world, entity);
			for (size_t step = 0; above && step <= children; ++step)
			{
				if (visit(*above))
				{
					return true;
				}
				above = FindParent(world, *above);
			}
			return false;
		}
	}

	std::optional<Nyx::Entity> FindParent(const Nyx::Registry& world, Nyx::Entity entity)
	{
		if (!world.IsAlive(entity) || !world.Has<Nyx::ParentComponent>(entity))
		{
			return std::nullopt;
		}
		const Nyx::EntityGuid parent = world.Get<Nyx::ParentComponent>(entity).Parent;
		return parent.IsValid() ? Nyx::FindEntityByGuid(world, parent) : std::nullopt;
	}

	bool IsSelfOrBelow(const Nyx::Registry& world, Nyx::Entity entity, Nyx::Entity ancestor)
	{
		return entity == ancestor || AnyAbove(world, entity, [&](Nyx::Entity above) { return above == ancestor; });
	}

	bool SetParent(SceneDocument& scene, TransactionSystem& transactions, Nyx::Entity entity, std::optional<Nyx::Entity> parent)
	{
		Nyx::Registry& world = scene.GetRegistry();
		const ObjectRef target = MakeSceneEntityRef(scene, entity);
		if (!target.IsValid() || !world.Has<Nyx::TransformComponent>(entity))
		{
			return false;
		}

		const Nyx::Reflection::TypeMetadata& linkType = Nyx::Reflection::GetTypeMetadata<Nyx::ParentComponent>();

		// None: the link goes, and the entity stays where it is, its transform being its place already
		if (!parent)
		{
			if (!world.Has<Nyx::ParentComponent>(entity))
			{
				return false;
			}
			SubobjectSnapshot beforeRemove = CaptureSubobjectSnapshot(&world.Get<Nyx::ParentComponent>(entity), linkType);
			world.Remove<Nyx::ParentComponent>(entity);

			Transaction transaction;
			transaction.Label = "Clear Parent";
			transaction.Changes.push_back(
				Change{ EChangeKind::RemoveSubobject, RemoveSubobjectChange{ .Target = target, .BeforeRemove = std::move(beforeRemove) } });
			transactions.Push(std::move(transaction));
			return true;
		}

		if (!world.IsAlive(*parent) || !world.Has<Nyx::TransformComponent>(*parent) || !world.Has<Nyx::GuidComponent>(*parent) ||
			IsSelfOrBelow(world, *parent, entity) || FindParent(world, entity) == parent)
		{
			return false;
		}

		Nyx::ParentComponent link;
		link.Parent = world.Get<Nyx::GuidComponent>(*parent).Guid;
		Nyx::SetLocalTransform(link, world.Get<Nyx::TransformComponent>(*parent), world.Get<Nyx::TransformComponent>(entity));

		// Another parent: one value edit of the link
		if (world.Has<Nyx::ParentComponent>(entity))
		{
			Nyx::ParentComponent& existing = world.Get<Nyx::ParentComponent>(entity);
			TransactionDiffUtil diff;
			diff.TakeSnapshot(target, &existing, linkType);
			existing = link;
			return diff.CommitChanges("Set Parent", transactions);
		}

		// The first parent: the link is added, with its values, as one step
		Nyx::ParentComponent& added = world.Add<Nyx::ParentComponent>(entity, link);
		Transaction transaction;
		transaction.Label = "Set Parent";
		transaction.Changes.push_back(
			Change{ EChangeKind::AddSubobject, AddSubobjectChange{ .Target = target, .AfterAdd = CaptureSubobjectSnapshot(&added, linkType) } });
		transactions.Push(std::move(transaction));
		return true;
	}

	void FollowWorldTransform(Nyx::Registry& world, Nyx::Entity entity)
	{
		const std::optional<Nyx::Entity> parent = FindParent(world, entity);
		if (!parent || !world.Has<Nyx::TransformComponent>(*parent) || !world.Has<Nyx::TransformComponent>(entity))
		{
			return;
		}
		Nyx::SetLocalTransform(world.Get<Nyx::ParentComponent>(entity), world.Get<Nyx::TransformComponent>(*parent),
			world.Get<Nyx::TransformComponent>(entity));
	}

	bool HasParentMovingInGame(const Nyx::Registry& world, Nyx::Entity entity)
	{
		return AnyAbove(world, entity,
			[&](Nyx::Entity above)
			{
				return world.Has<Nyx::RigidBodyComponent>(above) || world.Has<Nyx::CharacterComponent>(above);
			});
	}
}
