#pragma once

#include "Entity.h"
#include "MeshData.h"
#include "MeshInstancesComponent.h"
#include "ScatterComponent.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace Nyx
{
	// At most this many instances a scatter, whatever its density: 64 MB of transforms
	constexpr uint32_t MaxScatterInstances = 1u << 20;

	// A blocker's shape (ScatterBlockerComponent) as one scatter sees it, ready to test its points
	// against
	struct ScatterBlocker
	{
		enum class EKind : uint8_t
		{
			Circle,
			Box,
			Sphere,
			Capsule
		};

		// Orders the blockers, so their densities are multiplied in the same order wherever a cell is
		// made: the entity's guid
		uint64_t Key = 0;
		EKind Kind = EKind::Circle;
		// From the scatter's space into the shape's, in metres. A circle's stands along the scatter's
		// Y, round its origin; a box, a sphere and a capsule are round their origin, along its axes,
		// a capsule along Y.
		glm::mat4 ScatterToShape{ 1.0f };
		// Box: half its size; sphere and circle: x the radius; capsule: x the radius, y half the
		// middle's length
		glm::vec3 Size{ 0.0f };
		float Margin = 0.0f;
		float Falloff = 0.0f;
		// What it reaches in the scatter's X and Z, the margin and falloff with it: its density is 1
		// everywhere outside
		glm::vec2 ReachMin{ 0.0f };
		glm::vec2 ReachMax{ 0.0f };
		// Of the blocker's values, its place to the centimetre, and the scatter's transform: the
		// same hash, the same effect
		uint64_t Hash = 0;
	};

	// The blockers of the world as the scatter entity whose transform is scatterToWorld sees them, in
	// their order (by Key). Each one's position is taken to the centimetre, its rotation to about a
	// tenth of a degree and its scale to a thousandth, so moving one less than that changes nothing.
	std::vector<ScatterBlocker> GatherScatterBlockers(const Registry& world, const glm::mat4& scatterToWorld);

	// How much of the density a blocker leaves at a point in the scatter's space: 0 within its shape
	// and margin, 1 past the falloff, smooth between
	float GetBlockerDensity(const ScatterBlocker& blocker, const glm::vec3& point);

	// The scatter's noise mask before its threshold: about 0 to 1, half of it above 0.5, at a point
	// of the scatter's X and Z
	float SampleScatterNoise(const ScatterComponent& scatter, float x, float z);

	// How much of the density the noise mask leaves there: 1 everywhere when it is off
	float GetNoiseMaskDensity(const ScatterComponent& scatter, float x, float z);

	// A scatter's instances, cell by cell, as last made, so a change in one place makes only the cells
	// it reaches again (UpdateScatterCells()). Each cell's instances are a function of the scatter's
	// values, the surface's triangles in it, the blockers that reach it, and the instances its
	// neighbours before it threw, which the spacing keeps apart from: the same whether one cell or all
	// of them are made.
	struct ScatterCells
	{
		// A triangle of the surface, or a part of one cut along the cell's edges, in the scatter's space
		struct Piece
		{
			glm::vec3 Positions[3];
			glm::vec3 Normals[3];
		};

		struct Cell
		{
			// Its square of the area, in the scatter's X and Z
			glm::vec2 Min{ 0.0f };
			glm::vec2 Max{ 0.0f };
			std::vector<Piece> Pieces;
			// The pieces' areas added up, to pick one by its area
			std::vector<double> AreasUpTo;
			// Of the pieces: when the surface changes, only the cells whose pieces did are made again
			uint64_t SurfaceHash = 0;
			// The points thrown and kept within the cell, and their transforms
			std::vector<glm::vec3> Points;
			std::vector<glm::mat4> Thrown;
			// Those of them also far enough from the points of the cells before it
			std::vector<glm::mat4> Kept;
		};

		bool bMade = false;
		// HashScatterValues() of the values the cells were made with
		uint64_t Values = 0;
		// What the pieces were cut from, as the caller tells it (UpdateScatterCells())
		uint64_t Surface = 0;
		glm::vec2 HalfSize{ 0.0f };
		float CellSize = 1.0f;
		uint32_t Columns = 0;
		uint32_t Rows = 0;
		std::vector<Cell> Cells;
		// The blockers the cells were made with
		std::vector<ScatterBlocker> Blockers;

		// What the last update did: cells whose points were thrown again, and cells whose kept
		// instances were looked at again, those and the ones after them within the spacing
		uint32_t CellsThrown = 0;
		uint32_t CellsKeptApart = 0;
	};

	// Makes the cells again where something changed: all of them for other values; for another
	// surface (surfaceKey, e.g. its mesh's revision and where it lies, differs), the cells whose
	// pieces of it changed; for blockers added, removed or changed, the cells they reached before or
	// after. Then the cells after each of those within the spacing, to keep apart from them again.
	// The surface's space goes into the scatter's by surfaceToScatter. True if any cell was made again.
	bool UpdateScatterCells(ScatterCells& cells, const ScatterComponent& scatter, const MeshData& surface, const glm::mat4& surfaceToScatter, uint64_t surfaceKey,
		std::span<const ScatterBlocker> blockers);

	// The cells' kept instances, one cell after another, each non-empty cell with the bounds of its
	// origins
	MeshInstances CollectScatterInstances(const ScatterCells& cells);

	// The scatter's instances on a surface mesh whose space goes into the scatter entity's by
	// surfaceToScatter, all its cells made at once. The area is cut into squares of CellSize, and each
	// square's part of the surface sampled on its own, by area: density times its area points, each
	// with its own hashes of the seed, the square and its number. One is kept if it lies within the
	// slope and height limits (against the normal between the triangle's vertex normals, in the
	// scatter's space), survives the noise mask and the blockers (a hash against the density they
	// leave), and is at least Spacing from every one kept before in its square and every one of the
	// squares before it. Each is scaled, turned about its up and tilted to the normal as the values
	// say, and sunk along its up. A function of the values, the mesh and the blockers alone, the same
	// on every machine: integer hashes, and arithmetic that IEEE 754 rounds the same everywhere.
	MeshInstances ScatterOnMesh(const ScatterComponent& scatter, const MeshData& surface, const glm::mat4& surfaceToScatter,
		std::span<const ScatterBlocker> blockers = {});

	// On the flat plane through the scatter entity, across its area: where a scatter without a
	// surface puts its instances
	MeshInstances ScatterOnPlane(const ScatterComponent& scatter, std::span<const ScatterBlocker> blockers = {});

	// The flat plane a scatter without a surface stands on, over its area
	MeshData MakeScatterPlane(const ScatterComponent& scatter);

	// A hash of every value the instances are made from, the surface's guid among them
	uint64_t HashScatterValues(const ScatterComponent& scatter);

	// The scatter's post-load step: gives `entity` the scatter's instances (MeshInstancesComponent)
	// and raises their revision, unless they were made from the same values on the same surface,
	// lying the same way, with the same blockers. Makes again only the cells that changed
	// (ScatterComponent::Cells, UpdateScatterCells()). A surface that has no mesh yet, or none of
	// this world, gives no instances, until UpdateScatters() finds it has one.
	void UpdateScatter(ScatterComponent& scatter, Registry& world, Entity entity);

	// UpdateScatter() for every scatter of the world: each frame, in the editor and the game, so the
	// instances follow their surfaces and the blockers, which other entities' steps and the editor
	// change, in whatever order they were loaded. Costs a guid lookup, a hash and a look at each
	// blocker a scatter when nothing changed.
	void UpdateScatters(Registry& world);

	// Its remove step: the instances go with the component
	void RemoveScatter(Registry& world, Entity entity);
}
