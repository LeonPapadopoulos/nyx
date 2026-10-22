#pragma once

#include "Entity.h"
#include "EntityGuid.h"
#include "ReflectionMacros.h"

#include <glm/glm.hpp>

#include <optional>

namespace Nyx
{
	struct EntityGuidLookup;

	// A point on an entity's surface, with the surface's normal and a tangent along it, that moves
	// with the entity, such as a mark drawn on a rock. It is kept in the host's local space and names
	// the host by guid, never by Entity handle, so a component can hold one as a property that saves,
	// undoes and goes over the editor link like any other. Without a host (guid 0) it is in world
	// space.
	NYX_REFLECT()
	struct SurfaceAnchor
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Tooltip = "The entity the anchor is on; 0 for none, which puts the anchor in world space"))
		EntityGuid Host;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 0.01))
		glm::vec3 LocalPosition{ 0.0f };

		// Needn't be unit length: resolving normalizes it
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 0.01))
		glm::vec3 LocalNormal{ 0.0f, 1.0f, 0.0f };

		// Needn't be at right angles to the normal: resolving makes it so
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 0.01))
		glm::vec3 LocalTangent{ 1.0f, 0.0f, 0.0f };
	};

	// Where an anchor is in the world. Normal and Tangent are unit length and at right angles, and
	// Bitangent = cross(Tangent, Normal) completes a right-handed frame.
	struct SurfaceFrame
	{
		glm::vec3 Position{ 0.0f };
		glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };
		glm::vec3 Tangent{ 1.0f, 0.0f, 0.0f };
		glm::vec3 Bitangent{ 0.0f, 0.0f, 1.0f };

		// From the frame to the world: x along the tangent, y along the normal, z along the
		// bitangent, the origin at Position. Places a mesh flat on the surface.
		glm::mat4 ToMatrix() const
		{
			return glm::mat4(glm::vec4(Tangent, 0.0f), glm::vec4(Normal, 0.0f), glm::vec4(Bitangent, 0.0f),
				glm::vec4(Position, 1.0f));
		}
	};

	// The anchor's frame in the world now, following its host's TransformComponent: position,
	// rotation and scale, with the normal kept at right angles to the surface under any scale. A host
	// without a TransformComponent is at the origin, unrotated and unscaled. Empty when the host guid
	// names no entity of the world, such as one that was deleted.
	std::optional<SurfaceFrame> ResolveSurfaceAnchor(const Registry& world, const SurfaceAnchor& anchor);

	// The same, finding the host in a lookup instead of going through every guid of the world, for
	// resolving many anchors in one frame
	std::optional<SurfaceFrame> ResolveSurfaceAnchor(
		const Registry& world, const EntityGuidLookup& lookup, const SurfaceAnchor& anchor);

	// The anchor on host that resolves to this world frame: the inverse of ResolveSurfaceAnchor(). The
	// tangent is made unit length and at right angles to the normal first. A null host gives a
	// world-space anchor, and so does a host without a GuidComponent: it resolves to the same frame,
	// but doesn't follow the entity. Along an axis the host is scaled to 0, the position is taken as
	// 0, since every position along it lands on the same point.
	SurfaceAnchor MakeSurfaceAnchor(const Registry& world, Entity host, const glm::vec3& worldPosition,
		const glm::vec3& worldNormal, const glm::vec3& worldTangent);
}

#include "Generated/Runtime/SurfaceAnchor.reflect.h"
