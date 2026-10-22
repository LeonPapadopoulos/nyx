#include "NyxPCH.h"
#include "SurfaceAnchor.h"

#include "EntityGuidLookup.h"
#include "GuidComponent.h"
#include "TransformComponent.h"

#include <cmath>

namespace Nyx
{
	namespace
	{
		// Shorter than this, a direction is taken as none
		constexpr float MinDirectionLength = 1e-6f;

		glm::vec3 NormalizeOr(const glm::vec3& direction, const glm::vec3& fallback)
		{
			const float length = glm::length(direction);
			return length > MinDirectionLength ? direction / length : fallback;
		}

		// A unit vector at right angles to the unit vector direction
		glm::vec3 AnyPerpendicular(const glm::vec3& direction)
		{
			// Crossed with an axis far from direction, so the result is never near zero
			const glm::vec3 axis = std::abs(direction.x) < 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
			return glm::normalize(glm::cross(direction, axis));
		}

		// The tangent's part at right angles to the unit normal, unit length; any tangent at right
		// angles when it has none, such as a tangent along the normal
		glm::vec3 TangentAlong(const glm::vec3& normal, const glm::vec3& tangent)
		{
			return NormalizeOr(tangent - normal * glm::dot(normal, tangent), AnyPerpendicular(normal));
		}

		// What normals are scaled by: the inverse of the scale, up to a positive factor, so they stay
		// at right angles to a surface that is stretched. Its cofactors rather than its inverse, so a
		// scale of 0 along one axis still gives the flattened surface's normal; their sign turned
		// for a mirroring scale, whose negative determinant would turn normals inside.
		glm::vec3 NormalScale(const glm::vec3& scale)
		{
			const glm::vec3 cofactors(scale.y * scale.z, scale.x * scale.z, scale.x * scale.y);
			return scale.x * scale.y * scale.z < 0.0f ? -cofactors : cofactors;
		}

		float InverseOrZero(float value)
		{
			return value != 0.0f ? 1.0f / value : 0.0f;
		}

		const TransformComponent& GetHostTransform(const Registry& world, Entity host)
		{
			static const TransformComponent Identity;
			return world.Has<TransformComponent>(host) ? world.Get<TransformComponent>(host) : Identity;
		}

		SurfaceFrame ResolveOn(const TransformComponent& host, const SurfaceAnchor& anchor)
		{
			SurfaceFrame frame;
			frame.Position = host.Position + host.Rotation * (host.Scale * anchor.LocalPosition);
			frame.Normal = NormalizeOr(
				host.Rotation * (NormalScale(host.Scale) * anchor.LocalNormal), host.Rotation * glm::vec3(0.0f, 1.0f, 0.0f));
			frame.Tangent = TangentAlong(frame.Normal, host.Rotation * (host.Scale * anchor.LocalTangent));
			frame.Bitangent = glm::cross(frame.Tangent, frame.Normal);
			return frame;
		}

		std::optional<SurfaceFrame> ResolveOnHost(
			const Registry& world, std::optional<Entity> host, const SurfaceAnchor& anchor)
		{
			if (!anchor.Host.IsValid())
			{
				return ResolveOn(TransformComponent{}, anchor);
			}

			if (!host)
			{
				return std::nullopt;
			}

			return ResolveOn(GetHostTransform(world, *host), anchor);
		}
	}

	std::optional<SurfaceFrame> ResolveSurfaceAnchor(const Registry& world, const SurfaceAnchor& anchor)
	{
		return ResolveOnHost(world, FindEntityByGuid(world, anchor.Host), anchor);
	}

	std::optional<SurfaceFrame> ResolveSurfaceAnchor(
		const Registry& world, const EntityGuidLookup& lookup, const SurfaceAnchor& anchor)
	{
		return ResolveOnHost(world, FindEntityByGuid(world, lookup, anchor.Host), anchor);
	}

	SurfaceAnchor MakeSurfaceAnchor(const Registry& world, Entity host, const glm::vec3& worldPosition,
		const glm::vec3& worldNormal, const glm::vec3& worldTangent)
	{
		SurfaceAnchor anchor;
		TransformComponent transform;

		if (world.Has<GuidComponent>(host) && world.Get<GuidComponent>(host).Guid.IsValid())
		{
			anchor.Host = world.Get<GuidComponent>(host).Guid;
			transform = GetHostTransform(world, host);
		}

		const glm::vec3 normal = NormalizeOr(worldNormal, glm::vec3(0.0f, 1.0f, 0.0f));
		const glm::vec3 tangent = TangentAlong(normal, worldTangent);

		// Undoes ResolveOn(): the rotation by its inverse, the scale by its own. Normals go back by
		// the scale itself, the inverse of NormalScale() up to a positive factor.
		const glm::quat inverseRotation = glm::inverse(transform.Rotation);
		const glm::vec3 inverseScale(
			InverseOrZero(transform.Scale.x), InverseOrZero(transform.Scale.y), InverseOrZero(transform.Scale.z));

		anchor.LocalPosition = inverseScale * (inverseRotation * (worldPosition - transform.Position));
		anchor.LocalNormal = NormalizeOr(transform.Scale * (inverseRotation * normal), glm::vec3(0.0f, 1.0f, 0.0f));
		anchor.LocalTangent = NormalizeOr(inverseScale * (inverseRotation * tangent), AnyPerpendicular(anchor.LocalNormal));
		return anchor;
	}
}
