#pragma once

#include "Entity.h"
#include "EntityGuid.h"
#include "ReflectionMacros.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vector>

namespace Nyx
{
	struct EntityGuidLookup;
	struct TransformComponent;

	// Links an entity's transform to another entity's: it is placed relative to its parent, and moves,
	// turns and scales with it. Only children have one. The entity's TransformComponent stays its
	// place in the world, which everything else reads; the editor works it out from the parent's and
	// the local values here after every edit (UpdateTransformHierarchy()). A game places children so
	// when a scene loads and a prefab is spawned, and then uses the world transforms as they are,
	// unless a child follows in the game (bFollowInGame): then the game places it again each step its
	// parent moved (TransformFollowers, in Sim). The details panel shows it inside the Transform
	// section, not as a component of its own: the parent as a row that keeps the entity where it is
	// when changed, and the local values as the transform.
	NYX_REFLECT(Component, meta = (DisplayName = "Parent"))
	struct ParentComponent
	{
		// Hidden: the Transform section's Parent row changes it, keeping the entity where it is
		NYX_PROPERTY(Edit, Undo, Serialize, Hidden, meta = (Tooltip = "The entity this one is placed relative to; 0 or one that isn't there leaves it where it is"))
		EntityGuid Parent;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Transform", DisplayName = "Position", DragSpeed = 0.1, Tooltip = "Relative to the parent"))
		glm::vec3 LocalPosition{ 0.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Transform", DisplayName = "Rotation", UI = Degrees, Tooltip = "Relative to the parent"))
		glm::quat LocalRotation{ 1.0f, 0.0f, 0.0f, 0.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Transform", DisplayName = "Scale", DragSpeed = 0.1, Tooltip = "Relative to the parent"))
		glm::vec3 LocalScale{ 1.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Transform", DisplayName = "Follows in Game",
			Tooltip = "Comes along when the parent moves in the game; off, it stays where it was placed. A dynamic body never follows: join it with a constraint"))
		bool bFollowInGame = false;
	};

	// Where a child with these local values is in the world, its parent at `parentWorld`. A parent
	// scaled unevenly with a turned child would skew it, which a position, rotation and scale can't
	// hold: the scale is then multiplied axis by axis, as in Unity.
	TransformComponent GetChildWorldTransform(const TransformComponent& parentWorld, const ParentComponent& link);

	// The local values that put a child at `world`, its parent at `parentWorld`: what keeps a child
	// where it is when it is linked to another parent. An axis the parent scales to 0 gives 0.
	void SetLocalTransform(ParentComponent& link, const TransformComponent& parentWorld, const TransformComponent& world);

	// Gives every linked entity its place in the world again from its parent's, parents before their
	// children, also down long chains. A link to an entity that isn't there (deleted, or guid 0), or
	// one that goes round in a circle, moves nothing: the entity stays where it was. `guids` must be
	// up to date with the world. Only what moved is written; outMoved, if given, gets those entities
	// (cleared first), parents before their children. Goes through the linked entities only, not the
	// whole world.
	void UpdateTransformHierarchy(Registry& world, const EntityGuidLookup& guids, std::vector<Entity>* outMoved = nullptr);

	// A child and its parent, which the world has
	struct TransformLink
	{
		Entity Child;
		Entity Parent;
	};

	// The world's links whose parent is there, both with a transform, parents before their children,
	// the same depth in the order the world has them; those in or below a circle left out. Only those
	// that follow in the game (ParentComponent::bFollowInGame), with bFollowingOnly. `guids` must be
	// up to date with the world.
	std::vector<TransformLink> GetOrderedTransformLinks(const Registry& world, const EntityGuidLookup& guids, bool bFollowingOnly);

	// The entity and everything below it, each parent before its children, children in the order
	// the world has their links; an entity met again (a circle) only once
	std::vector<Entity> GetTransformSubtree(const Registry& world, Entity entity);
}

#include "Generated/Runtime/ParentComponent.reflect.h"
