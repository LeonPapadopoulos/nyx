#include "NyxPCH.h"
#include "FieldMesher.h"

#include <algorithm>
#include <array>
#include <cmath>

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

		class Mesher
		{
		public:
			Mesher(const FieldGrid& grid, const FieldMeshSettings& settings) : Grid(grid), Settings(settings)
			{
				CellCounts = glm::max(grid.Size - 1, glm::ivec3(0));
				Cells.resize(size_t(CellCounts.x) * size_t(CellCounts.y) * size_t(CellCounts.z));
				for (int axis = 0; axis < 3; ++axis)
				{
					QuadMin[axis] = settings.QuadMin[axis] > 0 ? settings.QuadMin[axis] : 1;
					QuadMax[axis] = settings.QuadMax[axis] > 0 ? settings.QuadMax[axis] : grid.Size[axis] - 1;
					QuadMin[axis] = std::max(QuadMin[axis], 1);
					QuadMax[axis] = std::min(QuadMax[axis], grid.Size[axis] - 1);
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
			struct Quad
			{
				int32_t V[4];
				int Axis;
				int Sign;
			};

			size_t CellIndex(const glm::ivec3& cell) const
			{
				return size_t(cell.x) + size_t(CellCounts.x) * (size_t(cell.y) + size_t(CellCounts.y) * size_t(cell.z));
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

			glm::vec3 GridPosition(const WorkVertex& vertex) const
			{
				return glm::vec3(vertex.Cell) + vertex.Local;
			}

			bool IsSharp(uint32_t material) const
			{
				return material < Settings.SharpMaterials.size() && Settings.SharpMaterials[material];
			}

			// The cells round the grid edge from `from` along `axis`, counter-clockwise about it; false if
			// one is outside the grid
			bool GetCellsAround(const glm::ivec3& from, int axis, glm::ivec3 outCells[4], int outEdges[4]) const
			{
				const int b = (axis + 1) % 3;
				const int c = (axis + 2) % 3;
				for (int n = 0; n < 4; ++n)
				{
					glm::ivec3 cell = from;
					cell[b] += Around[n][0] - 1;
					cell[c] += Around[n][1] - 1;
					if (!IsCellInGrid(cell))
					{
						return false;
					}
					const int localCorner = ((1 - Around[n][0]) << b) | ((1 - Around[n][1]) << c);
					outCells[n] = cell;
					outEdges[n] = EdgeBetween(localCorner, localCorner | (1 << axis));
				}
				return true;
			}

			// The cell's pieces of surface and their vertices, made the first time they are needed
			CellRecord& GetCell(const glm::ivec3& cell)
			{
				CellRecord& record = Cells[CellIndex(cell)];
				if (record.bMade)
				{
					return record;
				}
				record.bMade = true;
				record.EdgePiece.fill(-1);

				const CellValues values = ReadCell(Grid, cell);
				bool bInside[8] = {};
				for (int corner = 0; corner < 8; ++corner)
				{
					bInside[corner] = IsInside(values.Corner[corner]);
				}

				// Crossed edges joined into pieces through the faces: a face with two crossed edges
				// joins them; one with four joins each pair round a corner its middle cuts off
				std::array<int, EdgeCount> parents{};
				for (int edge = 0; edge < EdgeCount; ++edge)
				{
					parents[edge] = edge;
				}
				for (const CellFace& face : Faces)
				{
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
						const float middle = values.Corner[face.Corners[0]] + values.Corner[face.Corners[1]] + values.Corner[face.Corners[2]] +
							values.Corner[face.Corners[3]];
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
				if (pieces > 1)
				{
					++SplitCells;
				}

				record.FirstVertex = static_cast<int32_t>(Vertices.size());
				for (int piece = 0; piece < pieces; ++piece)
				{
					Vertices.push_back(MakeVertex(cell, values, bInside, record, piece));
				}
				return record;
			}

			WorkVertex MakeVertex(const glm::ivec3& cell, const CellValues& values, const bool bInside[8], const CellRecord& record, int piece)
			{
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
					const glm::vec3 fromGradient = SampleGradient(Grid, cell + CornerOffset(cellEdge.From));
					const glm::vec3 toGradient = SampleGradient(Grid, cell + CornerOffset(cellEdge.To));
					gradients[count] = fromGradient + (toGradient - fromGradient) * t;

					const int insideCorner = bInside[cellEdge.From] ? cellEdge.From : cellEdge.To;
					const int outsideCorner = bInside[cellEdge.From] ? cellEdge.To : cellEdge.From;
					ends[count].Inside = CellCorner(cell + CornerOffset(insideCorner));
					ends[count].Outside = CellCorner(cell + CornerOffset(outsideCorner));
					ends[count].InsideValue = values.Corner[insideCorner];
					ends[count].OutsideValue = values.Corner[outsideCorner];
					const glm::ivec3 at = cell + CornerOffset(insideCorner);
					materials[count] = Grid.GetMaterial(at.x, at.y, at.z);
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
						crossing.Point = CellCorner(cell) + crossings[k] * Grid.CellSize;
						crossing.Gradient = gradients[k];
						SharpCrossings.push_back(crossing);
					}
				}
				return vertex;
			}

			void MakeQuads()
			{
				for (int axis = 0; axis < 3; ++axis)
				{
					for (int k = QuadMin.z; k < QuadMax.z; ++k)
					{
						for (int j = QuadMin.y; j < QuadMax.y; ++j)
						{
							for (int i = QuadMin.x; i < QuadMax.x; ++i)
							{
								const glm::ivec3 from(i, j, k);
								glm::ivec3 to = from;
								to[axis] += 1;
								if (to[axis] > Grid.Size[axis] - 1)
								{
									continue;
								}
								const bool bFromInside = IsInside(Grid.GetValue(from.x, from.y, from.z));
								const bool bToInside = IsInside(Grid.GetValue(to.x, to.y, to.z));
								if (bFromInside == bToInside)
								{
									continue;
								}

								glm::ivec3 cells[4];
								int edges[4];
								if (!GetCellsAround(from, axis, cells, edges))
								{
									continue;
								}
								int32_t quad[4] = {};
								for (int n = 0; n < 4; ++n)
								{
									const CellRecord& record = GetCell(cells[n]);
									quad[n] = record.FirstVertex + record.EdgePiece[edges[n]];
								}
								// Facing out: towards the outside end of the edge
								if (!bFromInside)
								{
									std::swap(quad[1], quad[3]);
								}
								for (const int32_t index : quad)
								{
									Vertices[index].bUsed = true;
								}
								Quads.push_back({ { quad[0], quad[1], quad[2], quad[3] }, axis, bFromInside ? 1 : -1 });
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
					const glm::ivec3 cell = Vertices[v].Cell;
					const CellRecord& record = Cells[CellIndex(cell)];
					for (int edge = 0; edge < EdgeCount; ++edge)
					{
						if (record.EdgePiece[edge] < 0)
						{
							continue;
						}
						glm::ivec3 cells[4];
						int edges[4];
						if (GetCellsAround(cell + CornerOffset(Edges[edge].From), Edges[edge].Axis, cells, edges))
						{
							for (const glm::ivec3& around : cells)
							{
								GetCell(around);
							}
						}
					}
				}
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
					AskField(points, values);
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
					const float step = 0.05f * Grid.CellSize;
					std::vector<glm::vec3> points;
					points.reserve(SharpCrossings.size() * 3);
					for (const Crossing& crossing : SharpCrossings)
					{
						for (int axis = 0; axis < 3; ++axis)
						{
							glm::vec3 point = crossing.Point;
							point[axis] += step;
							points.push_back(point);
						}
					}
					std::vector<float> values;
					AskField(points, values);
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
					const glm::vec3 corner = CellCorner(vertex.Cell);
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
						const glm::vec3 local = (crossing.Point - corner) / Grid.CellSize;
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
			// own cell and its neighbours alone (they at their middles), so both chunks that have a cell
			// decide the same for it.
			void UnfoldShared(FieldMeshStats& stats)
			{
				for (size_t v = 0; v < Vertices.size(); ++v)
				{
					WorkVertex& vertex = Vertices[v];
					if (!vertex.bSharp || !vertex.bUsed || vertex.Local == vertex.MassLocal)
					{
						continue;
					}
					const CellRecord& own = Cells[CellIndex(vertex.Cell)];
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
						if (!GetCellsAround(from, axis, cells, edges))
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
							const CellRecord& record = Cells[CellIndex(cells[n])];
							const WorkVertex& other = Vertices[record.FirstVertex + record.EdgePiece[edges[n]]];
							corners[n] = glm::vec3(cells[n] - vertex.Cell) + other.MassLocal;
						}
						const bool bFromInside = IsInside(Grid.GetValue(from.x, from.y, from.z));
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

			// Whether no other chunk has the cell: its quads, and the cells they join, are all this mesh's
			bool IsPrivateCell(const glm::ivec3& cell) const
			{
				return glm::all(glm::greaterThanEqual(cell, QuadMin)) && glm::all(glm::lessThan(cell, QuadMax - 1));
			}

			// The quad's two triangles: split along the diagonal whose triangles both face out, or along
			// the shorter one if both or neither do
			void Triangulate(const Quad& quad, int32_t outTriangles[2][3]) const
			{
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
			}

			// The quad's two triangles for the mesh: split along the diagonal whose triangles agree best
			// with the field's normals at their corners (the worst of the two, as a cosine), the
			// shorter one on a tie
			void TriangulateByNormals(const Quad& quad, const std::vector<uint32_t>& remap, const std::vector<glm::vec3>& normals, int32_t outTriangles[2][3]) const
			{
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
			}

			// Then, as the triangles really are: a sharp vertex of a cell no other chunk has whose
			// triangles face the wrong way goes back to its middle, until none does
			void UnfoldPrivate(FieldMeshStats& stats)
			{
				for (int pass = 0; pass < 4; ++pass)
				{
					bool bMoved = false;
					for (const Quad& quad : Quads)
					{
						int32_t triangles[2][3];
						Triangulate(quad, triangles);
						for (const auto& triangle : triangles)
						{
							if (FacesAlong(GridPosition(Vertices[triangle[0]]), GridPosition(Vertices[triangle[1]]), GridPosition(Vertices[triangle[2]]),
									quad.Axis, quad.Sign))
							{
								continue;
							}
							for (const int32_t index : triangle)
							{
								WorkVertex& vertex = Vertices[index];
								if (vertex.bSharp && vertex.Local != vertex.MassLocal && IsPrivateCell(vertex.Cell))
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
			void ComputeSkyVisibility(const std::vector<size_t>& used, const std::vector<glm::vec3>& normals, std::vector<float>& outVisibility) const
			{
				outVisibility.assign(used.size(), 1.0f);
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

				std::vector<uint8_t> blocked(used.size() * directions.size(), 0);
				// Points past the grid, asked of the field in one go
				std::vector<glm::vec3> farPoints;
				std::vector<size_t> farRays;
				for (size_t u = 0; u < used.size(); ++u)
				{
					const WorkVertex& vertex = Vertices[used[u]];
					const glm::vec3 start = vertex.Local + normals[u] * 0.5f;
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
							if (SampleGrid(vertex.Cell, local, value))
							{
								if (IsInside(value))
								{
									blocked[ray] = 1;
									break;
								}
							}
							else if (Settings.Field)
							{
								farPoints.push_back(CellCorner(vertex.Cell) + local * Grid.CellSize);
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

				for (size_t u = 0; u < used.size(); ++u)
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

			// The corners' gradients blended trilinearly at a point of the cell
			glm::vec3 BlendGradients(const glm::ivec3& cell, const glm::vec3& local) const
			{
				glm::vec3 corners[8];
				for (int corner = 0; corner < 8; ++corner)
				{
					corners[corner] = SampleGradient(Grid, cell + CornerOffset(corner));
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
					const glm::vec3 gradient = BlendGradients(vertex.Cell, vertex.Local);
					const float length = glm::length(gradient);
					normals[u] = length > 0.0f ? gradient / length : glm::vec3(0.0f, 1.0f, 0.0f);
				}
				std::vector<float> sky;
				ComputeSkyVisibility(used, normals, sky);

				MeshData mesh;
				mesh.Vertices.resize(used.size());
				for (size_t u = 0; u < used.size(); ++u)
				{
					const WorkVertex& work = Vertices[used[u]];
					Vertex& vertex = mesh.Vertices[u];
					vertex.Position = CellCorner(work.Cell) + work.Local * Grid.CellSize;
					vertex.Normal = normals[u];
					const glm::vec3 side = std::fabs(normals[u].z) < 0.9f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
					vertex.Tangent = glm::vec4(glm::normalize(glm::cross(side, normals[u])), 1.0f);
					vertex.Custom = glm::vec4(sky[u], float(work.Material), work.bSharp ? 1.0f : 0.0f, 0.0f);
					stats.SharpVertices += work.bSharp ? 1u : 0u;
				}

				mesh.Indices.reserve(Quads.size() * 6);
				for (const Quad& quad : Quads)
				{
					int32_t triangles[2][3];
					TriangulateByNormals(quad, remap, normals, triangles);
					for (const auto& triangle : triangles)
					{
						// Against the field's own normals at its corners: what would show as a triangle
						// turned inside out
						const glm::vec3 a = GridPosition(Vertices[triangle[0]]);
						const glm::vec3 facing = glm::cross(GridPosition(Vertices[triangle[1]]) - a, GridPosition(Vertices[triangle[2]]) - a);
						const glm::vec3 normal = normals[remap[triangle[0]]] + normals[remap[triangle[1]]] + normals[remap[triangle[2]]];
						if (glm::dot(facing, normal) < 0.0f)
						{
							++stats.BackFacingTriangles;
						}
						for (const int32_t index : triangle)
						{
							mesh.Indices.push_back(remap[index]);
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
			glm::ivec3 CellCounts{ 0 };
			glm::ivec3 QuadMin{ 1 };
			glm::ivec3 QuadMax{ 1 };
			std::vector<CellRecord> Cells;
			std::vector<WorkVertex> Vertices;
			std::vector<Crossing> SharpCrossings;
			std::vector<Quad> Quads;
			uint32_t SplitCells = 0;
		};
	}

	MeshData MakeFieldMesh(const FieldGrid& grid, const FieldMeshSettings& settings, FieldMeshStats* outStats)
	{
		FieldMeshStats stats;
		MeshData mesh;
		const size_t samples = size_t(std::max(grid.Size.x, 0)) * size_t(std::max(grid.Size.y, 0)) * size_t(std::max(grid.Size.z, 0));
		if (glm::all(glm::greaterThanEqual(grid.Size, glm::ivec3(2))) && grid.Values.size() == samples &&
			(grid.Materials.empty() || grid.Materials.size() == samples))
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
}
