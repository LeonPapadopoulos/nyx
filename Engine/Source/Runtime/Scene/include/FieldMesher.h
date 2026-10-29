#pragma once

#include "MeshData.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace Nyx
{
	// A signed field sampled on a grid: negative inside, 0 or more outside. Sample (i, j, k) is at
	// Origin + (i, j, k) * CellSize; a cell is the cube between eight neighbouring samples.
	struct FieldGrid
	{
		glm::ivec3 Size{ 0 };
		glm::vec3 Origin{ 0.0f };
		float CellSize = 1.0f;
		// i fastest, then j, then k
		std::vector<float> Values;
		// One a sample, or none for material 0 everywhere
		std::vector<uint32_t> Materials;

		size_t IndexOf(int i, int j, int k) const { return size_t(i) + size_t(Size.x) * (size_t(j) + size_t(Size.y) * size_t(k)); }
		float GetValue(int i, int j, int k) const { return Values[IndexOf(i, j, k)]; }
		uint32_t GetMaterial(int i, int j, int k) const { return Materials.empty() ? 0u : Materials[IndexOf(i, j, k)]; }
	};

	struct FieldMeshSettings
	{
		// Which quads this mesh makes: those of the grid edges starting at a sample from QuadMin up to
		// (not including) QuadMax on every axis. A chunk makes the quads of its own edges only, so
		// chunks side by side meet without a gap or an overlap. Its grid needs two samples before its
		// own and one after: the cells its quads join, and the samples either side of their corners
		// for the gradients, so a cell's vertex is the same bits in both chunks that have it. 0 or
		// less: from 1 to Size - 1, all the grid can make.
		glm::ivec3 QuadMin{ 0 };
		glm::ivec3 QuadMax{ 0 };

		// The materials whose vertices go where their surface's planes meet (sharp: cliffs, cut
		// stone), by material number; every other vertex at the middle of its crossings (smooth: earth,
		// sand)
		std::vector<bool> SharpMaterials;
		// How strongly a sharp vertex is drawn towards the middle of its crossings, against the planes:
		// more keeps it steadier where the planes are nearly parallel
		float SharpBias = 0.002f;

		// How much of the sky each vertex sees, from rays up to this many metres through the field;
		// 0: none worked out (1 everywhere)
		float SkyReach = 16.0f;

		// The field itself, anywhere: values at the points given (in the grid's space, as three
		// arrays), asked once for every point needed. With it, the planes a sharp vertex is placed by
		// come from its gradient at the crossings, exact enough to keep corners, and sky rays go on
		// past the grid; without it, the planes come from the samples' gradients, which blend near a
		// corner, and the sky is open past the grid.
		std::function<void(const float* x, const float* y, const float* z, size_t count, float* outValues)> Field;
	};

	// What making a mesh did, for tests and the frame stats
	struct FieldMeshStats
	{
		uint32_t Vertices = 0;
		uint32_t Triangles = 0;
		uint32_t SharpVertices = 0;
		// Cells whose surface crossing them is in more than one piece, so they got a vertex for each
		uint32_t SplitCells = 0;
		// Sharp vertices moved back to the middle of their crossings because a triangle of theirs
		// would have faced the wrong way
		uint32_t UnfoldedVertices = 0;
		// Triangles facing against the field's normals at their corners: turned inside out
		uint32_t BackFacingTriangles = 0;
	};

	// The surface where the field crosses 0, by dual meshing: a vertex in every cell the surface
	// passes through (one for each separate piece of it there), and a quad for every grid edge the
	// surface crosses, joining the vertices of the four cells round it, as two triangles
	// counter-clockwise from outside, split along the diagonal whose triangles agree best with the
	// normals. A vertex goes where the planes of its crossings meet (sharp, by material) or at their
	// middle (smooth), always inside its cell; a sharp one whose quads would face the wrong way goes
	// to the middle. A face of a cell with all four edges crossed joins its inside corners when the
	// face's middle is inside, so both cells sharing the face agree. The mesh is closed; it is
	// manifold except where such a face's two curves are one piece in both cells, where four
	// triangles share an edge.
	//
	// The normals come from the field's gradient at the samples (from the samples either side),
	// blended through a cell. A sharp vertex's crossings and their planes come from the field itself
	// where the settings give it (FieldMeshSettings::Field), else from the samples and their
	// gradients along the edge.
	//
	// Each vertex, in the grid's space: its position; its normal; Custom.x how much of the sky it sees
	// (0 to 1), Custom.y its material's number (the most common material of the inside corners of its
	// crossings), Custom.z 1 if sharp. A function of the grid and the settings alone: the same bits
	// every time, and a cell two chunks share the same bits in both.
	MeshData MakeFieldMesh(const FieldGrid& grid, const FieldMeshSettings& settings = {}, FieldMeshStats* outStats = nullptr);
}
