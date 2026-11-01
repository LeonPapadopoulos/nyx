#include "NyxPCH.h"
#include "TransformFollowers.h"

#include "CharacterComponent.h"
#include "EntityGuidLookup.h"
#include "GuidComponent.h"
#include "Log.h"
#include "NameComponent.h"
#include "PhysicsComponents.h"
#include "PreviousTransformComponent.h"
#include "Profiler.h"

#include <string>
#include <unordered_map>

namespace Nyx
{
	namespace
	{
		bool SameTransform(const TransformComponent& left, const TransformComponent& right)
		{
			return left.Position == right.Position && left.Rotation == right.Rotation && left.Scale == right.Scale;
		}

		bool SameLocal(const ParentComponent& left, const ParentComponent& right)
		{
			return left.LocalPosition == right.LocalPosition && left.LocalRotation == right.LocalRotation && left.LocalScale == right.LocalScale;
		}

		// Physics moves it: a dynamic body or a character
		bool IsMovedByPhysics(const Registry& world, Entity entity)
		{
			return (world.Has<RigidBodyComponent>(entity) && !world.Get<RigidBodyComponent>(entity).bKinematic) || world.Has<CharacterComponent>(entity);
		}

		// By its name, else its guid, as the editor shows them
		std::string LabelOf(const Registry& world, Entity entity)
		{
			if (world.Has<NameComponent>(entity) && !world.Get<NameComponent>(entity).Name.empty())
			{
				return "'" + world.Get<NameComponent>(entity).Name + "'";
			}
			if (world.Has<GuidComponent>(entity))
			{
				return "Entity " + std::to_string(world.Get<GuidComponent>(entity).Guid.Value);
			}
			return "An entity";
		}
	}

	void TransformFollowers::Find(Registry& world, const EntityGuidLookup& guids)
	{
		NYX_PROFILE_SCOPE("TransformFollowers::Find");
		std::unordered_map<uint64_t, Follower> kept;
		for (const Follower& follower : Followers)
		{
			kept.emplace(follower.Child.Value, follower);
		}

		Followers.clear();
		Waiting.clear();
		FoundVersion = world.GetVersion<ParentComponent>();
		bFound = true;

		for (const TransformLink& link : GetOrderedTransformLinks(world, guids, true))
		{
			if (IsMovedByPhysics(world, link.Child))
			{
				if (WarnedMovedByPhysics.insert(link.Child.Value).second)
				{
					LOG_WARNING("{0} follows its parent in the game, but physics moves its body: join the two with a constraint, or make "
								"its Rigid Body kinematic",
						LabelOf(world, link.Child));
				}
				continue;
			}

			const auto found = kept.find(link.Child.Value);
			Follower follower = found != kept.end() && found->second.Parent == link.Parent ? found->second : Follower{};
			follower.Child = link.Child;
			follower.Parent = link.Parent;
			Followers.push_back(follower);
		}

		// Those whose parent isn't there yet
		world.Each<ParentComponent>(
			[&](Entity child, const ParentComponent& link)
			{
				if (link.bFollowInGame && link.Parent.IsValid() && !FindEntityByGuid(world, guids, link.Parent))
				{
					Waiting.push_back(child);
				}
			});
	}

	void TransformFollowers::Update(Registry& world, const EntityGuidLookup& guids)
	{
		NYX_PROFILE_SCOPE("TransformFollowers::Update");
		bool bFindAgain = !bFound || world.GetVersion<ParentComponent>() != FoundVersion;
		for (size_t index = 0; index < Waiting.size() && !bFindAgain; ++index)
		{
			const Entity child = Waiting[index];
			bFindAgain = world.Has<ParentComponent>(child) && FindEntityByGuid(world, guids, world.Get<ParentComponent>(child).Parent).has_value();
		}
		if (bFindAgain)
		{
			Find(world, guids);
		}

		bool bLost = false;
		for (Follower& follower : Followers)
		{
			// A parent destroyed changes no link: found again next step, the follower then waits for it
			if (!world.Has<TransformComponent>(follower.Child) || !world.Has<TransformComponent>(follower.Parent) ||
				!world.Has<ParentComponent>(follower.Child))
			{
				bLost = true;
				continue;
			}

			const TransformComponent& parent = world.Get<TransformComponent>(follower.Parent);
			const ParentComponent& link = world.Get<ParentComponent>(follower.Child);
			TransformComponent& transform = world.Get<TransformComponent>(follower.Child);
			if (follower.bPlaced && SameTransform(parent, follower.LastParent) && SameTransform(transform, follower.LastPlaced) &&
				SameLocal(link, follower.LastLink))
			{
				world.Remove<PreviousTransformComponent>(follower.Child);
				continue;
			}

			// Drawn from where it was, unless something else put it elsewhere since, which is drawn where it is
			const bool bFromLast = follower.bPlaced && SameTransform(transform, follower.LastPlaced);
			const TransformComponent before = transform;
			const TransformComponent placed = GetChildWorldTransform(parent, link);
			transform.Position = placed.Position;
			transform.Rotation = placed.Rotation;
			transform.Scale = placed.Scale;
			if (bFromLast && !SameTransform(before, transform))
			{
				PreviousTransformComponent& previous = world.Has<PreviousTransformComponent>(follower.Child)
					? world.Get<PreviousTransformComponent>(follower.Child)
					: world.Add<PreviousTransformComponent>(follower.Child);
				SetPreviousTransform(previous, before.Position, before.Rotation, transform);
			}
			else
			{
				world.Remove<PreviousTransformComponent>(follower.Child);
			}

			follower.LastParent = parent;
			follower.LastLink = link;
			follower.LastPlaced = transform;
			follower.bPlaced = true;
		}
		if (bLost)
		{
			bFound = false;
		}
	}
}
