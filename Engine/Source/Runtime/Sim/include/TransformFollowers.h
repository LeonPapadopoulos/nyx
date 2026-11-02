#pragma once

#include "Entity.h"
#include "ParentComponent.h"
#include "TransformComponent.h"

#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>

namespace Nyx
{
	struct EntityGuidLookup;

	// The children that follow their parents in a game (ParentComponent::bFollowInGame), placed again
	// after each physics step, parents before their children (GameWorld). Everything else keeps the
	// world transform it was placed at.
	// - What follows is found once and kept, and found again only when the world's links change
	//   (Registry::GetVersion<ParentComponent>(): a link added or removed, an entity with one
	//   destroyed, a link's values read from a file, a spawn or the editor), so a world without
	//   followers costs one comparison a step.
	// - Each step, only a follower whose parent, own transform or local values changed since it was
	//   last placed is placed again. A placed one gets a PreviousTransformComponent from where it was,
	//   to be drawn between the two as the bodies are; one that held still has none.
	// - A follower with a kinematic body moves its body to its transform over the next step, pushing
	//   what it meets (PhysicsBodies), so physics sees it one step late. A follower with a dynamic body
	//   or a character never follows, as physics moves it, with a warning once: a constraint or a body
	//   group joins bodies.
	// - A follower whose parent is gone stays where it is, and follows again once an entity with the
	//   parent's guid is there, e.g. brought back by the editor's undo.
	class TransformFollowers
	{
	public:
		// After the physics step; `guids` as made at the step's start
		void Update(Registry& world, const EntityGuidLookup& guids);

		// The followers found, whose parents are there
		size_t GetFollowerCount() const
		{
			return Followers.size();
		}

	private:
		struct Follower
		{
			Entity Child;
			Entity Parent;
			// As it was last placed: the parent's transform, the child's local values and where that put it
			TransformComponent LastParent;
			ParentComponent LastLink;
			TransformComponent LastPlaced;
			bool bPlaced = false;
		};

		// Finds the followers again, keeping what was last placed of those that stay
		void Find(Registry& world, const EntityGuidLookup& guids);

		// Parents before their children
		std::vector<Follower> Followers;
		// Followers whose parent wasn't there, looked for each step
		std::vector<Entity> Waiting;
		uint64_t FoundVersion = 0;
		bool bFound = false;
		// The followers warned of for a body that physics moves, by entity
		std::set<uint64_t> WarnedMovedByPhysics;
	};
}
