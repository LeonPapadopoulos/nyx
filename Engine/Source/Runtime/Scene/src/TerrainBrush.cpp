#include "NyxPCH.h"
#include "TerrainBrush.h"

#include "SdfTerrain.h"
#include "TransformComponent.h"

#include <algorithm>
#include <cmath>

namespace Nyx
{
	std::optional<TerrainBrushHit> CastTerrainBrush(const Registry& world, const Ray& ray, float reach, std::optional<Entity> terrain)
	{
		std::optional<TerrainBrushHit> nearest;
		world.Each<SdfTerrainComponent>(
			[&](Entity entity, const SdfTerrainComponent& values)
			{
				if (terrain && entity != *terrain)
				{
					return;
				}
				// Into the terrain's space; its scale stretches the ray, so distances are kept in
				// the world's metres
				const glm::mat4 toWorld = world.Has<TransformComponent>(entity) ? world.Get<TransformComponent>(entity).ToMatrix() : glm::mat4(1.0f);
				const glm::mat4 toTerrain = glm::inverse(toWorld);
				const glm::vec3 origin = glm::vec3(toTerrain * glm::vec4(ray.Origin, 1.0f));
				const glm::vec3 along = glm::mat3(toTerrain) * ray.Direction;
				const float stretch = glm::length(along);
				if (!(stretch > 0.0f))
				{
					return;
				}
				const std::optional<TerrainRayHit> hit = RayCastTerrain(values, origin, along / stretch, reach * stretch);
				if (!hit || (nearest && hit->Distance / stretch >= nearest->Distance))
				{
					return;
				}
				TerrainBrushHit found;
				found.Terrain = entity;
				found.LocalPoint = hit->Point;
				found.Point = glm::vec3(toWorld * glm::vec4(hit->Point, 1.0f));
				found.Normal = glm::normalize(glm::transpose(glm::mat3(toTerrain)) * hit->Normal);
				found.Distance = hit->Distance / stretch;
				nearest = found;
			});
		return nearest;
	}

	SdfTerrainEdit MakeTerrainBrushEdit(const TerrainBrushSettings& settings, const glm::vec3& from, const glm::vec3& to)
	{
		SdfTerrainEdit edit;
		edit.Shape = settings.Shape;
		edit.Mode = settings.Mode;
		edit.Position = settings.Shape == ESdfTerrainEditShape::Capsule ? from : to;
		edit.End = to;
		edit.Radius = std::max(settings.Radius, TerrainBrushMinRadius);
		edit.HalfSize = glm::vec3(edit.Radius);
		edit.BlendRadius = std::max(settings.BlendRadius, 0.0f);
		edit.Material = settings.Material;
		return edit;
	}

	TerrainBrushStroke::TerrainBrushStroke(const TerrainBrushSettings& settings, Entity terrain)
		: Settings(settings)
		, Terrain(terrain)
	{
	}

	std::vector<SdfTerrainEdit> TerrainBrushStroke::MoveTo(const glm::vec3& localPoint)
	{
		std::vector<SdfTerrainEdit> edits;
		if (!Last)
		{
			edits.push_back(MakeTerrainBrushEdit(Settings, localPoint, localPoint));
			Last = localPoint;
			return edits;
		}
		const float step = std::max(std::max(Settings.Radius, TerrainBrushMinRadius) * Settings.Spacing, TerrainBrushMinSpacing);
		const float distance = glm::length(localPoint - *Last);
		const int count = static_cast<int>(std::floor(distance / step));
		if (count <= 0)
		{
			return edits;
		}
		// Those between, on the straight line, at the spacing; then from the last of them on
		const glm::vec3 direction = (localPoint - *Last) / distance;
		for (int n = 1; n <= count; ++n)
		{
			const glm::vec3 at = *Last + direction * (step * float(n));
			const glm::vec3 from = n == 1 ? *Last : *Last + direction * (step * float(n - 1));
			edits.push_back(MakeTerrainBrushEdit(Settings, from, at));
		}
		Last = *Last + direction * (step * float(count));
		return edits;
	}

	std::vector<glm::vec3> GetTerrainBrushOutline(const TerrainBrushHit& hit, float radius, int count)
	{
		// Two directions in the surface's plane
		const glm::vec3 normal = glm::length(hit.Normal) > 0.0f ? glm::normalize(hit.Normal) : glm::vec3(0.0f, 1.0f, 0.0f);
		const glm::vec3 other = std::fabs(normal.y) < 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
		const glm::vec3 u = glm::normalize(glm::cross(normal, other));
		const glm::vec3 v = glm::cross(normal, u);
		std::vector<glm::vec3> points;
		points.reserve(size_t(std::max(count, 3)));
		for (int n = 0; n < std::max(count, 3); ++n)
		{
			const float angle = 6.2831853f * float(n) / float(std::max(count, 3));
			points.push_back(hit.Point + (u * std::cos(angle) + v * std::sin(angle)) * radius);
		}
		return points;
	}
}
