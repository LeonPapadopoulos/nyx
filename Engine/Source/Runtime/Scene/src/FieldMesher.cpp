#include "NyxPCH.h"
#include "FieldMesher.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <unordered_map>

// Every operation as written, never fused into a multiply-add: the same mesh on every machine
#pragma fp_contract(off)

namespace Nyx
{
	namespace
	{
		// A cell's corners by bit: 1 is +x, 2 is +y, 4 is +z. Its twelve edges: four along each axis,
		// from the corner without that axis's bit.
		constexpr int EdgeCount = 12;

		struct CellEdge
		{
			int From = 0;
			int To = 0;
			int Axis = 0;
		};

		constexpr std::array<CellEdge, EdgeCount> MakeEdges()
		{
			std::array<CellEdge, EdgeCount> edges{};
			int next = 0;
			for (int axis = 0; axis < 3; ++axis)
			{
				for (int corner = 0; corner < 8; ++corner)
				{
					if ((corner & (1 << axis)) == 0)
					{
						edges[next++] = CellEdge{ corner, corner | (1 << axis), axis };
					}
				}
			}
			return edges;
		}

		constexpr std::array<CellEdge, EdgeCount> Edges = MakeEdges();

		constexpr int EdgeBetween(int a, int b)
		{
			for (int edge = 0; edge < EdgeCount; ++edge)
			{
				if ((Edges[edge].From == a && Edges[edge].To == b) || (Edges[edge].From == b && Edges[edge].To == a))
				{
					return edge;
				}
			}
			return -1;
		}

		// A cell's six faces, each's corners in order round it and the edges between them: the same
		// order of the same samples for both cells that share a face
		struct CellFace
		{
			int Corners[4] = {};
			int Edges[4] = {};
		};

		constexpr std::array<CellFace, 6> MakeFaces()
		{
			std::array<CellFace, 6> faces{};
			for (int axis = 0; axis < 3; ++axis)
			{
				const int b = (axis + 1) % 3;
				const int c = (axis + 2) % 3;
				for (int side = 0; side < 2; ++side)
				{
					CellFace& face = faces[axis * 2 + side];
					const int cycle[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
					for (int k = 0; k < 4; ++k)
					{
						face.Corners[k] = (side << axis) | (cycle[k][0] << b) | (cycle[k][1] << c);
					}
					for (int k = 0; k < 4; ++k)
					{
						face.Edges[k] = EdgeBetween(face.Corners[k], face.Corners[(k + 1) % 4]);
					}
				}
			}
			return faces;
		}

		constexpr std::array<CellFace, 6> Faces = MakeFaces();

		// The four cells round a grid edge, counter-clockwise about its axis seen from its far end:
		// each's offset from the edge's start along the other two axes, less one
		constexpr int Around[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };

		glm::ivec3 CornerOffset(int corner)
		{
			return { corner & 1, (corner >> 1) & 1, (corner >> 2) & 1 };
		}

		bool IsInside(float value)
		{
			return value < 0.0f;
		}

		// The field within a cell, from its eight corners
		struct CellValues
		{
			float Corner[8] = {};

			float At(const glm::vec3& local) const
			{
				const float x0 = Corner[0] + (Corner[1] - Corner[0]) * local.x;
				const float x1 = Corner[2] + (Corner[3] - Corner[2]) * local.x;
				const float x2 = Corner[4] + (Corner[5] - Corner[4]) * local.x;
				const float x3 = Corner[6] + (Corner[7] - Corner[6]) * local.x;
				const float y0 = x0 + (x1 - x0) * local.y;
				const float y1 = x2 + (x3 - x2) * local.y;
				return y0 + (y1 - y0) * local.z;
			}
		};

		// The field's gradient at a sample, in field units a cell: from the samples either side, or
		// the one side there is at the grid's edge
		glm::vec3 SampleGradient(const FieldGrid& grid, const glm::ivec3& at)
		{
			glm::vec3 gradient(0.0f);
			for (int axis = 0; axis < 3; ++axis)
			{
				glm::ivec3 before = at;
				glm::ivec3 after = at;
				before[axis] = std::max(at[axis] - 1, 0);
				after[axis] = std::min(at[axis] + 1, grid.Size[axis] - 1);
				const float span = float(after[axis] - before[axis]);
				gradient[axis] = span > 0.0f ? (grid.GetValue(after.x, after.y, after.z) - grid.GetValue(before.x, before.y, before.z)) / span : 0.0f;
			}
			return gradient;
		}

		CellValues ReadCell(const FieldGrid& grid, const glm::ivec3& cell)
		{
			CellValues values;
			for (int corner = 0; corner < 8; ++corner)
			{
				const glm::ivec3 at = cell + CornerOffset(corner);
				values.Corner[corner] = grid.GetValue(at.x, at.y, at.z);
			}
			return values;
		}

		int FindRoot(std::array<int, EdgeCount>& parents, int edge)
		{
			while (parents[edge] != edge)
			{
				parents[edge] = parents[parents[edge]];
				edge = parents[edge];
			}
			return edge;
		}

		void Join(std::array<int, EdgeCount>& parents, int a, int b)
		{
			const int rootA = FindRoot(parents, a);
			const int rootB = FindRoot(parents, b);
			if (rootA != rootB)
			{
				parents[std::max(rootA, rootB)] = std::min(rootA, rootB);
			}
		}

		// A vertex being made. Kept as its cell and where it is within it (0 to 1 on each axis), never as
		// a position in the grid: a cell two chunks share is at different grid positions in each, and
		// only its own corners and the offset within it come out the same bits in both.
		struct WorkVertex
		{
			glm::ivec3 Cell{ 0 };
			glm::vec3 Local{ 0.5f };
			// The middle of its crossings
			glm::vec3 MassLocal{ 0.5f };
			uint32_t Material = 0;
			bool bSharp = false;
			// Its grid: 0 the mesh's own, 1 that of the coarser cells round it (FieldSeams::Coarser)
			uint8_t Level = 0;
			// A quad of this mesh joins it, so it goes into the mesh
			bool bUsed = false;
			// A sharp vertex's crossings (Mesher::SharpCrossings)
			uint32_t FirstCrossing = 0;
			uint32_t CrossingCount = 0;
		};

		// A crossing of a sharp vertex's edges, in the grid's space (metres), with the samples'
		// gradient there, and the edge's ends with the field there, to find it on the field itself
		struct Crossing
		{
			glm::vec3 Point{ 0.0f };
			glm::vec3 Gradient{ 0.0f };
			glm::vec3 Inside{ 0.0f };
			glm::vec3 Outside{ 0.0f };
			float InsideValue = 0.0f;
			float OutsideValue = 0.0f;
			// The field at Point, once found on it
			float PointValue = 0.0f;
			// How far from Point its gradient is taken: a twentieth of its cell
			float Step = 0.0f;
		};

		// What is known of a cell once its vertices are made
		struct CellRecord
		{
			bool bMade = false;
			int32_t FirstVertex = -1;
			// Each edge's piece of the surface; -1 for an edge the surface doesn't cross
			std::array<int8_t, EdgeCount> EdgePiece{};
		};

		// Solves (A + bias I) y = b for a symmetric A; y = 0 where it can't
		glm::vec3 SolveSymmetric(const float a[6], const glm::vec3& b)
		{
			// a: xx, xy, xz, yy, yz, zz
			const float m00 = a[0], m01 = a[1], m02 = a[2], m11 = a[3], m12 = a[4], m22 = a[5];
			const float c00 = m11 * m22 - m12 * m12;
			const float c01 = m02 * m12 - m01 * m22;
			const float c02 = m01 * m12 - m02 * m11;
			const float determinant = m00 * c00 + m01 * c01 + m02 * c02;
			if (!(std::fabs(determinant) > 1e-12f))
			{
				return glm::vec3(0.0f);
			}
			const float c11 = m00 * m22 - m02 * m02;
			const float c12 = m01 * m02 - m00 * m12;
			const float c22 = m00 * m11 - m01 * m01;
			const float inverse = 1.0f / determinant;
			return { (c00 * b.x + c01 * b.y + c02 * b.z) * inverse, (c01 * b.x + c11 * b.y + c12 * b.z) * inverse,
				(c02 * b.x + c12 * b.y + c22 * b.z) * inverse };
		}

		// Whether the triangle faces along +axis (sign 1) or -axis (sign -1)
		bool FacesAlong(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, int axis, int sign)
		{
			return glm::cross(b - a, c - a)[axis] * float(sign) > 0.0f;
		}

		// A grid and its settings as both meshers read them: where its cells are, the field within it
		// and past it, and how much sky a point on the surface sees
		struct GridView
		{
			GridView(const FieldGrid& grid, const FieldMeshSettings& settings)
				: Grid(grid), Settings(settings), CellCounts(glm::max(grid.Size - 1, glm::ivec3(0)))
			{
			}

			bool IsCellInGrid(const glm::ivec3& cell) const
			{
				return glm::all(glm::greaterThanEqual(cell, glm::ivec3(0))) && glm::all(glm::lessThan(cell, CellCounts));
			}

			// Where a cell's first corner is in the grid's space: whole cells from the origin, the same bits
			// in every grid whose origin is whole cells from this one's
			glm::vec3 CellCorner(const glm::ivec3& cell) const
			{
				return Grid.Origin + glm::vec3(cell) * Grid.CellSize;
			}

			// The field at points in the grid's space, asked of the settings' field in one go
			void AskField(const std::vector<glm::vec3>& points, std::vector<float>& outValues) const
			{
				std::vector<float> x(points.size());
				std::vector<float> y(points.size());
				std::vector<float> z(points.size());
				for (size_t n = 0; n < points.size(); ++n)
				{
					x[n] = points[n].x;
					y[n] = points[n].y;
					z[n] = points[n].z;
				}
				outValues.resize(points.size());
				Settings.Field(x.data(), y.data(), z.data(), points.size(), outValues.data());
			}

			// The field at `local` from a cell's corner, in cells, if that is within the grid
			bool SampleGrid(const glm::ivec3& cell, const glm::vec3& local, float& outValue) const
			{
				const glm::vec3 whole = glm::floor(local);
				glm::ivec3 at = cell + glm::ivec3(whole);
				glm::vec3 within = local - whole;
				// The far faces of the grid belong to its last cells
				for (int axis = 0; axis < 3; ++axis)
				{
					if (at[axis] == CellCounts[axis] && within[axis] == 0.0f)
					{
						at[axis] -= 1;
						within[axis] = 1.0f;
					}
				}
				if (!IsCellInGrid(at))
				{
					return false;
				}
				outValue = ReadCell(Grid, at).At(within);
				return true;
			}

			// How much of the sky each vertex sees: rays over the sky's half of the directions, each
			// weighted by how squarely the vertex faces it, blocked where the field is inside
			void ComputeSkyVisibility(const std::vector<glm::ivec3>& cells, const std::vector<glm::vec3>& locals, const std::vector<glm::vec3>& normals,
			std::vector<float>& outVisibility) const
			{
				outVisibility.assign(cells.size(), 1.0f);
				if (!(Settings.SkyReach > 0.0f))
				{
					return;
				}

				// Straight up and six round it 45 degrees up, written out rather than from sine and cosine,
				// which a C library may round its own way
				const std::vector<glm::vec3> directions{ { 0.0f, 1.0f, 0.0f }, { 0.70710677f, 0.70710677f, 0.0f }, { 0.35355338f, 0.70710677f, 0.61237244f },
					{ -0.35355338f, 0.70710677f, 0.61237244f }, { -0.70710677f, 0.70710677f, 0.0f }, { -0.35355338f, 0.70710677f, -0.61237244f },
					{ 0.35355338f, 0.70710677f, -0.61237244f } };
				// A cell, then twice as far each time
				std::vector<float> distances;
				for (float distance = 1.0f; distance * Grid.CellSize <= Settings.SkyReach; distance *= 2.0f)
				{
					distances.push_back(distance);
				}

				std::vector<uint8_t> blocked(cells.size() * directions.size(), 0);
				// Points past the grid, asked of the field in one go
				std::vector<glm::vec3> farPoints;
				std::vector<size_t> farRays;
				for (size_t u = 0; u < cells.size(); ++u)
				{
					const glm::vec3 start = locals[u] + normals[u] * 0.5f;
					for (size_t d = 0; d < directions.size(); ++d)
					{
						if (!(glm::dot(directions[d], normals[u]) > 0.0f))
						{
							continue;
						}
						const size_t ray = u * directions.size() + d;
						for (const float distance : distances)
						{
							const glm::vec3 local = start + directions[d] * distance;
							float value = 0.0f;
							if (SampleGrid(cells[u], local, value))
							{
								if (IsInside(value))
								{
									blocked[ray] = 1;
									break;
								}
							}
							else if (Settings.Field)
							{
								farPoints.push_back(CellCorner(cells[u]) + local * Grid.CellSize);
								farRays.push_back(ray);
							}
						}
					}
				}
				if (!farPoints.empty())
				{
					std::vector<float> values;
					AskField(farPoints, values);
					for (size_t n = 0; n < farRays.size(); ++n)
					{
						if (IsInside(values[n]))
						{
							blocked[farRays[n]] = 1;
						}
					}
				}

				for (size_t u = 0; u < cells.size(); ++u)
				{
					float open = 0.0f;
					float total = 0.0f;
					for (size_t d = 0; d < directions.size(); ++d)
					{
						const float weight = std::max(glm::dot(directions[d], normals[u]), 0.0f);
						total += weight;
						open += blocked[u * directions.size() + d] != 0 ? 0.0f : weight;
					}
					outVisibility[u] = total > 0.0f ? open / total : 0.0f;
				}
			}

			const FieldGrid& Grid;
			const FieldMeshSettings& Settings;
			glm::ivec3 CellCounts{ 0 };
		};

		bool IsGridValid(const FieldGrid& grid)
		{
			const size_t samples = size_t(std::max(grid.Size.x, 0)) * size_t(std::max(grid.Size.y, 0)) * size_t(std::max(grid.Size.z, 0));
			return glm::all(glm::greaterThanEqual(grid.Size, glm::ivec3(2))) && grid.Values.size() == samples &&
				(grid.Materials.empty() || grid.Materials.size() == samples);
		}

		// A grid's cells as a mesher has them: the grid, what it reads of it, and each cell's record
		struct MeshLevel
		{
			MeshLevel(const FieldGrid& grid, const FieldMeshSettings& settings) : Grid(grid), View(grid, settings)
			{
				Cells.resize(size_t(View.CellCounts.x) * size_t(View.CellCounts.y) * size_t(View.CellCounts.z));
			}

			size_t CellIndex(const glm::ivec3& cell) const
			{
				return size_t(cell.x) + size_t(View.CellCounts.x) * (size_t(cell.y) + size_t(View.CellCounts.y) * size_t(cell.z));
			}

			const FieldGrid& Grid;
			GridView View;
			std::vector<CellRecord> Cells;
		};

		int FloorHalf(int value)
		{
			return value >= 0 ? value / 2 : -((1 - value) / 2);
		}

		class Mesher
		{
		public:
			Mesher(const FieldGrid& grid, const FieldMeshSettings& settings) : Grid(grid), Settings(settings), Fine(grid, settings), View(Fine.View)
			{
				CellCounts = glm::max(grid.Size - 1, glm::ivec3(0));
				bSeams = static_cast<bool>(settings.Seams.CellSizes);
				for (int axis = 0; axis < 3; ++axis)
				{
					if (bSeams)
					{
						// The box of samples, with a cell round it for the quads of the edges on its faces
						QuadMin[axis] = std::clamp(settings.QuadMin[axis], 1, std::max(grid.Size[axis] - 2, 1));
						QuadMax[axis] = std::clamp(settings.QuadMax[axis], QuadMin[axis], std::max(grid.Size[axis] - 2, 1));
						continue;
					}
					QuadMin[axis] = settings.QuadMin[axis] > 0 ? settings.QuadMin[axis] : 1;
					QuadMax[axis] = settings.QuadMax[axis] > 0 ? settings.QuadMax[axis] : grid.Size[axis] - 1;
					QuadMin[axis] = std::max(QuadMin[axis], 1);
					QuadMax[axis] = std::min(QuadMax[axis], grid.Size[axis] - 1);
				}
				if (bSeams && settings.Seams.Coarser != nullptr && IsGridValid(*settings.Seams.Coarser))
				{
					Coarse = std::make_unique<MeshLevel>(*settings.Seams.Coarser, settings);
					CoarseOffset = glm::ivec3(glm::round((settings.Seams.Coarser->Origin - grid.Origin) / grid.CellSize));
				}
			}

			MeshData Make(FieldMeshStats& stats)
			{
				MakeQuads();
				MakeSharpNeighbours();
				PlaceSharpVertices();
				UnfoldShared(stats);
				UnfoldPrivate(stats);
				return Finish(stats);
			}

		private:
			// Its vertices, in order round it: four, or three where two of its cells were one
			struct Quad
			{
				int32_t V[4];
				int Count;
				int Axis;
				int Sign;
			};

			MeshLevel& LevelOf(int level)
			{
				return level == 0 ? Fine : *Coarse;
			}

			const MeshLevel& LevelOf(int level) const
			{
				return level == 0 ? Fine : *Coarse;
			}

			// Where a vertex is in the grid's cells: a coarser one's cell twice the size
			glm::vec3 GridPosition(const WorkVertex& vertex) const
			{
				if (vertex.Level == 0)
				{
					return glm::vec3(vertex.Cell) + vertex.Local;
				}
				return (glm::vec3(vertex.Cell) + vertex.Local) * 2.0f + glm::vec3(CoarseOffset);
			}

			bool IsSharp(uint32_t material) const
			{
				return material < Settings.SharpMaterials.size() && Settings.SharpMaterials[material];
			}

			bool IsCellInBox(const glm::ivec3& cell) const
			{
				return glm::all(glm::greaterThanEqual(cell, QuadMin)) && glm::all(glm::lessThan(cell, QuadMax));
			}

			bool IsOnBoxFace(const glm::ivec3& sample) const
			{
				return glm::any(glm::equal(sample, QuadMin)) || glm::any(glm::equal(sample, QuadMax));
			}

			// The cells round the grid edge from `from` along `axis`, counter-clockwise about it, in a
			// level's grid; false if one is outside it
			bool GetCellsAround(const MeshLevel& level, const glm::ivec3& from, int axis, glm::ivec3 outCells[4], int outEdges[4]) const
			{
				const int b = (axis + 1) % 3;
				const int c = (axis + 2) % 3;
				for (int n = 0; n < 4; ++n)
				{
					glm::ivec3 cell = from;
					cell[b] += Around[n][0] - 1;
					cell[c] += Around[n][1] - 1;
					if (!level.View.IsCellInGrid(cell))
					{
						return false;
					}
					const int localCorner = ((1 - Around[n][0]) << b) | ((1 - Around[n][1]) << c);
					outCells[n] = cell;
					outEdges[n] = EdgeBetween(localCorner, localCorner | (1 << axis));
				}
				return true;
			}

			// Whether a face of a grid's cell, across `axis` at `sample`, lies where the cells beyond are
			// twice the size: its curves are then the coarser face's, cut in four, joined as its middle
			// decides
			bool IsCoarserFace(const glm::ivec3& sample, int axis) const
			{
				glm::vec3 middle = glm::vec3(sample) + 0.5f;
				middle[axis] = float(sample[axis]);
				return Settings.Seams.CellSizes(middle).y > 1.5f;
			}

			// The cell's pieces of surface and their vertices, made the first time they are needed
			CellRecord& GetCell(int levelIndex, const glm::ivec3& cell)
			{
				MeshLevel& level = LevelOf(levelIndex);
				CellRecord& record = level.Cells[level.CellIndex(cell)];
				if (record.bMade)
				{
					return record;
				}
				record.bMade = true;
				record.EdgePiece.fill(-1);

				const CellValues values = ReadCell(level.Grid, cell);
				bool bInside[8] = {};
				for (int corner = 0; corner < 8; ++corner)
				{
					bInside[corner] = IsInside(values.Corner[corner]);
				}
				// A cell of this grid not well inside the box may have faces on coarser cells
				const bool bNearSeams = bSeams && levelIndex == 0 &&
					!(glm::all(glm::greaterThan(cell, QuadMin)) && glm::all(glm::lessThan(cell, QuadMax - 1)));

				// Crossed edges joined into pieces through the faces: a face with two crossed edges
				// joins them; one with four joins each pair round a corner its middle cuts off
				std::array<int, EdgeCount> parents{};
				for (int edge = 0; edge < EdgeCount; ++edge)
				{
					parents[edge] = edge;
				}
				for (int faceIndex = 0; faceIndex < 6; ++faceIndex)
				{
					const CellFace& face = Faces[faceIndex];
					int crossed[4] = {};
					int count = 0;
					for (int k = 0; k < 4; ++k)
					{
						if (bInside[face.Corners[k]] != bInside[face.Corners[(k + 1) % 4]])
						{
							crossed[count++] = face.Edges[k];
						}
					}
					if (count == 2)
					{
						Join(parents, crossed[0], crossed[1]);
					}
					else if (count == 4)
					{
						float middle = values.Corner[face.Corners[0]] + values.Corner[face.Corners[1]] + values.Corner[face.Corners[2]] +
							values.Corner[face.Corners[3]];
						const int axis = faceIndex / 2;
						glm::ivec3 sample = cell;
						sample[axis] += faceIndex % 2;
						if (bNearSeams && IsCoarserFace(sample, axis))
						{
							// A quarter of a coarser face: its middle is the corner with odd indices
							for (int k = 0; k < 4; ++k)
							{
								const glm::ivec3 at = cell + CornerOffset(face.Corners[k]);
								if ((at[(axis + 1) % 3] & 1) != 0 && (at[(axis + 2) % 3] & 1) != 0)
								{
									middle = values.Corner[face.Corners[k]];
								}
							}
						}
						// Inside corners joined across the face: the outside ones are cut off, else
						// the inside ones
						const bool bCutOutside = IsInside(middle);
						for (int k = 0; k < 4; ++k)
						{
							if (bInside[face.Corners[k]] != bCutOutside)
							{
								Join(parents, face.Edges[(k + 3) % 4], face.Edges[k]);
							}
						}
					}
				}

				int pieceOfRoot[EdgeCount];
				std::fill(std::begin(pieceOfRoot), std::end(pieceOfRoot), -1);
				int pieces = 0;
				for (int edge = 0; edge < EdgeCount; ++edge)
				{
					if (bInside[Edges[edge].From] == bInside[Edges[edge].To])
					{
						continue;
					}
					const int root = FindRoot(parents, edge);
					if (pieceOfRoot[root] < 0)
					{
						pieceOfRoot[root] = pieces++;
					}
					record.EdgePiece[edge] = static_cast<int8_t>(pieceOfRoot[root]);
				}
				if (pieces == 0)
				{
					return record;
				}
				if (pieces > 1 && levelIndex == 0)
				{
					++SplitCells;
				}

				record.FirstVertex = static_cast<int32_t>(Vertices.size());
				for (int piece = 0; piece < pieces; ++piece)
				{
					Vertices.push_back(MakeVertex(levelIndex, cell, values, bInside, record, piece));
				}
				return record;
			}

			WorkVertex MakeVertex(int levelIndex, const glm::ivec3& cell, const CellValues& values, const bool bInside[8], const CellRecord& record, int piece)
			{
				const MeshLevel& level = LevelOf(levelIndex);
				glm::vec3 crossings[EdgeCount];
				glm::vec3 gradients[EdgeCount];
				Crossing ends[EdgeCount];
				uint32_t materials[EdgeCount];
				int count = 0;
				for (int edge = 0; edge < EdgeCount; ++edge)
				{
					if (record.EdgePiece[edge] != piece)
					{
						continue;
					}
					const CellEdge& cellEdge = Edges[edge];
					const float from = values.Corner[cellEdge.From];
					const float to = values.Corner[cellEdge.To];
					const float t = std::clamp(from / (from - to), 0.0f, 1.0f);
					glm::vec3 point = glm::vec3(CornerOffset(cellEdge.From));
					point[cellEdge.Axis] = t;
					crossings[count] = point;
					const glm::vec3 fromGradient = SampleGradient(level.Grid, cell + CornerOffset(cellEdge.From));
					const glm::vec3 toGradient = SampleGradient(level.Grid, cell + CornerOffset(cellEdge.To));
					gradients[count] = fromGradient + (toGradient - fromGradient) * t;

					const int insideCorner = bInside[cellEdge.From] ? cellEdge.From : cellEdge.To;
					const int outsideCorner = bInside[cellEdge.From] ? cellEdge.To : cellEdge.From;
					ends[count].Inside = level.View.CellCorner(cell + CornerOffset(insideCorner));
					ends[count].Outside = level.View.CellCorner(cell + CornerOffset(outsideCorner));
					ends[count].InsideValue = values.Corner[insideCorner];
					ends[count].OutsideValue = values.Corner[outsideCorner];
					const glm::ivec3 at = cell + CornerOffset(insideCorner);
					materials[count] = level.Grid.GetMaterial(at.x, at.y, at.z);
					++count;
				}

				glm::vec3 mass(0.0f);
				for (int k = 0; k < count; ++k)
				{
					mass += crossings[k];
				}
				mass /= float(count);

				// The most common material of the inside corners, the lowest number on a tie
				uint32_t material = materials[0];
				int best = 0;
				for (int k = 0; k < count; ++k)
				{
					int same = 0;
					for (int m = 0; m < count; ++m)
					{
						same += materials[m] == materials[k] ? 1 : 0;
					}
					if (same > best || (same == best && materials[k] < material))
					{
						best = same;
						material = materials[k];
					}
				}

				WorkVertex vertex;
				vertex.Cell = cell;
				vertex.Level = static_cast<uint8_t>(levelIndex);
				vertex.Material = material;
				vertex.MassLocal = mass;
				vertex.Local = mass;
				vertex.bSharp = IsSharp(material);
				if (vertex.bSharp)
				{
					vertex.FirstCrossing = static_cast<uint32_t>(SharpCrossings.size());
					vertex.CrossingCount = static_cast<uint32_t>(count);
					for (int k = 0; k < count; ++k)
					{
						Crossing crossing = ends[k];
						crossing.Point = level.View.CellCorner(cell) + crossings[k] * level.Grid.CellSize;
						crossing.Gradient = gradients[k];
						crossing.Step = 0.05f * level.Grid.CellSize;
						SharpCrossings.push_back(crossing);
					}
				}
				return vertex;
			}

			// The piece of a coarser cell that the crossing of a finer edge on its face belongs to: that
			// of the coarser edge it is half of, or, for one across the face's middle, that of the
			// coarser edge its curve over the face, cut in four, reaches; -1 if none
			int GetCoarsePiece(const CellRecord& record, const glm::ivec3& coarseCell, const glm::ivec3& from, int axis) const
			{
				const glm::ivec3 relative = from - CoarseOffset;
				const int b = (axis + 1) % 3;
				const int c = (axis + 2) % 3;
				const bool bOddB = (relative[b] & 1) != 0;
				const bool bOddC = (relative[c] & 1) != 0;
				if (!bOddB && !bOddC)
				{
					glm::ivec3 start;
					start[axis] = FloorHalf(relative[axis]);
					start[b] = relative[b] / 2;
					start[c] = relative[c] / 2;
					const glm::ivec3 corner = start - coarseCell;
					if (corner[axis] != 0 || glm::any(glm::lessThan(corner, glm::ivec3(0))) || glm::any(glm::greaterThan(corner, glm::ivec3(1))))
					{
						return -1;
					}
					const int bits = corner.x | (corner.y << 1) | (corner.z << 2);
					return record.EdgePiece[EdgeBetween(bits, bits | (1 << axis))];
				}
				if (bOddB && bOddC)
				{
					return -1;
				}

				// Across the face whose normal is `normal`, in the cell's middle along `across`
				const int across = bOddB ? b : c;
				const int normal = bOddB ? c : b;
				const int side = relative[normal] / 2 - coarseCell[normal];
				if (side < 0 || side > 1)
				{
					return -1;
				}
				// The face's samples as the finer grid has them, [along axis][along across]
				float v[3][3];
				glm::ivec3 base;
				base[axis] = coarseCell[axis] * 2 + CoarseOffset[axis];
				base[across] = coarseCell[across] * 2 + CoarseOffset[across];
				base[normal] = from[normal];
				for (int i = 0; i < 3; ++i)
				{
					for (int j = 0; j < 3; ++j)
					{
						glm::ivec3 at = base;
						at[axis] += i;
						at[across] += j;
						if (glm::any(glm::lessThan(at, glm::ivec3(0))) || glm::any(glm::greaterThanEqual(at, Grid.Size)))
						{
							return -1;
						}
						v[i][j] = Grid.GetValue(at.x, at.y, at.z);
					}
				}
				// Its twelve finer edges: along `axis` (i, j) as i * 3 + j, along `across` (i, j) as 6 + i * 2 + j
				std::array<int, 12> parents{};
				for (int n = 0; n < 12; ++n)
				{
					parents[n] = n;
				}
				auto findRoot = [&](int n)
				{
					while (parents[n] != n)
					{
						parents[n] = parents[parents[n]];
						n = parents[n];
					}
					return n;
				};
				auto join = [&](int p, int q)
				{
					const int rootP = findRoot(p);
					const int rootQ = findRoot(q);
					if (rootP != rootQ)
					{
						parents[std::max(rootP, rootQ)] = std::min(rootP, rootQ);
					}
				};
				for (int i = 0; i < 2; ++i)
				{
					for (int j = 0; j < 2; ++j)
					{
						const float corners[4] = { v[i][j], v[i + 1][j], v[i + 1][j + 1], v[i][j + 1] };
						const int edges[4] = { i * 3 + j, 6 + (i + 1) * 2 + j, i * 3 + j + 1, 6 + i * 2 + j };
						int crossed[4] = {};
						int count = 0;
						for (int k = 0; k < 4; ++k)
						{
							if (IsInside(corners[k]) != IsInside(corners[(k + 1) % 4]))
							{
								crossed[count++] = edges[k];
							}
						}
						if (count == 2)
						{
							join(crossed[0], crossed[1]);
						}
						else if (count == 4)
						{
							const bool bCutOutside = IsInside(v[1][1]);
							for (int k = 0; k < 4; ++k)
							{
								if (IsInside(corners[k]) != bCutOutside)
								{
									join(edges[(k + 3) % 4], edges[k]);
								}
							}
						}
					}
				}
				const int root = findRoot((from[axis] - base[axis]) * 3 + 1);
				// The coarser edges round the face, as finer edges on them
				for (int n = 0; n < 12; ++n)
				{
					const bool bAlongAxis = n < 6;
					const int i = bAlongAxis ? n / 3 : (n - 6) / 2;
					const int j = bAlongAxis ? n % 3 : (n - 6) % 2;
					if ((bAlongAxis ? j : i) == 1 || findRoot(n) != root)
					{
						continue;
					}
					const bool bCrossed = bAlongAxis ? IsInside(v[i][j]) != IsInside(v[i + 1][j]) : IsInside(v[i][j]) != IsInside(v[i][j + 1]);
					if (!bCrossed)
					{
						continue;
					}
					const int bits = (side << normal) | ((bAlongAxis ? 0 : i / 2) << axis) | ((bAlongAxis ? j / 2 : 0) << across);
					const int piece = record.EdgePiece[EdgeBetween(bits, bits | (1 << (bAlongAxis ? axis : across)))];
					if (piece >= 0)
					{
						return piece;
					}
				}
				return -1;
			}

			void MakeQuads()
			{
				// With seams, the edges of the box of samples, those on its faces as the seams say
				const glm::ivec3 last = bSeams ? QuadMax : QuadMax - 1;
				for (int axis = 0; axis < 3; ++axis)
				{
					for (int k = QuadMin.z; k <= last.z; ++k)
					{
						for (int j = QuadMin.y; j <= last.y; ++j)
						{
							for (int i = QuadMin.x; i <= last.x; ++i)
							{
								const glm::ivec3 from(i, j, k);
								glm::ivec3 to = from;
								to[axis] += 1;
								if (to[axis] > (bSeams ? QuadMax[axis] : Grid.Size[axis] - 1))
								{
									continue;
								}
								const bool bFromInside = IsInside(Grid.GetValue(from.x, from.y, from.z));
								const bool bToInside = IsInside(Grid.GetValue(to.x, to.y, to.z));
								if (bFromInside == bToInside)
								{
									continue;
								}
								if (bSeams && (IsOnBoxFace(from) || IsOnBoxFace(to)) && Settings.Seams.MakesEdge && !Settings.Seams.MakesEdge(from, axis))
								{
									continue;
								}

								glm::ivec3 cells[4];
								int edges[4];
								if (!GetCellsAround(Fine, from, axis, cells, edges))
								{
									continue;
								}
								int32_t quad[4] = {};
								bool bComplete = true;
								for (int n = 0; n < 4 && bComplete; ++n)
								{
									// Beyond the box where the cells are twice the size: the coarser cell
									if (bSeams && !IsCellInBox(cells[n]) && Settings.Seams.CellSizes(glm::vec3(cells[n]) + 0.5f).y > 1.5f)
									{
										const glm::ivec3 coarseCell(FloorHalf(cells[n].x - CoarseOffset.x), FloorHalf(cells[n].y - CoarseOffset.y),
											FloorHalf(cells[n].z - CoarseOffset.z));
										if (!Coarse || !Coarse->View.IsCellInGrid(coarseCell))
										{
											bComplete = false;
											break;
										}
										const CellRecord& record = GetCell(1, coarseCell);
										const int piece = GetCoarsePiece(record, coarseCell, from, axis);
										bComplete = piece >= 0 && record.FirstVertex >= 0;
										quad[n] = bComplete ? record.FirstVertex + piece : 0;
										continue;
									}
									const CellRecord& record = GetCell(0, cells[n]);
									quad[n] = record.FirstVertex + record.EdgePiece[edges[n]];
								}
								if (!bComplete)
								{
									continue;
								}
								// Facing out: towards the outside end of the edge
								if (!bFromInside)
								{
									std::swap(quad[1], quad[3]);
								}
								// Two of its cells one coarser cell: a triangle
								Quad made{ {}, 0, axis, bFromInside ? 1 : -1 };
								for (int n = 0; n < 4; ++n)
								{
									if (quad[n] != quad[(n + 1) % 4])
									{
										made.V[made.Count++] = quad[n];
									}
								}
								if (made.Count < 3)
								{
									continue;
								}
								for (int n = 0; n < made.Count; ++n)
								{
									Vertices[made.V[n]].bUsed = true;
								}
								Quads.push_back(made);
							}
						}
					}
				}
			}

			// The cells round every crossed edge of a sharp vertex's cell, so its check against its
			// neighbours (UnfoldShared()) has them
			void MakeSharpNeighbours()
			{
				const size_t count = Vertices.size();
				for (size_t v = 0; v < count; ++v)
				{
					if (!Vertices[v].bSharp || !Vertices[v].bUsed)
					{
						continue;
					}
					const int levelIndex = Vertices[v].Level;
					const glm::ivec3 cell = Vertices[v].Cell;
					const std::array<int8_t, EdgeCount> pieces = LevelOf(levelIndex).Cells[LevelOf(levelIndex).CellIndex(cell)].EdgePiece;
					for (int edge = 0; edge < EdgeCount; ++edge)
					{
						if (pieces[edge] < 0)
						{
							continue;
						}
						glm::ivec3 cells[4];
						int edges[4];
						if (GetCellsAround(LevelOf(levelIndex), cell + CornerOffset(Edges[edge].From), Edges[edge].Axis, cells, edges))
						{
							for (const glm::ivec3& around : cells)
							{
								GetCell(levelIndex, around);
							}
						}
					}
				}
			}

			// Each sharp crossing found on the field itself, along its edge: false position, its
			// bracket kept between an inside and an outside point, two steps for all of them at once
			void RefineCrossings()
			{
				std::vector<glm::vec3> points(SharpCrossings.size());
				std::vector<float> values;
				for (int step = 0; step < 2; ++step)
				{
					for (size_t n = 0; n < SharpCrossings.size(); ++n)
					{
						const Crossing& crossing = SharpCrossings[n];
						const float t = std::clamp(crossing.InsideValue / (crossing.InsideValue - crossing.OutsideValue), 0.0f, 1.0f);
						points[n] = crossing.Inside + (crossing.Outside - crossing.Inside) * t;
					}
					View.AskField(points, values);
					for (size_t n = 0; n < SharpCrossings.size(); ++n)
					{
						Crossing& crossing = SharpCrossings[n];
						crossing.Point = points[n];
						crossing.PointValue = values[n];
						if (IsInside(values[n]))
						{
							crossing.Inside = points[n];
							crossing.InsideValue = values[n];
						}
						else
						{
							crossing.Outside = points[n];
							crossing.OutsideValue = values[n];
						}
					}
				}
			}

			// Each sharp vertex where the planes of its crossings meet, drawn towards their middle by
			// the bias, solved about the middle, then kept in its cell. Where the settings give the
			// field itself, the crossings are found on it and the planes' normals are its gradient
			// there, by differences a twentieth of a cell along each axis from the crossing, all asked
			// for at once.
			void PlaceSharpVertices()
			{
				if (SharpCrossings.empty())
				{
					return;
				}
				if (Settings.Field)
				{
					RefineCrossings();
					std::vector<glm::vec3> points;
					points.reserve(SharpCrossings.size() * 3);
					for (const Crossing& crossing : SharpCrossings)
					{
						for (int axis = 0; axis < 3; ++axis)
						{
							glm::vec3 point = crossing.Point;
							point[axis] += crossing.Step;
							points.push_back(point);
						}
					}
					std::vector<float> values;
					View.AskField(points, values);
					for (size_t n = 0; n < SharpCrossings.size(); ++n)
					{
						const float* at = values.data() + n * 3;
						const float here = SharpCrossings[n].PointValue;
						SharpCrossings[n].Gradient = glm::vec3(at[0] - here, at[1] - here, at[2] - here);
					}
				}

				for (WorkVertex& vertex : Vertices)
				{
					if (!vertex.bSharp)
					{
						continue;
					}
					const MeshLevel& level = LevelOf(vertex.Level);
					const glm::vec3 corner = level.View.CellCorner(vertex.Cell);
					const glm::vec3 mass = vertex.MassLocal;
					float a[6] = {};
					glm::vec3 b(0.0f);
					for (uint32_t k = 0; k < vertex.CrossingCount; ++k)
					{
						const Crossing& crossing = SharpCrossings[vertex.FirstCrossing + k];
						const float length = glm::length(crossing.Gradient);
						if (!(length > 0.0f))
						{
							continue;
						}
						const glm::vec3 n = crossing.Gradient / length;
						const glm::vec3 local = (crossing.Point - corner) / level.Grid.CellSize;
						a[0] += n.x * n.x;
						a[1] += n.x * n.y;
						a[2] += n.x * n.z;
						a[3] += n.y * n.y;
						a[4] += n.y * n.z;
						a[5] += n.z * n.z;
						b += n * glm::dot(n, local - mass);
					}
					const float bias = Settings.SharpBias * float(vertex.CrossingCount);
					a[0] += bias;
					a[3] += bias;
					a[5] += bias;
					vertex.Local = glm::clamp(mass + SolveSymmetric(a, b), glm::vec3(0.0f), glm::vec3(1.0f));
				}
			}

			// A sharp vertex whose quads would face the wrong way goes back to its middle. Judged from its
			// own cell and its neighbours alone (they at their middles), in its own grid, so every mesh
			// that has a cell decides the same for it.
			void UnfoldShared(FieldMeshStats& stats)
			{
				for (size_t v = 0; v < Vertices.size(); ++v)
				{
					WorkVertex& vertex = Vertices[v];
					if (!vertex.bSharp || !vertex.bUsed || vertex.Local == vertex.MassLocal)
					{
						continue;
					}
					const MeshLevel& level = LevelOf(vertex.Level);
					const CellRecord& own = level.Cells[level.CellIndex(vertex.Cell)];
					const int piece = static_cast<int>(v) - own.FirstVertex;
					bool bFolds = false;
					for (int edge = 0; edge < EdgeCount && !bFolds; ++edge)
					{
						if (own.EdgePiece[edge] != piece)
						{
							continue;
						}
						const glm::ivec3 from = vertex.Cell + CornerOffset(Edges[edge].From);
						const int axis = Edges[edge].Axis;
						glm::ivec3 cells[4];
						int edges[4];
						if (!GetCellsAround(level, from, axis, cells, edges))
						{
							continue;
						}
						glm::vec3 corners[4];
						for (int n = 0; n < 4; ++n)
						{
							if (cells[n] == vertex.Cell)
							{
								corners[n] = vertex.Local;
								continue;
							}
							const CellRecord& record = level.Cells[level.CellIndex(cells[n])];
							const WorkVertex& other = Vertices[record.FirstVertex + record.EdgePiece[edges[n]]];
							corners[n] = glm::vec3(cells[n] - vertex.Cell) + other.MassLocal;
						}
						const bool bFromInside = IsInside(level.Grid.GetValue(from.x, from.y, from.z));
						if (!bFromInside)
						{
							std::swap(corners[1], corners[3]);
						}
						const int sign = bFromInside ? 1 : -1;
						bFolds = !FacesAlong(corners[0], corners[1], corners[2], axis, sign) || !FacesAlong(corners[0], corners[2], corners[3], axis, sign) ||
							!FacesAlong(corners[0], corners[1], corners[3], axis, sign) || !FacesAlong(corners[1], corners[2], corners[3], axis, sign);
					}
					if (bFolds)
					{
						vertex.Local = vertex.MassLocal;
						++stats.UnfoldedVertices;
					}
				}
			}

			// Whether no other mesh has the cell: its quads, and the cells they join, are all this mesh's
			bool IsPrivateCell(const WorkVertex& vertex) const
			{
				const glm::ivec3& cell = vertex.Cell;
				if (vertex.Level != 0)
				{
					return false;
				}
				if (!bSeams)
				{
					return glm::all(glm::greaterThanEqual(cell, QuadMin)) && glm::all(glm::lessThan(cell, QuadMax - 1));
				}
				// Every one of its twelve edges made here
				if (!IsCellInBox(cell))
				{
					return false;
				}
				for (const CellEdge& edge : Edges)
				{
					const glm::ivec3 from = cell + CornerOffset(edge.From);
					glm::ivec3 to = from;
					to[edge.Axis] += 1;
					if ((IsOnBoxFace(from) || IsOnBoxFace(to)) && Settings.Seams.MakesEdge && !Settings.Seams.MakesEdge(from, edge.Axis))
					{
						return false;
					}
				}
				return true;
			}

			// The quad's triangles: a triangle as it is; a quad split along the diagonal whose triangles
			// both face out, or along the shorter one if both or neither do
			int Triangulate(const Quad& quad, int32_t outTriangles[2][3]) const
			{
				if (quad.Count == 3)
				{
					std::copy(quad.V, quad.V + 3, outTriangles[0]);
					return 1;
				}
				const glm::vec3 p0 = GridPosition(Vertices[quad.V[0]]);
				const glm::vec3 p1 = GridPosition(Vertices[quad.V[1]]);
				const glm::vec3 p2 = GridPosition(Vertices[quad.V[2]]);
				const glm::vec3 p3 = GridPosition(Vertices[quad.V[3]]);
				const bool bFirstFacesOut = FacesAlong(p0, p1, p2, quad.Axis, quad.Sign) && FacesAlong(p0, p2, p3, quad.Axis, quad.Sign);
				const bool bSecondFacesOut = FacesAlong(p0, p1, p3, quad.Axis, quad.Sign) && FacesAlong(p1, p2, p3, quad.Axis, quad.Sign);
				const glm::vec3 d02 = p2 - p0;
				const glm::vec3 d13 = p3 - p1;
				const bool bFirst = bFirstFacesOut != bSecondFacesOut ? bFirstFacesOut : glm::dot(d02, d02) <= glm::dot(d13, d13);
				if (bFirst)
				{
					const int32_t triangles[2][3] = { { quad.V[0], quad.V[1], quad.V[2] }, { quad.V[0], quad.V[2], quad.V[3] } };
					std::copy(&triangles[0][0], &triangles[0][0] + 6, &outTriangles[0][0]);
				}
				else
				{
					const int32_t triangles[2][3] = { { quad.V[0], quad.V[1], quad.V[3] }, { quad.V[1], quad.V[2], quad.V[3] } };
					std::copy(&triangles[0][0], &triangles[0][0] + 6, &outTriangles[0][0]);
				}
				return 2;
			}

			// The quad's triangles for the mesh: a triangle as it is; a quad split along the diagonal
			// whose triangles agree best with the field's normals at their corners (the worst of the
			// two, as a cosine), the shorter one on a tie
			int TriangulateByNormals(const Quad& quad, const std::vector<uint32_t>& remap, const std::vector<glm::vec3>& normals, int32_t outTriangles[2][3]) const
			{
				if (quad.Count == 3)
				{
					std::copy(quad.V, quad.V + 3, outTriangles[0]);
					return 1;
				}
				auto agreement = [&](int32_t a, int32_t b, int32_t c)
				{
					const glm::vec3 pa = GridPosition(Vertices[a]);
					const glm::vec3 facing = glm::cross(GridPosition(Vertices[b]) - pa, GridPosition(Vertices[c]) - pa);
					const glm::vec3 normal = normals[remap[a]] + normals[remap[b]] + normals[remap[c]];
					const float lengths = glm::length(facing) * glm::length(normal);
					return lengths > 0.0f ? glm::dot(facing, normal) / lengths : 0.0f;
				};
				const int32_t* v = quad.V;
				const float first = std::min(agreement(v[0], v[1], v[2]), agreement(v[0], v[2], v[3]));
				const float second = std::min(agreement(v[0], v[1], v[3]), agreement(v[1], v[2], v[3]));
				const glm::vec3 d02 = GridPosition(Vertices[v[2]]) - GridPosition(Vertices[v[0]]);
				const glm::vec3 d13 = GridPosition(Vertices[v[3]]) - GridPosition(Vertices[v[1]]);
				const bool bFirst = first != second ? first > second : glm::dot(d02, d02) <= glm::dot(d13, d13);
				const int32_t triangles[2][2][3] = { { { v[0], v[1], v[2] }, { v[0], v[2], v[3] } }, { { v[0], v[1], v[3] }, { v[1], v[2], v[3] } } };
				std::copy(&triangles[bFirst ? 0 : 1][0][0], &triangles[bFirst ? 0 : 1][0][0] + 6, &outTriangles[0][0]);
				return 2;
			}

			// Then, as the triangles really are: a sharp vertex of a cell no other mesh has whose
			// triangles face the wrong way goes back to its middle, until none does
			void UnfoldPrivate(FieldMeshStats& stats)
			{
				for (int pass = 0; pass < 4; ++pass)
				{
					bool bMoved = false;
					for (const Quad& quad : Quads)
					{
						int32_t triangles[2][3];
						const int count = Triangulate(quad, triangles);
						for (int t = 0; t < count; ++t)
						{
							const int32_t* triangle = triangles[t];
							if (FacesAlong(GridPosition(Vertices[triangle[0]]), GridPosition(Vertices[triangle[1]]), GridPosition(Vertices[triangle[2]]),
									quad.Axis, quad.Sign))
							{
								continue;
							}
							for (int n = 0; n < 3; ++n)
							{
								WorkVertex& vertex = Vertices[triangle[n]];
								if (vertex.bSharp && vertex.Local != vertex.MassLocal && IsPrivateCell(vertex))
								{
									vertex.Local = vertex.MassLocal;
									++stats.UnfoldedVertices;
									bMoved = true;
								}
							}
						}
					}
					if (!bMoved)
					{
						break;
					}
				}
			}

			// The corners' gradients blended trilinearly at a point of the cell
			static glm::vec3 BlendGradients(const FieldGrid& grid, const glm::ivec3& cell, const glm::vec3& local)
			{
				glm::vec3 corners[8];
				for (int corner = 0; corner < 8; ++corner)
				{
					corners[corner] = SampleGradient(grid, cell + CornerOffset(corner));
				}
				const glm::vec3 x0 = corners[0] + (corners[1] - corners[0]) * local.x;
				const glm::vec3 x1 = corners[2] + (corners[3] - corners[2]) * local.x;
				const glm::vec3 x2 = corners[4] + (corners[5] - corners[4]) * local.x;
				const glm::vec3 x3 = corners[6] + (corners[7] - corners[6]) * local.x;
				const glm::vec3 y0 = x0 + (x1 - x0) * local.y;
				const glm::vec3 y1 = x2 + (x3 - x2) * local.y;
				return y0 + (y1 - y0) * local.z;
			}

			MeshData Finish(FieldMeshStats& stats)
			{
				// Only the vertices a quad joins, in the order they were made
				std::vector<size_t> used;
				std::vector<uint32_t> remap(Vertices.size(), 0);
				for (size_t v = 0; v < Vertices.size(); ++v)
				{
					if (Vertices[v].bUsed)
					{
						remap[v] = static_cast<uint32_t>(used.size());
						used.push_back(v);
					}
				}

				std::vector<glm::vec3> normals(used.size());
				for (size_t u = 0; u < used.size(); ++u)
				{
					const WorkVertex& vertex = Vertices[used[u]];
					const glm::vec3 gradient = BlendGradients(LevelOf(vertex.Level).Grid, vertex.Cell, vertex.Local);
					const float length = glm::length(gradient);
					normals[u] = length > 0.0f ? gradient / length : glm::vec3(0.0f, 1.0f, 0.0f);
				}
				// Each grid's vertices' sky through that grid
				std::vector<float> sky(used.size(), 1.0f);
				for (int levelIndex = 0; levelIndex < (Coarse ? 2 : 1); ++levelIndex)
				{
					std::vector<size_t> ofLevel;
					std::vector<glm::ivec3> cells;
					std::vector<glm::vec3> locals;
					std::vector<glm::vec3> levelNormals;
					for (size_t u = 0; u < used.size(); ++u)
					{
						if (Vertices[used[u]].Level == levelIndex)
						{
							ofLevel.push_back(u);
							cells.push_back(Vertices[used[u]].Cell);
							locals.push_back(Vertices[used[u]].Local);
							levelNormals.push_back(normals[u]);
						}
					}
					std::vector<float> levelSky;
					LevelOf(levelIndex).View.ComputeSkyVisibility(cells, locals, levelNormals, levelSky);
					for (size_t n = 0; n < ofLevel.size(); ++n)
					{
						sky[ofLevel[n]] = levelSky[n];
					}
				}

				MeshData mesh;
				mesh.Vertices.resize(used.size());
				for (size_t u = 0; u < used.size(); ++u)
				{
					const WorkVertex& work = Vertices[used[u]];
					const MeshLevel& level = LevelOf(work.Level);
					Vertex& vertex = mesh.Vertices[u];
					vertex.Position = level.View.CellCorner(work.Cell) + work.Local * level.Grid.CellSize;
					vertex.Normal = normals[u];
					const glm::vec3 side = std::fabs(normals[u].z) < 0.9f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
					vertex.Tangent = glm::vec4(glm::normalize(glm::cross(side, normals[u])), 1.0f);
					vertex.Custom = glm::vec4(sky[u], float(work.Material), work.bSharp ? 1.0f : 0.0f, work.Level != 0 ? 1.0f : 0.0f);
					stats.SharpVertices += work.bSharp ? 1u : 0u;
				}

				mesh.Indices.reserve(Quads.size() * 6);
				for (const Quad& quad : Quads)
				{
					int32_t triangles[2][3];
					const int count = TriangulateByNormals(quad, remap, normals, triangles);
					bool bSeam = false;
					for (int n = 0; n < quad.Count; ++n)
					{
						bSeam = bSeam || Vertices[quad.V[n]].Level != 0;
					}
					for (int t = 0; t < count; ++t)
					{
						const int32_t* triangle = triangles[t];
						// Against the field's own normals at its corners: what would show as a triangle
						// turned inside out
						const glm::vec3 a = GridPosition(Vertices[triangle[0]]);
						const glm::vec3 facing = glm::cross(GridPosition(Vertices[triangle[1]]) - a, GridPosition(Vertices[triangle[2]]) - a);
						const glm::vec3 normal = normals[remap[triangle[0]]] + normals[remap[triangle[1]]] + normals[remap[triangle[2]]];
						if (glm::dot(facing, normal) < 0.0f)
						{
							++stats.BackFacingTriangles;
						}
						stats.SeamTriangles += bSeam ? 1u : 0u;
						for (int n = 0; n < 3; ++n)
						{
							mesh.Indices.push_back(remap[triangle[n]]);
						}
					}
				}

				stats.Vertices = static_cast<uint32_t>(mesh.Vertices.size());
				stats.Triangles = static_cast<uint32_t>(mesh.Indices.size() / 3);
				stats.SplitCells = SplitCells;
				return mesh;
			}

			const FieldGrid& Grid;
			const FieldMeshSettings& Settings;
			MeshLevel Fine;
			// Where coarser cells are, theirs (FieldSeams::Coarser), their samples' indices less
			// CoarseOffset twice the grid's
			std::unique_ptr<MeshLevel> Coarse;
			glm::ivec3 CoarseOffset{ 0 };
			const GridView& View;
			glm::ivec3 CellCounts{ 0 };
			bool bSeams = false;
			glm::ivec3 QuadMin{ 1 };
			glm::ivec3 QuadMax{ 1 };
			std::vector<WorkVertex> Vertices;
			std::vector<Crossing> SharpCrossings;
			std::vector<Quad> Quads;
			uint32_t SplitCells = 0;
		};

		// Marching cubes: the surface's vertices on the crossed grid edges, shared by every cell round
		// an edge, and in each cell the loops of its crossed edges, joined through its faces as the dual
		// mesher joins them, as polygons facing out, fanned into triangles
		class MarchingMesher
		{
		public:
			MarchingMesher(const FieldGrid& grid, const FieldMeshSettings& settings) : Grid(grid), Settings(settings), View(grid, settings)
			{
				for (int axis = 0; axis < 3; ++axis)
				{
					CellMin[axis] = std::max(settings.QuadMin[axis] > 0 ? settings.QuadMin[axis] : 0, 0);
					CellMax[axis] = std::min(settings.QuadMax[axis] > 0 ? settings.QuadMax[axis] : View.CellCounts[axis], View.CellCounts[axis]);
				}
				bSeams = static_cast<bool>(settings.Seams.CellSizes);
				EdgeVertices.assign(grid.Values.size() * 3, -1);
			}

			MeshData Make(FieldMeshStats& stats)
			{
				for (int k = CellMin.z; k < CellMax.z; ++k)
				{
					for (int j = CellMin.y; j < CellMax.y; ++j)
					{
						for (int i = CellMin.x; i < CellMax.x; ++i)
						{
							MakeCell(glm::ivec3(i, j, k));
						}
					}
				}
				return Finish(stats);
			}

		private:
			struct EdgeVertex
			{
				// The edge's start, and where along it the surface crosses, in cells
				glm::ivec3 Cell{ 0 };
				glm::vec3 Local{ 0.0f };
				glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };
				uint32_t Material = 0;
				// On an edge of finer cells, made where this mesh's cells meet them: where it is, worked
				// out as the finer mesh does (Cell and Local only near it)
				bool bExact = false;
				glm::vec3 Exact{ 0.0f };
				// On a face or edge shared with cells of another size: its normal from the field, this
				// far either side, as the other mesh has it
				bool bSeam = false;
				float NormalStep = 0.0f;
			};

			// Whether a sample is on a face of the box of samples [CellMin, CellMax]
			bool IsOnBoxFace(const glm::ivec3& sample) const
			{
				return glm::any(glm::equal(sample, CellMin)) || glm::any(glm::equal(sample, CellMax));
			}

			static glm::vec3 FaceMiddle(const glm::ivec3& sample, int axis)
			{
				glm::vec3 middle = glm::vec3(sample) + 0.5f;
				middle[axis] = float(sample[axis]);
				return middle;
			}

			static glm::vec3 EdgeMiddle(const glm::ivec3& from, int axis)
			{
				glm::vec3 middle(from);
				middle[axis] += 0.5f;
				return middle;
			}

			// The vertex on the grid edge from `from` along `axis`, made the first time a cell needs it
			int32_t GetEdgeVertex(const glm::ivec3& from, int axis)
			{
				int32_t& index = EdgeVertices[Grid.IndexOf(from.x, from.y, from.z) * 3 + size_t(axis)];
				if (index >= 0)
				{
					return index;
				}
				glm::ivec3 to = from;
				to[axis] += 1;
				const float fromValue = Grid.GetValue(from.x, from.y, from.z);
				const float toValue = Grid.GetValue(to.x, to.y, to.z);
				// Kept a thousandth of a cell off the ends: a sample exactly at 0 would otherwise put the
				// vertices of all its crossed edges at one point
				const float t = std::clamp(fromValue / (fromValue - toValue), 0.001f, 0.999f);

				EdgeVertex vertex;
				vertex.Cell = from;
				vertex.Local[axis] = t;
				const glm::vec3 fromGradient = SampleGradient(Grid, from);
				const glm::vec3 toGradient = SampleGradient(Grid, to);
				const glm::vec3 gradient = fromGradient + (toGradient - fromGradient) * t;
				const float length = glm::length(gradient);
				vertex.Normal = length > 0.0f ? gradient / length : glm::vec3(0.0f, 1.0f, 0.0f);
				const glm::ivec3 inside = IsInside(fromValue) ? from : to;
				vertex.Material = Grid.GetMaterial(inside.x, inside.y, inside.z);
				// An edge on a face shared with coarser cells: theirs cut in two, as they have it
				if (bSeams && (IsOnBoxFace(from) || IsOnBoxFace(to)) && Settings.Seams.CellSizes(EdgeMiddle(from, axis)).y > 1.5f)
				{
					vertex.bSeam = true;
					vertex.NormalStep = 0.05f * Grid.CellSize;
				}

				index = static_cast<int32_t>(Vertices.size());
				Vertices.push_back(vertex);
				return index;
			}

			// The faces of a cell an edge of it lies on, as bits of the faces' numbers (CellFace)
			static int EdgeFaceMask(int edge)
			{
				int mask = 0;
				for (int axis = 0; axis < 3; ++axis)
				{
					if (axis != Edges[edge].Axis)
					{
						mask |= 1 << (axis * 2 + ((Edges[edge].From >> axis) & 1));
					}
				}
				return mask;
			}

			// A loop's polygon as a fan from a vertex none of whose diagonals lies on a face of the cell
			// (`faceMasks`, the faces each of the loop's edges lies on): the cell across that face could
			// make the same diagonal, and four triangles would share it. Where no vertex will do (a loop
			// through a face twice), a fan round a vertex in its middle.
			void AddPolygon(const glm::ivec3& cell, const int faceMasks[], const int32_t loop[], int length)
			{
				for (int first = 0; first < length; ++first)
				{
					bool bFits = true;
					for (int k = 2; k + 1 < length && bFits; ++k)
					{
						bFits = (faceMasks[first] & faceMasks[(first + k) % length]) == 0;
					}
					if (!bFits)
					{
						continue;
					}
					for (int n = 1; n + 1 < length; ++n)
					{
						Triangles.push_back({ loop[first], loop[(first + n) % length], loop[(first + n + 1) % length] });
					}
					return;
				}

				EdgeVertex middle;
				middle.Cell = cell;
				middle.Local = glm::vec3(0.0f);
				glm::vec3 normal(0.0f);
				bool bSeam = false;
				for (int n = 0; n < length; ++n)
				{
					const EdgeVertex& vertex = Vertices[loop[n]];
					middle.Local += glm::vec3(vertex.Cell - cell) + vertex.Local;
					normal += vertex.Normal;
					bSeam = bSeam || vertex.bExact;
				}
				middle.Local /= float(length);
				const float normalLength = glm::length(normal);
				middle.Normal = normalLength > 0.0f ? normal / normalLength : glm::vec3(0.0f, 1.0f, 0.0f);
				middle.Material = Vertices[loop[0]].Material;
				const int32_t centre = static_cast<int32_t>(Vertices.size());
				Vertices.push_back(middle);
				for (int n = 0; n < length; ++n)
				{
					Triangles.push_back({ centre, loop[n], loop[(n + 1) % length] });
				}
				if (bSeam)
				{
					SeamCentres.push_back(centre);
				}
			}

			void MakeCell(const glm::ivec3& cell)
			{
				const CellValues values = ReadCell(Grid, cell);
				bool bInside[8] = {};
				int insideCount = 0;
				for (int corner = 0; corner < 8; ++corner)
				{
					bInside[corner] = IsInside(values.Corner[corner]);
					insideCount += bInside[corner] ? 1 : 0;
				}

				// The samples between its corners never cross where its corners don't
				if (insideCount == 0 || insideCount == 8)
				{
					return;
				}

				// A cell on the box's faces: those shared with finer cells, and its edges on them, make it
				// a cell of their samples
				const bool bOnBox = bSeams && (glm::any(glm::equal(cell, CellMin)) || glm::any(glm::equal(cell + 1, CellMax)));
				if (bOnBox)
				{
					bool bFinerFace[6] = {};
					bool bSplit[EdgeCount] = {};
					bool bAny = false;
					for (int face = 0; face < 6; ++face)
					{
						glm::ivec3 sample = cell;
						sample[face / 2] += face % 2;
						if (sample[face / 2] == CellMin[face / 2] || sample[face / 2] == CellMax[face / 2])
						{
							bFinerFace[face] = Settings.Seams.CellSizes(FaceMiddle(sample, face / 2)).x < 0.75f;
							bAny = bAny || bFinerFace[face];
						}
					}
					for (int edge = 0; edge < EdgeCount; ++edge)
					{
						const glm::ivec3 from = cell + CornerOffset(Edges[edge].From);
						glm::ivec3 to = from;
						to[Edges[edge].Axis] += 1;
						for (int face = 0; face < 6; ++face)
						{
							bSplit[edge] = bSplit[edge] || (bFinerFace[face] && (EdgeFaceMask(edge) & (1 << face)) != 0);
						}
						if (!bSplit[edge] && IsOnBoxFace(from) && IsOnBoxFace(to))
						{
							bSplit[edge] = Settings.Seams.CellSizes(EdgeMiddle(from, Edges[edge].Axis)).x < 0.75f;
						}
						bAny = bAny || bSplit[edge];
					}
					if (bAny)
					{
						MakeMixedCell(cell, values, bFinerFace, bSplit);
						return;
					}
				}

				// On each face, its curves from one crossed edge to the next, each the way round that has
				// the inside of the face on its right seen from outside the cell, so every loop runs
				// counter-clockwise seen from the air
				std::array<int, EdgeCount> next;
				next.fill(-1);
				for (int faceIndex = 0; faceIndex < 6; ++faceIndex)
				{
					const CellFace& face = Faces[faceIndex];
					glm::vec3 outward(0.0f);
					outward[faceIndex / 2] = (faceIndex % 2) != 0 ? 1.0f : -1.0f;

					int crossed[4] = {};
					int count = 0;
					for (int k = 0; k < 4; ++k)
					{
						if (bInside[face.Corners[k]] != bInside[face.Corners[(k + 1) % 4]])
						{
							crossed[count++] = k;
						}
					}

					// Each curve: its two edges and the corner it cuts off, or -1 for one across the face
					int curves[2][3] = {};
					int curveCount = 0;
					if (count == 2)
					{
						const int a = crossed[0];
						const int b = crossed[1];
						const int cut = (b == a + 1) ? face.Corners[b] : (a == 0 && b == 3 ? face.Corners[0] : -1);
						curves[curveCount][0] = face.Edges[a];
						curves[curveCount][1] = face.Edges[b];
						curves[curveCount][2] = cut;
						++curveCount;
					}
					else if (count == 4)
					{
						float middle = values.Corner[face.Corners[0]] + values.Corner[face.Corners[1]] + values.Corner[face.Corners[2]] +
							values.Corner[face.Corners[3]];
						const int axis = faceIndex / 2;
						glm::ivec3 sample = cell;
						sample[axis] += faceIndex % 2;
						if (bOnBox && (sample[axis] == CellMin[axis] || sample[axis] == CellMax[axis]) &&
							Settings.Seams.CellSizes(FaceMiddle(sample, axis)).y > 1.5f)
						{
							// A quarter of a coarser face: its middle is the corner with odd indices
							for (int k = 0; k < 4; ++k)
							{
								const glm::ivec3 at = cell + CornerOffset(face.Corners[k]);
								if ((at[(axis + 1) % 3] & 1) != 0 && (at[(axis + 2) % 3] & 1) != 0)
								{
									middle = values.Corner[face.Corners[k]];
								}
							}
						}
						const bool bCutOutside = IsInside(middle);
						for (int k = 0; k < 4; ++k)
						{
							if (bInside[face.Corners[k]] != bCutOutside)
							{
								curves[curveCount][0] = face.Edges[(k + 3) % 4];
								curves[curveCount][1] = face.Edges[k];
								curves[curveCount][2] = face.Corners[k];
								++curveCount;
							}
						}
					}

					for (int c = 0; c < curveCount; ++c)
					{
						int from = curves[c][0];
						int to = curves[c][1];
						const int cut = curves[c][2];
						// The corner to have on the right: the inside one it cuts off, or across from the
						// outside one it cuts off; for a curve across the face, an inside corner
						int reference = cut;
						bool bWantRight = cut >= 0 && bInside[cut];
						if (cut < 0)
						{
							for (int k = 0; k < 4; ++k)
							{
								if (bInside[face.Corners[k]])
								{
									reference = face.Corners[k];
									break;
								}
							}
							bWantRight = true;
						}
						const glm::vec3 fromMiddle = glm::vec3(CornerOffset(Edges[from].From)) + glm::vec3(CornerOffset(Edges[from].To)) * 1.0f;
						const glm::vec3 toMiddle = glm::vec3(CornerOffset(Edges[to].From)) + glm::vec3(CornerOffset(Edges[to].To)) * 1.0f;
						const glm::vec3 corner = glm::vec3(CornerOffset(reference)) * 2.0f;
						const bool bRight = glm::dot(outward, glm::cross(toMiddle - fromMiddle, corner - fromMiddle)) < 0.0f;
						if (bRight != bWantRight)
						{
							std::swap(from, to);
						}
						next[from] = to;
					}
				}

				// The loops, each from its lowest edge, as polygons
				std::array<bool, EdgeCount> visited{};
				int polygons = 0;
				for (int start = 0; start < EdgeCount; ++start)
				{
					if (next[start] < 0 || visited[start])
					{
						continue;
					}
					int faceMasks[EdgeCount];
					int32_t loop[EdgeCount];
					int length = 0;
					for (int edge = start; !visited[edge] && length < EdgeCount; edge = next[edge])
					{
						visited[edge] = true;
						faceMasks[length] = EdgeFaceMask(edge);
						loop[length++] = GetEdgeVertex(cell + CornerOffset(Edges[edge].From), Edges[edge].Axis);
						if (next[edge] < 0)
						{
							break;
						}
					}
					AddPolygon(cell, faceMasks, loop, length);
					++polygons;
				}
				if (polygons > 1)
				{
					++SplitCells;
				}
			}

			// A cell next to finer cells: each face it shares with them cut in four, each edge on them in
			// two, the samples between its corners as both meshes take them (InterpolateSeamSample()).
			// Points of the cell are counted in half cells, 0 to 2 on each axis; its edges become segments
			// between them, a cell's or a half, joined into loops through its faces and quarter faces as a
			// cell's edges are.
			void MakeMixedCell(const glm::ivec3& cell, const CellValues& values, const bool bFinerFace[6], const bool bSplit[EdgeCount])
			{
				++SeamCells;
				auto pointIndex = [](const glm::ivec3& point) { return point.x + 3 * (point.y + 3 * point.z); };
				float pointValues[27] = {};
				uint32_t pointMaterials[27] = {};
				for (int corner = 0; corner < 8; ++corner)
				{
					const glm::ivec3 at = cell + CornerOffset(corner);
					pointValues[pointIndex(CornerOffset(corner) * 2)] = values.Corner[corner];
					pointMaterials[pointIndex(CornerOffset(corner) * 2)] = Grid.GetMaterial(at.x, at.y, at.z);
				}
				for (int edge = 0; edge < EdgeCount; ++edge)
				{
					if (bSplit[edge])
					{
						const glm::ivec3 from = CornerOffset(Edges[edge].From) * 2;
						glm::ivec3 middle = from;
						middle[Edges[edge].Axis] = 1;
						const float ends[2] = { values.Corner[Edges[edge].From], values.Corner[Edges[edge].To] };
						pointValues[pointIndex(middle)] = InterpolateSeamSample(ends, 2);
						pointMaterials[pointIndex(middle)] = pointMaterials[pointIndex(from)];
					}
				}
				for (int face = 0; face < 6; ++face)
				{
					if (!bFinerFace[face])
					{
						continue;
					}
					const int axis = face / 2;
					const int low = axis == 0 ? 1 : 0;
					const int high = axis == 2 ? 1 : 2;
					// Its corners in order of its two axes, the lowest first
					float corners[4];
					int base = 0;
					for (int n = 0; n < 4; ++n)
					{
						const int corner = ((face % 2) << axis) | ((n & 1) << low) | (((n >> 1) & 1) << high);
						corners[n] = values.Corner[corner];
						if (n == 0)
						{
							base = corner;
						}
					}
					glm::ivec3 middle(1);
					middle[axis] = (face % 2) * 2;
					pointValues[pointIndex(middle)] = InterpolateSeamSample(corners, 4);
					pointMaterials[pointIndex(middle)] = pointMaterials[pointIndex(CornerOffset(base) * 2)];
				}

				// The segments: each edge whole or in halves, and on each finer face the four from its
				// middle to its edges' middles
				struct Segment
				{
					glm::ivec3 From{ 0 };
					int Axis = 0;
					int Length = 2;
					int FaceMask = 0;
					bool bCrossed = false;
				};
				std::vector<Segment> segments;
				segments.reserve(48);
				auto addSegment = [&](const glm::ivec3& from, int axis, int length)
				{
					Segment segment;
					segment.From = from;
					segment.Axis = axis;
					segment.Length = length;
					glm::ivec3 to = from;
					to[axis] += length;
					for (int other = 0; other < 3; ++other)
					{
						if (other != axis && (from[other] == 0 || from[other] == 2))
						{
							segment.FaceMask |= 1 << (other * 2 + from[other] / 2);
						}
					}
					segment.bCrossed = IsInside(pointValues[pointIndex(from)]) != IsInside(pointValues[pointIndex(to)]);
					segments.push_back(segment);
				};
				auto findSegment = [&](const glm::ivec3& from, int axis, int length)
				{
					for (size_t n = 0; n < segments.size(); ++n)
					{
						if (segments[n].From == from && segments[n].Axis == axis && segments[n].Length == length)
						{
							return static_cast<int>(n);
						}
					}
					return -1;
				};
				for (int edge = 0; edge < EdgeCount; ++edge)
				{
					const glm::ivec3 from = CornerOffset(Edges[edge].From) * 2;
					if (bSplit[edge])
					{
						glm::ivec3 middle = from;
						middle[Edges[edge].Axis] = 1;
						addSegment(from, Edges[edge].Axis, 1);
						addSegment(middle, Edges[edge].Axis, 1);
					}
					else
					{
						addSegment(from, Edges[edge].Axis, 2);
					}
				}
				for (int face = 0; face < 6; ++face)
				{
					if (!bFinerFace[face])
					{
						continue;
					}
					const int axis = face / 2;
					glm::ivec3 middle(1);
					middle[axis] = (face % 2) * 2;
					for (int other = 0; other < 3; ++other)
					{
						if (other != axis)
						{
							glm::ivec3 before = middle;
							before[other] = 0;
							addSegment(before, other, 1);
							addSegment(middle, other, 1);
						}
					}
				}

				// The curves on each face, or on each quarter of a finer one, as on a cell's face
				std::vector<int> next(segments.size(), -1);
				auto addCurves = [&](const glm::ivec3 points[4], const int sides[4], const glm::vec3& outward, float middle)
				{
					bool bInsidePoint[4];
					for (int k = 0; k < 4; ++k)
					{
						bInsidePoint[k] = IsInside(pointValues[pointIndex(points[k])]);
					}
					int crossed[4] = {};
					int count = 0;
					for (int k = 0; k < 4; ++k)
					{
						if (bInsidePoint[k] != bInsidePoint[(k + 1) % 4])
						{
							crossed[count++] = k;
						}
					}
					int curves[2][3] = {};
					int curveCount = 0;
					if (count == 2)
					{
						const int a = crossed[0];
						const int b = crossed[1];
						curves[0][0] = a;
						curves[0][1] = b;
						curves[0][2] = (b == a + 1) ? b : (a == 0 && b == 3 ? 0 : -1);
						curveCount = 1;
					}
					else if (count == 4)
					{
						const bool bCutOutside = IsInside(middle);
						for (int k = 0; k < 4; ++k)
						{
							if (bInsidePoint[k] != bCutOutside)
							{
								curves[curveCount][0] = (k + 3) % 4;
								curves[curveCount][1] = k;
								curves[curveCount][2] = k;
								++curveCount;
							}
						}
					}
					for (int c = 0; c < curveCount; ++c)
					{
						int from = curves[c][0];
						int to = curves[c][1];
						const int cut = curves[c][2];
						int reference = cut;
						bool bWantRight = cut >= 0 && bInsidePoint[cut];
						if (cut < 0)
						{
							for (int k = 0; k < 4; ++k)
							{
								if (bInsidePoint[k])
								{
									reference = k;
									break;
								}
							}
							bWantRight = true;
						}
						const glm::vec3 fromMiddle = glm::vec3(points[from] + points[(from + 1) % 4]);
						const glm::vec3 toMiddle = glm::vec3(points[to] + points[(to + 1) % 4]);
						const glm::vec3 corner = glm::vec3(points[reference]) * 2.0f;
						const bool bRight = glm::dot(outward, glm::cross(toMiddle - fromMiddle, corner - fromMiddle)) < 0.0f;
						if (bRight != bWantRight)
						{
							std::swap(from, to);
						}
						next[sides[from]] = sides[to];
					}
				};
				// The segment from point `a` to point `b`, `length` apart along one axis
				auto between = [&](const glm::ivec3& a, const glm::ivec3& b, int length)
				{
					int axis = 0;
					while (a[axis] == b[axis])
					{
						++axis;
					}
					return findSegment(a[axis] < b[axis] ? a : b, axis, length);
				};
				for (int face = 0; face < 6; ++face)
				{
					const CellFace& cellFace = Faces[face];
					glm::vec3 outward(0.0f);
					outward[face / 2] = (face % 2) != 0 ? 1.0f : -1.0f;
					if (!bFinerFace[face])
					{
						// Its corners, and on each side the segment the side's crossing is on
						glm::ivec3 points[4];
						int sides[4];
						for (int k = 0; k < 4; ++k)
						{
							points[k] = CornerOffset(cellFace.Corners[k]) * 2;
						}
						for (int k = 0; k < 4; ++k)
						{
							const int edge = cellFace.Edges[k];
							const glm::ivec3 from = CornerOffset(Edges[edge].From) * 2;
							sides[k] = findSegment(from, Edges[edge].Axis, 2);
							if (bSplit[edge])
							{
								glm::ivec3 middle = from;
								middle[Edges[edge].Axis] = 1;
								const int first = findSegment(from, Edges[edge].Axis, 1);
								sides[k] = segments[first].bCrossed ? first : findSegment(middle, Edges[edge].Axis, 1);
							}
						}
						const float middle = values.Corner[cellFace.Corners[0]] + values.Corner[cellFace.Corners[1]] + values.Corner[cellFace.Corners[2]] +
							values.Corner[cellFace.Corners[3]];
						addCurves(points, sides, outward, middle);
						continue;
					}
					// Its quarters, each round from its corner of the face: the face's middle decides them
					const int axis = face / 2;
					glm::ivec3 centre(1);
					centre[axis] = (face % 2) * 2;
					for (int k = 0; k < 4; ++k)
					{
						const glm::ivec3 corner = CornerOffset(cellFace.Corners[k]) * 2;
						const glm::ivec3 afterCorner = (corner + CornerOffset(cellFace.Corners[(k + 1) % 4]) * 2) / 2;
						const glm::ivec3 beforeCorner = (corner + CornerOffset(cellFace.Corners[(k + 3) % 4]) * 2) / 2;
						const glm::ivec3 points[4] = { corner, afterCorner, centre, beforeCorner };
						const int sides[4] = { between(points[0], points[1], 1), between(points[1], points[2], 1), between(points[2], points[3], 1),
							between(points[3], points[0], 1) };
						addCurves(points, sides, outward, pointValues[pointIndex(centre)]);
					}
				}

				// The loops, each from its lowest segment, as polygons
				std::vector<bool> visited(segments.size(), false);
				int polygons = 0;
				std::vector<int> faceMasks;
				std::vector<int32_t> loop;
				for (size_t start = 0; start < segments.size(); ++start)
				{
					if (next[start] < 0 || visited[start])
					{
						continue;
					}
					faceMasks.clear();
					loop.clear();
					for (int segment = static_cast<int>(start); !visited[segment] && loop.size() < segments.size(); segment = next[segment])
					{
						visited[segment] = true;
						faceMasks.push_back(segments[segment].FaceMask);
						loop.push_back(GetSegmentVertex(cell, segments[segment].From, segments[segment].Axis, segments[segment].Length, pointValues, pointMaterials));
						if (next[segment] < 0)
						{
							break;
						}
					}
					AddPolygon(cell, faceMasks.data(), loop.data(), static_cast<int>(loop.size()));
					++polygons;
				}
				if (polygons > 1)
				{
					++SplitCells;
				}
			}

			// A segment's vertex: a whole edge's, or that of a finer edge, the same bits as the finer
			// mesh's vertex on it
			int32_t GetSegmentVertex(const glm::ivec3& cell, const glm::ivec3& from, int axis, int length, const float* pointValues, const uint32_t* pointMaterials)
			{
				if (length == 2)
				{
					return GetEdgeVertex(cell + from / 2, axis);
				}
				const glm::ivec3 fine = cell * 2 + from;
				const uint64_t key = (uint64_t(uint32_t(fine.x) & 0xFFFFFu) | (uint64_t(uint32_t(fine.y) & 0xFFFFFu) << 20) |
					(uint64_t(uint32_t(fine.z) & 0xFFFFFu) << 40)) * 3 + uint64_t(axis);
				const auto found = FineVertices.find(key);
				if (found != FineVertices.end())
				{
					return found->second;
				}
				auto pointIndex = [](const glm::ivec3& point) { return point.x + 3 * (point.y + 3 * point.z); };
				glm::ivec3 to = from;
				to[axis] += 1;
				const float fromValue = pointValues[pointIndex(from)];
				const float toValue = pointValues[pointIndex(to)];
				const float t = std::clamp(fromValue / (fromValue - toValue), 0.001f, 0.999f);
				const float half = Grid.CellSize * 0.5f;

				EdgeVertex vertex;
				vertex.Cell = cell;
				vertex.Local = glm::vec3(from) * 0.5f;
				vertex.Local[axis] += t * 0.5f;
				vertex.bExact = true;
				vertex.Exact = View.CellCorner(cell) + glm::vec3(from) * half;
				vertex.Exact[axis] = vertex.Exact[axis] + t * half;
				vertex.Material = pointMaterials[pointIndex(IsInside(fromValue) ? from : to)];
				vertex.bSeam = true;
				vertex.NormalStep = 0.05f * half;
				// Without the field, the cell's corners' gradients blended there
				glm::vec3 corners[8];
				for (int corner = 0; corner < 8; ++corner)
				{
					corners[corner] = SampleGradient(Grid, cell + CornerOffset(corner));
				}
				const glm::vec3& l = vertex.Local;
				const glm::vec3 x0 = corners[0] + (corners[1] - corners[0]) * l.x;
				const glm::vec3 x1 = corners[2] + (corners[3] - corners[2]) * l.x;
				const glm::vec3 x2 = corners[4] + (corners[5] - corners[4]) * l.x;
				const glm::vec3 x3 = corners[6] + (corners[7] - corners[6]) * l.x;
				const glm::vec3 gradient = (x0 + (x1 - x0) * l.y) + ((x2 + (x3 - x2) * l.y) - (x0 + (x1 - x0) * l.y)) * l.z;
				const float gradientLength = glm::length(gradient);
				vertex.Normal = gradientLength > 0.0f ? gradient / gradientLength : glm::vec3(0.0f, 1.0f, 0.0f);

				const int32_t index = static_cast<int32_t>(Vertices.size());
				Vertices.push_back(vertex);
				FineVertices.emplace(key, index);
				return index;
			}

			glm::vec3 PositionOf(const EdgeVertex& vertex) const
			{
				return vertex.bExact ? vertex.Exact : View.CellCorner(vertex.Cell) + vertex.Local * Grid.CellSize;
			}

			MeshData Finish(FieldMeshStats& stats)
			{
				// Seam vertices' normals from the field, as the mesh on the other side has them: its
				// differences along each axis from the vertex, all asked for at once
				if (Settings.Field)
				{
					std::vector<glm::vec3> points;
					std::vector<size_t> asked;
					for (size_t v = 0; v < Vertices.size(); ++v)
					{
						if (Vertices[v].bSeam)
						{
							const glm::vec3 position = PositionOf(Vertices[v]);
							points.push_back(position);
							for (int axis = 0; axis < 3; ++axis)
							{
								glm::vec3 point = position;
								point[axis] += Vertices[v].NormalStep;
								points.push_back(point);
							}
							asked.push_back(v);
						}
					}
					std::vector<float> fieldValues;
					if (!points.empty())
					{
						View.AskField(points, fieldValues);
					}
					for (size_t n = 0; n < asked.size(); ++n)
					{
						const float* at = fieldValues.data() + n * 4;
						const glm::vec3 gradient(at[1] - at[0], at[2] - at[0], at[3] - at[0]);
						const float length = glm::length(gradient);
						if (length > 0.0f)
						{
							Vertices[asked[n]].Normal = gradient / length;
						}
					}
				}
				for (const int32_t centre : SeamCentres)
				{
					Vertices[centre].bSeam = true;
				}

				std::vector<glm::ivec3> cells(Vertices.size());
				std::vector<glm::vec3> locals(Vertices.size());
				std::vector<glm::vec3> normals(Vertices.size());
				for (size_t v = 0; v < Vertices.size(); ++v)
				{
					cells[v] = Vertices[v].Cell;
					locals[v] = Vertices[v].Local;
					normals[v] = Vertices[v].Normal;
				}
				std::vector<float> sky;
				View.ComputeSkyVisibility(cells, locals, normals, sky);

				MeshData mesh;
				mesh.Vertices.resize(Vertices.size());
				for (size_t v = 0; v < Vertices.size(); ++v)
				{
					Vertex& vertex = mesh.Vertices[v];
					vertex.Position = PositionOf(Vertices[v]);
					vertex.Normal = normals[v];
					const glm::vec3 side = std::fabs(normals[v].z) < 0.9f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
					vertex.Tangent = glm::vec4(glm::normalize(glm::cross(side, normals[v])), 1.0f);
					vertex.Custom = glm::vec4(sky[v], float(Vertices[v].Material), 0.0f, Vertices[v].bSeam ? 1.0f : 0.0f);
				}
				mesh.Indices.reserve(Triangles.size() * 3);
				for (const auto& triangle : Triangles)
				{
					const glm::vec3 a = glm::vec3(Vertices[triangle[0]].Cell) + Vertices[triangle[0]].Local;
					const glm::vec3 facing = glm::cross(glm::vec3(Vertices[triangle[1]].Cell) + Vertices[triangle[1]].Local - a,
						glm::vec3(Vertices[triangle[2]].Cell) + Vertices[triangle[2]].Local - a);
					if (glm::dot(facing, normals[triangle[0]] + normals[triangle[1]] + normals[triangle[2]]) < 0.0f)
					{
						++stats.BackFacingTriangles;
					}
					if (Vertices[triangle[0]].bSeam || Vertices[triangle[1]].bSeam || Vertices[triangle[2]].bSeam)
					{
						++stats.SeamTriangles;
					}
					for (const int32_t index : triangle)
					{
						mesh.Indices.push_back(static_cast<uint32_t>(index));
					}
				}
				stats.Vertices = static_cast<uint32_t>(mesh.Vertices.size());
				stats.Triangles = static_cast<uint32_t>(Triangles.size());
				stats.SplitCells = SplitCells;
				stats.SeamCells = SeamCells;
				return mesh;
			}

			const FieldGrid& Grid;
			const FieldMeshSettings& Settings;
			GridView View;
			glm::ivec3 CellMin{ 0 };
			glm::ivec3 CellMax{ 0 };
			bool bSeams = false;
			// Each grid edge's vertex: three a sample, one along each axis; -1 where none is made yet
			std::vector<int32_t> EdgeVertices;
			// The vertices on finer edges, by their start in half cells and axis
			std::unordered_map<uint64_t, int32_t> FineVertices;
			std::vector<EdgeVertex> Vertices;
			std::vector<std::array<int32_t, 3>> Triangles;
			// Middle vertices of polygons through finer edges
			std::vector<int32_t> SeamCentres;
			uint32_t SplitCells = 0;
			uint32_t SeamCells = 0;
		};
	}

	MeshData MakeFieldMesh(const FieldGrid& grid, const FieldMeshSettings& settings, FieldMeshStats* outStats)
	{
		FieldMeshStats stats;
		MeshData mesh;
		if (IsGridValid(grid))
		{
			Mesher mesher(grid, settings);
			mesh = mesher.Make(stats);
		}
		if (outStats != nullptr)
		{
			*outStats = stats;
		}
		return mesh;
	}

	MeshData MakeMarchingFieldMesh(const FieldGrid& grid, const FieldMeshSettings& settings, FieldMeshStats* outStats)
	{
		FieldMeshStats stats;
		MeshData mesh;
		if (IsGridValid(grid))
		{
			MarchingMesher mesher(grid, settings);
			mesh = mesher.Make(stats);
		}
		if (outStats != nullptr)
		{
			*outStats = stats;
		}
		return mesh;
	}
}
