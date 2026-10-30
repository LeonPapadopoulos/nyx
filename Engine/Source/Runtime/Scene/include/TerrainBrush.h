#pragma once

#include "CameraProjection.h"
#include "Entity.h"
#include "ReflectionMacros.h"
#include "SdfTerrainComponent.h"

#include <glm/glm.hpp>

#include <optional>
#include <vector>

namespace Nyx
{
	// A brush that edits an SDF terrain where a ray meets it, the editor's and a game's: what each of
	// its edits is (SdfTerrainEdit), and how far apart they go along a stroke
	NYX_REFLECT()
	struct TerrainBrushSettings
	{
		NYX_PROPERTY(Edit, Serialize, meta = (Tooltip = "Sphere: a ball at each place; Capsule: a rod from each place to the one before, a stroke one smooth trench or ridge; Box: a cube along the terrain's axes"))
		ESdfTerrainEditShape Shape = ESdfTerrainEditShape::Sphere;

		NYX_PROPERTY(Edit, Serialize, meta = (Tooltip = "Subtract digs, Add heaps up of the Material, Paint gives the terrain the Material"))
		ESdfTerrainEditMode Mode = ESdfTerrainEditMode::Subtract;

		NYX_PROPERTY(Edit, Serialize, meta = (DragSpeed = 0.05, Tooltip = "In metres: the ball's and the rod's radius, the cube's half size"))
		float Radius = 2.0f;

		NYX_PROPERTY(Edit, Serialize, meta = (DisplayName = "Blend Radius", DragSpeed = 0.05, Tooltip = "Over how many metres each edit blends into the terrain, rounding its edge; 0: sharp"))
		float BlendRadius = 0.5f;

		NYX_PROPERTY(Edit, Serialize, meta = (Tooltip = "Add and Paint: the material number given"))
		uint32_t Material = 0;

		NYX_PROPERTY(Edit, Serialize, meta = (DragSpeed = 0.01, Tooltip = "How far apart its edits go along a stroke, as a part of the radius"))
		float Spacing = 0.5f;

		NYX_PROPERTY(Edit, Serialize, meta = (DragSpeed = 1.0, Tooltip = "How far a ray looks for the terrain, in metres"))
		float Reach = 1000.0f;
	};

	// The smallest radius and spacing a brush uses, whatever its settings say
	constexpr float TerrainBrushMinRadius = 0.1f;
	constexpr float TerrainBrushMinSpacing = 0.05f;

	// Where a ray meets an SDF terrain: in the world's space and in the terrain entity's
	struct TerrainBrushHit
	{
		Entity Terrain;
		glm::vec3 Point{ 0.0f };
		glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };
		glm::vec3 LocalPoint{ 0.0f };
		// Along the ray, in metres
		float Distance = 0.0f;
	};

	// The SDF terrain a ray in the world meets first within `reach` metres (RayCastTerrain(), through
	// each terrain entity's transform), or only `terrain` if given; none if it meets none
	std::optional<TerrainBrushHit> CastTerrainBrush(const Registry& world, const Ray& ray, float reach, std::optional<Entity> terrain = std::nullopt);

	// One edit of the brush at `to`, in the terrain's space: a ball, a cube along the terrain's axes,
	// or a rod from `from` to `to`. Its revision is left 0: AppendTerrainEdit() gives it one.
	SdfTerrainEdit MakeTerrainBrushEdit(const TerrainBrushSettings& settings, const glm::vec3& from, const glm::vec3& to);

	// A stroke of the brush across one terrain: its first edit where it starts, then one each time it
	// has gone Spacing times the radius from the last, those between filled in when it jumps, the
	// rod's from the one before. In the terrain's space, so a stroke on a moving terrain stays on it.
	class TerrainBrushStroke
	{
	public:
		TerrainBrushStroke(const TerrainBrushSettings& settings, Entity terrain);

		Entity GetTerrain() const
		{
			return Terrain;
		}

		// The edits the brush makes going to `localPoint`; none until it has gone far enough
		std::vector<SdfTerrainEdit> MoveTo(const glm::vec3& localPoint);

	private:
		TerrainBrushSettings Settings;
		Entity Terrain;
		std::optional<glm::vec3> Last;
	};

	// The brush's outline where it meets the terrain, to draw: a ring of the radius in the plane of
	// the surface, `count` points round it, in the world's space
	std::vector<glm::vec3> GetTerrainBrushOutline(const TerrainBrushHit& hit, float radius, int count = 48);
}

#include "Generated/Runtime/TerrainBrush.reflect.h"
