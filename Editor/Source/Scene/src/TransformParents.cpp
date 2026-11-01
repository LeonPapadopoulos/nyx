#include "TransformParents.h"

#include "CharacterComponent.h"
#include "GuidComponent.h"
#include "ParentComponent.h"
#include "PhysicsComponents.h"
#include "ReflectedPropertyAccess.h"
#include "RootObjectSnapshotUtils.h"
#include "SceneDocument.h"
#include "TransactionObjectRefHelpers.h"
#include "TransactionSystem.h"
#include "TransformComponent.h"

#include <algorithm>
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
		Transaction transaction;
		transaction.Label = parent ? "Set Parent" : "Clear Parent";
		if (!ChangeParent(scene, entity, parent, transaction.Changes))
		{
			return false;
		}
		transactions.Push(std::move(transaction));
		return true;
	}

	bool ChangeParent(SceneDocument& scene, Nyx::Entity entity, std::optional<Nyx::Entity> parent, std::vector<Change>& changes)
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
			changes.push_back(
				Change{ EChangeKind::RemoveSubobject, RemoveSubobjectChange{ .Target = target, .BeforeRemove = std::move(beforeRemove) } });
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

		// Another parent: a value edit of each of the link's values that changed
		if (world.Has<Nyx::ParentComponent>(entity))
		{
			Nyx::ParentComponent& existing = world.Get<Nyx::ParentComponent>(entity);
			const Nyx::ParentComponent before = existing;
			existing = link;
			for (size_t index = 0; index < linkType.PropertyCount; ++index)
			{
				const Nyx::Reflection::PropertyMetadata& property = linkType.Properties[index];
				Nyx::Reflection::PropertyValue beforeValue = ReadReflectedPropertyValue(&before, property);
				Nyx::Reflection::PropertyValue afterValue = ReadReflectedPropertyValue(&existing, property);
				if (!Nyx::Reflection::ArePropertyValuesEqual(beforeValue, afterValue))
				{
					changes.push_back(Change{ EChangeKind::SetValue,
						SetValueChange{ .Target = target, .TypeMetadata = &linkType, .PropertyIndex = index, .Before = std::move(beforeValue), .After = std::move(afterValue) } });
				}
			}
			return true;
		}

		// The first parent: the link is added, with its values
		Nyx::ParentComponent& added = world.Add<Nyx::ParentComponent>(entity, link);
		changes.push_back(
			Change{ EChangeKind::AddSubobject, AddSubobjectChange{ .Target = target, .AfterAdd = CaptureSubobjectSnapshot(&added, linkType) } });
		return true;
	}

	std::vector<Nyx::Entity> FindChildren(const Nyx::Registry& world, Nyx::Entity entity)
	{
		std::vector<Nyx::Entity> children;
		if (!world.Has<Nyx::GuidComponent>(entity))
		{
			return children;
		}
		const Nyx::EntityGuid guid = world.Get<Nyx::GuidComponent>(entity).Guid;
		world.Each<Nyx::ParentComponent>(
			[&](Nyx::Entity child, const Nyx::ParentComponent& link)
			{
				if (guid.IsValid() && link.Parent == guid && child != entity)
				{
					children.push_back(child);
				}
			});
		return children;
	}

	std::vector<Nyx::Entity> GetSelfAndBelow(const Nyx::Registry& world, Nyx::Entity entity)
	{
		std::vector<Nyx::Entity> entities;
		if (!world.IsAlive(entity))
		{
			return entities;
		}
		entities.push_back(entity);

		// Level by level; an entity met again (a circle) isn't taken twice
		for (size_t next = 0; next < entities.size(); ++next)
		{
			for (const Nyx::Entity child : FindChildren(world, entities[next]))
			{
				if (std::find(entities.begin(), entities.end(), child) == entities.end())
				{
					entities.push_back(child);
				}
			}
		}
		return entities;
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
