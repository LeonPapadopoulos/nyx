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

	// Where a grid meets cells twice or half the size of its own, as a chunk meets one at another level
	// of detail, so that the meshes of both meet without a gap. The mesh's own region is the box of
	// samples [QuadMin, QuadMax] (QuadMax included); past its faces, and on them, other places may
	// have other cells, inside it none. The coarser cells' corners are the grid's samples with even
	// indices. Both meshes take a face shared by coarser and finer cells as the coarser cells' face,
	// cut in four, its samples between their corners their average: the grid's samples on such a face
	// (and past it) are the caller's to make so, the coarser mesh makes them itself (Interpolate
	// SeamSample()).
	//
	// The dual mesh joins coarser cells: an edge on a face it shares with them gets a quad (a
	// triangle where two of its cells are one) from the finer cells' vertices and theirs, which it
	// makes itself, the same bits as the coarser mesh does, from the coarser samples (Coarser).
	// Marching cubes meshes its own cells next to finer ones with the finer samples on their shared
	// faces and edges, as the finer mesh has them.
	struct FieldSeams
	{
		// The size of the cells at a point of the grid (in samples from its origin), as a multiple of
		// the grid's own: the finest and the coarsest (x and y) of all the places whose closed
		// boxes have it, each 0.5, 1 or 2. Unset: no seams, and QuadMin and QuadMax as without.
		std::function<glm::vec2(const glm::vec3& at)> CellSizes;
		// Dual: whether this mesh makes the quad of an edge with an end on a face of the box; it must
		// be the finest mesh that has the edge, and only one of those. Every edge strictly within
		// the box it makes.
		std::function<bool(const glm::ivec3& from, int axis)> MakesEdge;
		// Dual: the coarser cells' samples, with as many cells round the box's faces as its quads
		// join and those cells' neighbours need: two cells beyond the box, and one sample more.
		// CellSize twice the grid's, Origin at one of its even samples.
		const FieldGrid* Coarser = nullptr;
	};

	// The value both meshes take at a sample of the finer cells on a face shared with coarser ones:
	// at their corners, the sample; between two, their average; in a face's middle, the average of its
	// four corners; in a cell's middle, of its eight. `corners` are the coarser cells' corners round
	// it, as many as that, in order of the axes it is between them on, the lowest first.
	inline float InterpolateSeamSample(const float* corners, int count)
	{
		float sum = corners[0];
		for (int n = 1; n < count; ++n)
		{
			sum += corners[n];
		}
		return count == 1 ? sum : sum * (1.0f / float(count));
	}

	// Runs `count` parts, each once, on any threads, and returns once all have run, e.g. through
	// JobSystem::ParallelFor(); what is run in parts comes out the same bits as run at once
	using FieldRunParts = std::function<void(size_t count, const std::function<void(size_t)>& part)>;

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

		// Where it meets cells of other sizes; none by default
		FieldSeams Seams;

		// To share the sky's rays out among threads, the vertices in parts; none: all here. The Field
		// is then asked from those threads too.
		FieldRunParts RunParts;
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
		// Triangles joining cells of another size (FieldSeams), and cells meshed with finer samples
		uint32_t SeamTriangles = 0;
		uint32_t SeamCells = 0;
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

	// The same surface by marching cubes, as Transvoxel's regular cells make it, to compare with the
	// dual mesh: a vertex where the surface crosses each grid edge, shared by the cells round it, and
	// in each cell polygons through those vertices, counter-clockwise from outside, fanned into
	// triangles. A cell's polygons are the loops its crossed edges make, joined through its faces as
	// MakeFieldMesh() joins them (a face with all four edges crossed joins its inside corners when
	// its middle is inside), so both cells sharing a face agree and the mesh is closed and manifold.
	// Every vertex smooth: it can't keep a sharp edge. The settings' QuadMin and QuadMax are the cells
	// it meshes here (0 or less: all of them); SharpMaterials and SharpBias aren't used. Normals, sky
	// visibility and materials as MakeFieldMesh()'s, Custom.z 0.
	MeshData MakeMarchingFieldMesh(const FieldGrid& grid, const FieldMeshSettings& settings = {}, FieldMeshStats* outStats = nullptr);
}
