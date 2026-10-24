#include "NyxPCH.h"
#include "ScatterInstances.h"

#include "GradientNoise.h"
#include "GuidComponent.h"
#include "PhysicsComponents.h"
#include "Profiler.h"
#include "RuntimeMeshComponent.h"
#include "ScatterBlockerComponent.h"
#include "TransformComponent.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace Nyx
{
	namespace
	{
		constexpr float Pi = 3.14159265358979f;
		constexpr uint32_t NoPoint = 0xFFFFFFFFu;
		// At most this many points thrown, kept or not, whatever the density
		constexpr uint32_t MaxScatterPoints = MaxScatterInstances * 4;
		// At most this many squares a side, for the spacing and for the cells
		constexpr uint32_t MaxSpacingSquares = 1024;
		constexpr uint32_t MaxCellsPerSide = 256;
		// A blocker's place as the scatters take it: its position to the centimetre, each number of its
		// rotation to a 1024th (about a tenth of a degree), its scale to a thousandth
		constexpr float BlockerPositionSteps = 100.0f;
		constexpr float BlockerRotationSteps = 1024.0f;
		constexpr float BlockerScaleSteps = 1000.0f;
		// Added round what a blocker reaches, so rounding never puts a point it changes outside it
		constexpr float BlockerReachPadding = 0.01f;

		// Mixes the bits of a number: integer arithmetic only, the same everywhere
		uint32_t MixBits(uint32_t value)
		{
			value ^= value >> 16;
			value *= 0x7FEB352Du;
			value ^= value >> 15;
			value *= 0x846CA68Bu;
			return value ^ (value >> 16);
		}

		// The `draw`th number of the `point`th point thrown with this seed
		uint32_t HashPoint(uint32_t seed, uint32_t point, uint32_t draw)
		{
			return MixBits(MixBits(MixBits(seed + 0x9E3779B9u) ^ point) + draw * 0x632BE5ABu);
		}

		// A cell's own seed, for the points thrown in it
		uint32_t HashCell(uint32_t seed, uint32_t column, uint32_t row)
		{
			return MixBits(MixBits(MixBits(seed ^ 0x5BD1E995u) + column * 0x85EBCA77u) + row * 0xC2B2AE3Du);
		}

		// From 0 up to, not including, 1: the top 24 bits, which a float holds exactly
		float ToUnit(uint32_t hash)
		{
			return static_cast<float>(hash >> 8) * (1.0f / 16777216.0f);
		}

		// The same with all 32 bits, for picking one of many triangles
		double ToUnitDouble(uint32_t hash)
		{
			return static_cast<double>(hash) * (1.0 / 4294967296.0);
		}

		struct SineCosine
		{
			float Sine = 0.0f;
			float Cosine = 1.0f;
		};

		// Sine and cosine by their series to within about 1e-7, from additions and multiplications in
		// a fixed order: a C library's sin and cos may round their own way
		SineCosine GetSineCosine(float radians)
		{
			// Into -pi to pi, then -pi/2 to pi/2, where the series are short
			float x = radians - 2.0f * Pi * std::floor((radians + Pi) / (2.0f * Pi));
			float cosineSign = 1.0f;
			if (x > 0.5f * Pi)
			{
				x = Pi - x;
				cosineSign = -1.0f;
			}
			else if (x < -0.5f * Pi)
			{
				x = -Pi - x;
				cosineSign = -1.0f;
			}
			const float x2 = x * x;
			const float sine = x * (1.0f + x2 * (-1.0f / 6.0f + x2 * (1.0f / 120.0f + x2 * (-1.0f / 5040.0f + x2 * (1.0f / 362880.0f + x2 * (-1.0f / 39916800.0f))))));
			const float cosine = 1.0f + x2 * (-0.5f + x2 * (1.0f / 24.0f + x2 * (-1.0f / 720.0f + x2 * (1.0f / 40320.0f + x2 * (-1.0f / 3628800.0f + x2 * (1.0f / 479001600.0f))))));
			return SineCosine{ sine, cosineSign * cosine };
		}

		float ToRadians(float degrees)
		{
			return degrees * (Pi / 180.0f);
		}

		glm::vec2 Ordered(const glm::vec2& range)
		{
			return glm::vec2(std::min(range.x, range.y), std::max(range.x, range.y));
		}

		glm::vec2 GetHalfSize(const ScatterComponent& scatter)
		{
			return glm::max(scatter.Size, glm::vec2(0.01f)) * 0.5f;
		}

		// 0 below `from`, 1 above `to`, smooth between
		float SmoothStep(float from, float to, float value)
		{
			if (to <= from)
			{
				return value < from ? 0.0f : 1.0f;
			}
			const float t = std::clamp((value - from) / (to - from), 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		}

		// To the nearest of `steps` a unit
		float Snap(float value, float steps)
		{
			return std::round(value * steps) / steps;
		}

		template<typename T>
		void AddToHash(uint64_t& hash, const T& value)
		{
			unsigned char bytes[sizeof(value)];
			std::memcpy(bytes, &value, sizeof(value));
			for (const unsigned char byte : bytes)
			{
				hash = (hash ^ byte) * 1099511628211ull;
			}
		}

		void AddMatrixToHash(uint64_t& hash, const glm::mat4& matrix)
		{
			for (int column = 0; column < 4; ++column)
			{
				for (int row = 0; row < 4; ++row)
				{
					AddToHash(hash, matrix[column][row]);
				}
			}
		}

		// The points kept so far, in squares over a rectangle at least the spacing a side, so a new
		// point only looks at those in its own square and the eight around it. Points outside the
		// rectangle count in the squares at its edge.
		class SpacingGrid
		{
		public:
			SpacingGrid(const glm::vec2& min, const glm::vec2& max, float spacing)
				: Min(min)
				, SpacingSquared(spacing * spacing)
			{
				const glm::vec2 extent = glm::max(max - min, glm::vec2(0.01f));
				SquareSize = std::max(spacing, std::max(extent.x, extent.y) / static_cast<float>(MaxSpacingSquares));
				Columns = std::clamp(static_cast<uint32_t>(std::ceil(extent.x / SquareSize)), 1u, MaxSpacingSquares);
				Rows = std::clamp(static_cast<uint32_t>(std::ceil(extent.y / SquareSize)), 1u, MaxSpacingSquares);
				Heads.assign(static_cast<size_t>(Columns) * Rows, NoPoint);
			}

			bool IsFree(const glm::vec3& point) const
			{
				const int32_t column = static_cast<int32_t>(GetColumn(point.x));
				const int32_t row = static_cast<int32_t>(GetRow(point.z));
				for (int32_t z = std::max(row - 1, 0); z <= std::min(row + 1, static_cast<int32_t>(Rows) - 1); ++z)
				{
					for (int32_t x = std::max(column - 1, 0); x <= std::min(column + 1, static_cast<int32_t>(Columns) - 1); ++x)
					{
						for (uint32_t other = Heads[static_cast<size_t>(z) * Columns + x]; other != NoPoint; other = Next[other])
						{
							const glm::vec3 between = Points[other] - point;
							if (glm::dot(between, between) < SpacingSquared)
							{
								return false;
							}
						}
					}
				}
				return true;
			}

			void Add(const glm::vec3& point)
			{
				const size_t square = static_cast<size_t>(GetRow(point.z)) * Columns + GetColumn(point.x);
				Next.push_back(Heads[square]);
				Heads[square] = static_cast<uint32_t>(Points.size());
				Points.push_back(point);
			}

		private:
			uint32_t GetColumn(float x) const
			{
				return std::min(static_cast<uint32_t>(std::max(std::floor((x - Min.x) / SquareSize), 0.0f)), Columns - 1);
			}

			uint32_t GetRow(float z) const
			{
				return std::min(static_cast<uint32_t>(std::max(std::floor((z - Min.y) / SquareSize), 0.0f)), Rows - 1);
			}

			glm::vec2 Min;
			float SpacingSquared = 0.0f;
			float SquareSize = 1.0f;
			uint32_t Columns = 1;
			uint32_t Rows = 1;
			// The last point added to each square, and for each point the one added there before it
			std::vector<uint32_t> Heads;
			std::vector<uint32_t> Next;
			std::vector<glm::vec3> Points;
		};

		// The scatter's limits, ready to test a point against
		struct ScatterLimits
		{
			glm::vec2 HalfSize{ 0.0f };
			// The normal's Y between these: the cosines of the steepest and of the flattest slope
			float LowestUp = -1.0f;
			float HighestUp = 1.0f;
			glm::vec2 Heights{ 0.0f };

			explicit ScatterLimits(const ScatterComponent& scatter)
				: HalfSize(GetHalfSize(scatter))
				, Heights(Ordered(scatter.HeightRange))
			{
				const glm::vec2 slopes = glm::clamp(Ordered(scatter.SlopeRange), glm::vec2(0.0f), glm::vec2(180.0f));
				HighestUp = GetSineCosine(ToRadians(slopes.x)).Cosine;
				LowestUp = GetSineCosine(ToRadians(slopes.y)).Cosine;
			}

			bool Accepts(const glm::vec3& point, const glm::vec3& normal) const
			{
				return std::abs(point.x) <= HalfSize.x && std::abs(point.z) <= HalfSize.y && normal.y >= LowestUp && normal.y <= HighestUp &&
					point.y >= Heights.x && point.y <= Heights.y;
			}
		};

		// How much of the density is left at a point of a cell: the noise mask's share, times each
		// blocker's that reaches the cell, in the blockers' order. A painted map over the area is one
		// more factor here, when it comes.
		struct ScatterDensity
		{
			const ScatterComponent& Scatter;
			std::vector<const ScatterBlocker*> Blockers;

			float At(const glm::vec3& point) const
			{
				float density = GetNoiseMaskDensity(Scatter, point.x, point.z);
				for (const ScatterBlocker* blocker : Blockers)
				{
					if (!(density > 0.0f))
					{
						break;
					}
					density *= GetBlockerDensity(*blocker, point);
				}
				return density;
			}
		};

		// Where a kept point's copy goes: scaled, turned about its up, tilted to the normal, sunk
		glm::mat4 MakeInstanceTransform(const ScatterComponent& scatter, const glm::vec3& point, const glm::vec3& normal, uint32_t seed, uint32_t pointNumber)
		{
			const glm::vec2 scales = Ordered(scatter.ScaleRange);
			const float scale = scales.x + (scales.y - scales.x) * ToUnit(HashPoint(seed, pointNumber, 4));
			const float yaw = ToRadians(scatter.YawRange) * (ToUnit(HashPoint(seed, pointNumber, 5)) - 0.5f);
			const SineCosine turn = GetSineCosine(yaw);

			const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
			const glm::vec3 tilted = worldUp + (normal - worldUp) * std::clamp(scatter.AlignToNormal, 0.0f, 1.0f);
			const float tiltedLength = glm::length(tilted);
			const glm::vec3 up = tiltedLength > 1e-4f ? tilted / tiltedLength : worldUp;

			// X turned about Y by the yaw, then laid on the plane at right angles to up; Z turned the
			// same where up lies along it
			glm::vec3 tangent = glm::vec3(turn.Cosine, 0.0f, -turn.Sine);
			tangent -= up * glm::dot(up, tangent);
			if (glm::dot(tangent, tangent) < 1e-6f)
			{
				tangent = glm::vec3(turn.Sine, 0.0f, turn.Cosine);
				tangent -= up * glm::dot(up, tangent);
			}
			tangent = glm::normalize(tangent);
			const glm::vec3 bitangent = glm::cross(tangent, up);

			glm::mat4 transform(1.0f);
			transform[0] = glm::vec4(tangent * scale, 0.0f);
			transform[1] = glm::vec4(up * scale, 0.0f);
			transform[2] = glm::vec4(bitangent * scale, 0.0f);
			transform[3] = glm::vec4(point - up * scatter.SinkDepth, 1.0f);
			return transform;
		}

		// A corner of a polygon being cut, with its normal
		struct Corner
		{
			glm::vec3 Position{ 0.0f };
			glm::vec3 Normal{ 0.0f };
		};

		// The part of a polygon on one side of a line of X (axis 0) or Z (axis 2): Sutherland and
		// Hodgman's clipping. A triangle cut by four lines has at most seven corners.
		int ClipPolygon(const Corner* corners, int count, Corner* into, int axis, float bound, bool bKeepAbove)
		{
			int kept = 0;
			for (int corner = 0; corner < count; ++corner)
			{
				const Corner& from = corners[corner];
				const Corner& to = corners[(corner + 1) % count];
				const float fromSide = bKeepAbove ? from.Position[axis] - bound : bound - from.Position[axis];
				const float toSide = bKeepAbove ? to.Position[axis] - bound : bound - to.Position[axis];
				if (fromSide >= 0.0f)
				{
					into[kept++] = from;
				}
				if ((fromSide >= 0.0f) != (toSide >= 0.0f))
				{
					const float t = fromSide / (fromSide - toSide);
					into[kept++] = Corner{ from.Position + (to.Position - from.Position) * t, from.Normal + (to.Normal - from.Normal) * t };
				}
			}
			return kept;
		}

		float GetArea(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
		{
			return 0.5f * glm::length(glm::cross(b - a, c - a));
		}

		void AddPiece(ScatterCells::Cell& cell, const Corner& a, const Corner& b, const Corner& c)
		{
			if (GetArea(a.Position, b.Position, c.Position) > 0.0f)
			{
				cell.Pieces.push_back(ScatterCells::Piece{ { a.Position, b.Position, c.Position }, { a.Normal, b.Normal, c.Normal } });
			}
		}

		// The triangle's part in the cell, as pieces: whole if it lies inside, else cut along the
		// cell's edges and fanned out from its first corner
		void AddTriangle(ScatterCells::Cell& cell, const Corner (&triangle)[3], const glm::vec2& low, const glm::vec2& high)
		{
			if (low.x >= cell.Min.x && high.x <= cell.Max.x && low.y >= cell.Min.y && high.y <= cell.Max.y)
			{
				AddPiece(cell, triangle[0], triangle[1], triangle[2]);
				return;
			}
			Corner first[8];
			Corner second[8];
			int count = ClipPolygon(triangle, 3, first, 0, cell.Min.x, true);
			count = ClipPolygon(first, count, second, 0, cell.Max.x, false);
			count = ClipPolygon(second, count, first, 2, cell.Min.y, true);
			count = ClipPolygon(first, count, second, 2, cell.Max.y, false);
			for (int corner = 1; corner + 1 < count; ++corner)
			{
				AddPiece(cell, second[0], second[corner], second[corner + 1]);
			}
		}

		uint64_t HashPieces(const std::vector<ScatterCells::Piece>& pieces)
		{
			uint64_t hash = 14695981039346656037ull;
			AddToHash(hash, pieces.size());
			const size_t words = pieces.size() * sizeof(ScatterCells::Piece) / sizeof(uint32_t);
			const unsigned char* bytes = reinterpret_cast<const unsigned char*>(pieces.data());
			for (size_t word = 0; word < words; ++word)
			{
				uint32_t value = 0;
				std::memcpy(&value, bytes + word * sizeof(uint32_t), sizeof(value));
				hash = (hash ^ value) * 1099511628211ull;
			}
			return hash;
		}

		uint32_t GetColumn(const ScatterCells& cells, float x)
		{
			return std::min(static_cast<uint32_t>(std::max(std::floor((x + cells.HalfSize.x) / cells.CellSize), 0.0f)), cells.Columns - 1);
		}

		uint32_t GetRow(const ScatterCells& cells, float z)
		{
			return std::min(static_cast<uint32_t>(std::max(std::floor((z + cells.HalfSize.y) / cells.CellSize), 0.0f)), cells.Rows - 1);
		}

		// The area in squares of CellSize, the last ones cut off at its edges, nothing in them yet
		void SetUpCells(ScatterCells& cells, const ScatterComponent& scatter)
		{
			cells.HalfSize = GetHalfSize(scatter);
			cells.CellSize = std::max({ scatter.CellSize, 0.5f, 2.0f * cells.HalfSize.x / static_cast<float>(MaxCellsPerSide),
				2.0f * cells.HalfSize.y / static_cast<float>(MaxCellsPerSide) });
			cells.Columns = std::clamp(static_cast<uint32_t>(std::ceil(2.0f * cells.HalfSize.x / cells.CellSize)), 1u, MaxCellsPerSide);
			cells.Rows = std::clamp(static_cast<uint32_t>(std::ceil(2.0f * cells.HalfSize.y / cells.CellSize)), 1u, MaxCellsPerSide);
			cells.Cells.assign(static_cast<size_t>(cells.Columns) * cells.Rows, ScatterCells::Cell{});
			for (uint32_t row = 0; row < cells.Rows; ++row)
			{
				for (uint32_t column = 0; column < cells.Columns; ++column)
				{
					ScatterCells::Cell& cell = cells.Cells[static_cast<size_t>(row) * cells.Columns + column];
					cell.Min = -cells.HalfSize + glm::vec2(static_cast<float>(column), static_cast<float>(row)) * cells.CellSize;
					cell.Max.x = column + 1 == cells.Columns ? cells.HalfSize.x : std::min(cell.Min.x + cells.CellSize, cells.HalfSize.x);
					cell.Max.y = row + 1 == cells.Rows ? cells.HalfSize.y : std::min(cell.Min.y + cells.CellSize, cells.HalfSize.y);
				}
			}
		}

		// Cuts the surface's triangles into the cells, and marks those whose pieces changed
		void CutSurface(ScatterCells& cells, const MeshData& surface, const glm::mat4& surfaceToScatter, std::vector<uint8_t>& changed)
		{
			NYX_PROFILE_FUNCTION();
			// The vertices in the scatter's space; normals by the inverse transpose, so they stay at
			// right angles to the surface under any scale
			const glm::mat3 normalTransform = glm::transpose(glm::inverse(glm::mat3(surfaceToScatter)));
			std::vector<Corner> corners(surface.Vertices.size());
			for (size_t vertex = 0; vertex < surface.Vertices.size(); ++vertex)
			{
				corners[vertex].Position = glm::vec3(surfaceToScatter * glm::vec4(surface.Vertices[vertex].Position, 1.0f));
				corners[vertex].Normal = normalTransform * surface.Vertices[vertex].Normal;
			}

			std::vector<uint64_t> before(cells.Cells.size());
			for (size_t cell = 0; cell < cells.Cells.size(); ++cell)
			{
				before[cell] = cells.Cells[cell].SurfaceHash;
				cells.Cells[cell].Pieces.clear();
			}

			for (size_t index = 0; index + 2 < surface.Indices.size(); index += 3)
			{
				// A mesh that isn't valid (CheckMeshData()) loses the triangles that name no vertex
				if (std::max({ surface.Indices[index], surface.Indices[index + 1], surface.Indices[index + 2] }) >= corners.size())
				{
					continue;
				}
				const Corner triangle[3] = { corners[surface.Indices[index]], corners[surface.Indices[index + 1]], corners[surface.Indices[index + 2]] };
				const glm::vec2 low(std::min({ triangle[0].Position.x, triangle[1].Position.x, triangle[2].Position.x }),
					std::min({ triangle[0].Position.z, triangle[1].Position.z, triangle[2].Position.z }));
				const glm::vec2 high(std::max({ triangle[0].Position.x, triangle[1].Position.x, triangle[2].Position.x }),
					std::max({ triangle[0].Position.z, triangle[1].Position.z, triangle[2].Position.z }));
				if (high.x < -cells.HalfSize.x || low.x > cells.HalfSize.x || high.y < -cells.HalfSize.y || low.y > cells.HalfSize.y ||
					!(GetArea(triangle[0].Position, triangle[1].Position, triangle[2].Position) > 0.0f))
				{
					continue;
				}
				for (uint32_t row = GetRow(cells, low.y); row <= GetRow(cells, high.y); ++row)
				{
					for (uint32_t column = GetColumn(cells, low.x); column <= GetColumn(cells, high.x); ++column)
					{
						AddTriangle(cells.Cells[static_cast<size_t>(row) * cells.Columns + column], triangle, low, high);
					}
				}
			}

			for (size_t index = 0; index < cells.Cells.size(); ++index)
			{
				ScatterCells::Cell& cell = cells.Cells[index];
				cell.AreasUpTo.resize(cell.Pieces.size());
				double total = 0.0;
				for (size_t piece = 0; piece < cell.Pieces.size(); ++piece)
				{
					total += static_cast<double>(GetArea(cell.Pieces[piece].Positions[0], cell.Pieces[piece].Positions[1], cell.Pieces[piece].Positions[2]));
					cell.AreasUpTo[piece] = total;
				}
				cell.SurfaceHash = HashPieces(cell.Pieces);
				if (cell.SurfaceHash != before[index])
				{
					changed[index] = 1;
				}
			}
		}

		bool Reaches(const ScatterBlocker& blocker, const ScatterCells::Cell& cell)
		{
			return blocker.ReachMin.x <= cell.Max.x && blocker.ReachMax.x >= cell.Min.x && blocker.ReachMin.y <= cell.Max.y && blocker.ReachMax.y >= cell.Min.y;
		}

		void MarkReached(const ScatterCells& cells, const ScatterBlocker& blocker, std::vector<uint8_t>& marked)
		{
			if (blocker.ReachMax.x < -cells.HalfSize.x || blocker.ReachMin.x > cells.HalfSize.x || blocker.ReachMax.y < -cells.HalfSize.y ||
				blocker.ReachMin.y > cells.HalfSize.y)
			{
				return;
			}
			// A cell more each way than its corners say, so the cells' own test decides
			const uint32_t firstRow = GetRow(cells, blocker.ReachMin.y);
			const uint32_t firstColumn = GetColumn(cells, blocker.ReachMin.x);
			const uint32_t lastRow = std::min(GetRow(cells, blocker.ReachMax.y) + 1, cells.Rows - 1);
			const uint32_t lastColumn = std::min(GetColumn(cells, blocker.ReachMax.x) + 1, cells.Columns - 1);
			for (uint32_t row = firstRow > 0 ? firstRow - 1 : 0; row <= lastRow; ++row)
			{
				for (uint32_t column = firstColumn > 0 ? firstColumn - 1 : 0; column <= lastColumn; ++column)
				{
					const size_t index = static_cast<size_t>(row) * cells.Columns + column;
					if (Reaches(blocker, cells.Cells[index]))
					{
						marked[index] = 1;
					}
				}
			}
		}

		// Marks the cells of the blockers in one list and not the other, by their hashes: those added,
		// removed or changed, before and after
		void MarkChangedBlockers(const ScatterCells& cells, std::span<const ScatterBlocker> before, std::span<const ScatterBlocker> after, std::vector<uint8_t>& marked)
		{
			const auto sortedByHash = [](std::span<const ScatterBlocker> blockers)
			{
				std::vector<std::pair<uint64_t, const ScatterBlocker*>> sorted;
				sorted.reserve(blockers.size());
				for (const ScatterBlocker& blocker : blockers)
				{
					sorted.emplace_back(blocker.Hash, &blocker);
				}
				std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b)
					{
						return a.first < b.first;
					});
				return sorted;
			};
			const auto old = sortedByHash(before);
			const auto now = sortedByHash(after);
			size_t o = 0;
			size_t n = 0;
			while (o < old.size() || n < now.size())
			{
				if (n == now.size() || (o < old.size() && old[o].first < now[n].first))
				{
					MarkReached(cells, *old[o++].second, marked);
				}
				else if (o == old.size() || now[n].first < old[o].first)
				{
					MarkReached(cells, *now[n++].second, marked);
				}
				else
				{
					++o;
					++n;
				}
			}
		}

		// Throws the cell's points: density times its area, the fraction by a hash of its own, each
		// on a piece picked by its area, kept if within the limits, the density the masks leave and
		// the spacing within the cell
		void ThrowPoints(ScatterCells& cells, uint32_t column, uint32_t row, const ScatterComponent& scatter, const ScatterLimits& limits, uint32_t maxPoints, uint32_t maxKept)
		{
			ScatterCells::Cell& cell = cells.Cells[static_cast<size_t>(row) * cells.Columns + column];
			cell.Points.clear();
			cell.Thrown.clear();
			if (cell.Pieces.empty() || !(scatter.Density > 0.0f))
			{
				return;
			}

			ScatterDensity density{ .Scatter = scatter };
			for (const ScatterBlocker& blocker : cells.Blockers)
			{
				if (Reaches(blocker, cell))
				{
					density.Blockers.push_back(&blocker);
				}
			}

			const uint32_t seed = HashCell(scatter.Seed, column, row);
			const double totalArea = cell.AreasUpTo.back();
			const double wanted = static_cast<double>(scatter.Density) * totalArea;
			const double whole = std::floor(wanted);
			uint32_t points = maxPoints;
			if (whole < static_cast<double>(maxPoints))
			{
				points = static_cast<uint32_t>(whole) + (ToUnitDouble(HashPoint(seed, NoPoint, 6)) < wanted - whole ? 1u : 0u);
				points = std::min(points, maxPoints);
			}

			std::optional<SpacingGrid> spacing;
			if (scatter.Spacing > 0.0f)
			{
				spacing.emplace(cell.Min, cell.Max, scatter.Spacing);
			}

			for (uint32_t point = 0; point < points && cell.Thrown.size() < maxKept; ++point)
			{
				// A piece by its area, then a point on it, evenly over its area
				const double pick = ToUnitDouble(HashPoint(seed, point, 0)) * totalArea;
				const size_t picked = std::min(static_cast<size_t>(std::upper_bound(cell.AreasUpTo.begin(), cell.AreasUpTo.end(), pick) - cell.AreasUpTo.begin()), cell.Pieces.size() - 1);
				const ScatterCells::Piece& piece = cell.Pieces[picked];
				const float root = std::sqrt(ToUnit(HashPoint(seed, point, 1)));
				const float along = ToUnit(HashPoint(seed, point, 2));
				const float weightA = 1.0f - root;
				const float weightB = root * (1.0f - along);
				const float weightC = root * along;
				const glm::vec3 position = piece.Positions[0] * weightA + piece.Positions[1] * weightB + piece.Positions[2] * weightC;

				glm::vec3 normal = piece.Normals[0] * weightA + piece.Normals[1] * weightB + piece.Normals[2] * weightC;
				if (glm::dot(normal, normal) < 1e-12f)
				{
					// No normals to go by: the piece's own
					normal = glm::cross(piece.Positions[1] - piece.Positions[0], piece.Positions[2] - piece.Positions[0]);
				}
				normal = glm::normalize(normal);

				if (!limits.Accepts(position, normal) || !(ToUnit(HashPoint(seed, point, 3)) < density.At(position)) || (spacing && !spacing->IsFree(position)))
				{
					continue;
				}
				if (spacing)
				{
					spacing->Add(position);
				}
				cell.Points.push_back(position);
				cell.Thrown.push_back(MakeInstanceTransform(scatter, position, normal, seed, point));
			}
		}

		// How far past a cell's edges a point may lie within the spacing of one of its points, with
		// room for rounding
		float GetSpacingGrowth(float spacing)
		{
			return spacing * 1.01f + 1e-3f;
		}

		// How many cells a side a point may be within the spacing of
		uint32_t GetSpacingReach(const ScatterCells& cells, float spacing)
		{
			return spacing > 0.0f ? static_cast<uint32_t>(std::ceil(GetSpacingGrowth(spacing) / cells.CellSize)) : 0u;
		}

		// Keeps the cell's points that are at least the spacing from every point thrown in the cells
		// before it (in rows, then columns) nearby: of two too close in two cells, the later cell's
		// goes. It looks at what those cells threw, not what they kept, so it doesn't depend on theirs.
		void KeepApart(ScatterCells& cells, uint32_t column, uint32_t row, float spacing, uint32_t reach)
		{
			ScatterCells::Cell& cell = cells.Cells[static_cast<size_t>(row) * cells.Columns + column];
			if (reach == 0 || cell.Points.empty())
			{
				cell.Kept = cell.Thrown;
				return;
			}
			const glm::vec2 grown = glm::vec2(GetSpacingGrowth(spacing));
			const glm::vec2 low = cell.Min - grown;
			const glm::vec2 high = cell.Max + grown;
			SpacingGrid before(low, high, spacing);
			bool bAny = false;
			for (uint32_t otherRow = row > reach ? row - reach : 0; otherRow <= row; ++otherRow)
			{
				const uint32_t lastColumn = otherRow == row ? column : std::min(column + reach + 1, cells.Columns);
				for (uint32_t otherColumn = column > reach ? column - reach : 0; otherColumn < lastColumn; ++otherColumn)
				{
					for (const glm::vec3& point : cells.Cells[static_cast<size_t>(otherRow) * cells.Columns + otherColumn].Points)
					{
						if (point.x >= low.x && point.x <= high.x && point.z >= low.y && point.z <= high.y)
						{
							before.Add(point);
							bAny = true;
						}
					}
				}
			}
			if (!bAny)
			{
				cell.Kept = cell.Thrown;
				return;
			}
			cell.Kept.clear();
			for (size_t point = 0; point < cell.Points.size(); ++point)
			{
				if (before.IsFree(cell.Points[point]))
				{
					cell.Kept.push_back(cell.Thrown[point]);
				}
			}
		}

		glm::mat4 GetTransform(const Registry& world, Entity entity)
		{
			return world.Has<TransformComponent>(entity) ? world.Get<TransformComponent>(entity).ToMatrix() : glm::mat4(1.0f);
		}

		// A blocker's values as the scatters take them, before they are placed in a scatter's space
		struct BlockerValues
		{
			uint64_t Key = 0;
			ScatterBlocker::EKind Kind = ScatterBlocker::EKind::Circle;
			glm::vec3 Position{ 0.0f };
			glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
			glm::vec3 Size{ 0.0f };
			float Margin = 0.0f;
			float Falloff = 0.0f;
		};

		BlockerValues DescribeBlocker(const Registry& world, Entity entity, const ScatterBlockerComponent& component)
		{
			BlockerValues values;
			values.Key = world.Has<GuidComponent>(entity) ? world.Get<GuidComponent>(entity).Guid.Value : (entity.Value | (1ull << 63));
			values.Margin = std::max(component.Margin, 0.0f);
			values.Falloff = std::max(component.Falloff, 0.0f);

			TransformComponent transform;
			if (world.Has<TransformComponent>(entity))
			{
				transform = world.Get<TransformComponent>(entity);
			}
			values.Position = glm::vec3(Snap(transform.Position.x, BlockerPositionSteps), Snap(transform.Position.y, BlockerPositionSteps), Snap(transform.Position.z, BlockerPositionSteps));
			const glm::vec3 scale = glm::abs(glm::vec3(Snap(transform.Scale.x, BlockerScaleSteps), Snap(transform.Scale.y, BlockerScaleSteps), Snap(transform.Scale.z, BlockerScaleSteps)));
			glm::quat rotation(Snap(transform.Rotation.w, BlockerRotationSteps), Snap(transform.Rotation.x, BlockerRotationSteps), Snap(transform.Rotation.y, BlockerRotationSteps),
				Snap(transform.Rotation.z, BlockerRotationSteps));
			const float rotationLength = std::sqrt(rotation.w * rotation.w + rotation.x * rotation.x + rotation.y * rotation.y + rotation.z * rotation.z);
			rotation = rotationLength > 1e-6f ? glm::quat(rotation.w / rotationLength, rotation.x / rotationLength, rotation.y / rotationLength, rotation.z / rotationLength)
											  : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

			// One collider an entity, the same one the physics takes, scaled the same way
			const bool bCollider = component.Shape == EScatterBlockerShape::Collider;
			if (bCollider && world.Has<BoxColliderComponent>(entity))
			{
				values.Kind = ScatterBlocker::EKind::Box;
				values.Size = world.Get<BoxColliderComponent>(entity).HalfExtents * scale;
			}
			else if (bCollider && world.Has<SphereColliderComponent>(entity))
			{
				values.Kind = ScatterBlocker::EKind::Sphere;
				values.Size.x = world.Get<SphereColliderComponent>(entity).Radius * std::max({ scale.x, scale.y, scale.z });
			}
			else if (bCollider && world.Has<CapsuleColliderComponent>(entity))
			{
				const CapsuleColliderComponent& capsule = world.Get<CapsuleColliderComponent>(entity);
				values.Kind = ScatterBlocker::EKind::Capsule;
				values.Size = glm::vec3(capsule.Radius * std::max(scale.x, scale.z), capsule.HalfHeight * scale.y, 0.0f);
			}
			else
			{
				values.Kind = ScatterBlocker::EKind::Circle;
				values.Size.x = component.Radius * std::max(scale.x, scale.z);
			}
			values.Size = glm::max(values.Size, glm::vec3(0.0f));
			// A circle stands along each scatter's up: turning it changes nothing
			values.Rotation = values.Kind == ScatterBlocker::EKind::Circle ? glm::quat(1.0f, 0.0f, 0.0f, 0.0f) : rotation;
			return values;
		}

		uint64_t HashBlockerValues(const BlockerValues& values, uint64_t scatterHash)
		{
			uint64_t hash = scatterHash;
			AddToHash(hash, values.Key);
			AddToHash(hash, values.Kind);
			for (const float value : { values.Position.x, values.Position.y, values.Position.z, values.Rotation.w, values.Rotation.x, values.Rotation.y, values.Rotation.z,
					 values.Size.x, values.Size.y, values.Size.z, values.Margin, values.Falloff })
			{
				AddToHash(hash, value);
			}
			return hash;
		}

		// The blockers' values in their order, with their hashes, the scatter's transform in them
		std::vector<std::pair<BlockerValues, uint64_t>> DescribeBlockers(const Registry& world, const glm::mat4& scatterToWorld)
		{
			uint64_t scatterHash = 14695981039346656037ull;
			AddMatrixToHash(scatterHash, scatterToWorld);
			std::vector<std::pair<BlockerValues, uint64_t>> blockers;
			world.Each<ScatterBlockerComponent>(
				[&](Entity entity, const ScatterBlockerComponent& component)
				{
					const BlockerValues values = DescribeBlocker(world, entity, component);
					blockers.emplace_back(values, HashBlockerValues(values, scatterHash));
				});
			std::sort(blockers.begin(), blockers.end(), [](const auto& a, const auto& b)
				{
					return a.first.Key != b.first.Key ? a.first.Key < b.first.Key : a.second < b.second;
				});
			return blockers;
		}

		// The rotation and translation's inverse: the rotation turned back
		glm::mat4 InverseOfRigid(const glm::mat4& rigid)
		{
			const glm::mat3 back = glm::transpose(glm::mat3(rigid));
			glm::mat4 inverse(back);
			inverse[3] = glm::vec4(-(back * glm::vec3(rigid[3])), 1.0f);
			return inverse;
		}

		ScatterBlocker PlaceBlocker(const BlockerValues& values, uint64_t hash, const glm::mat4& scatterToWorld)
		{
			ScatterBlocker blocker{ .Key = values.Key, .Kind = values.Kind, .Size = values.Size, .Margin = values.Margin, .Falloff = values.Falloff, .Hash = hash };
			const float reach = values.Margin + values.Falloff;
			if (values.Kind == ScatterBlocker::EKind::Circle)
			{
				// Along the scatter's axes, round the blocker, in metres
				glm::vec3 lengths(1.0f);
				glm::mat4 shapeToWorld(1.0f);
				for (int axis = 0; axis < 3; ++axis)
				{
					const glm::vec3 direction(scatterToWorld[axis]);
					lengths[axis] = glm::length(direction);
					shapeToWorld[axis] = lengths[axis] > 1e-12f ? glm::vec4(direction / lengths[axis], 0.0f) : glm::vec4(0.0f);
				}
				shapeToWorld[3] = glm::vec4(values.Position, 1.0f);
				blocker.ScatterToShape = InverseOfRigid(shapeToWorld) * scatterToWorld;
				const glm::vec3 center(glm::inverse(blocker.ScatterToShape)[3]);
				const glm::vec2 across = glm::vec2(values.Size.x + reach) / glm::max(glm::vec2(lengths.x, lengths.z), glm::vec2(1e-6f));
				blocker.ReachMin = glm::vec2(center.x, center.z) - across - BlockerReachPadding;
				blocker.ReachMax = glm::vec2(center.x, center.z) + across + BlockerReachPadding;
				return blocker;
			}

			const glm::mat4 shapeToWorld = glm::translate(glm::mat4(1.0f), values.Position) * glm::toMat4(values.Rotation);
			blocker.ScatterToShape = InverseOfRigid(shapeToWorld) * scatterToWorld;
			glm::vec3 extent = values.Size + reach;
			if (values.Kind == ScatterBlocker::EKind::Sphere)
			{
				extent = glm::vec3(values.Size.x + reach);
			}
			else if (values.Kind == ScatterBlocker::EKind::Capsule)
			{
				extent = glm::vec3(values.Size.x + reach, values.Size.y + values.Size.x + reach, values.Size.x + reach);
			}
			const glm::mat4 shapeToScatter = glm::inverse(blocker.ScatterToShape);
			blocker.ReachMin = glm::vec2(std::numeric_limits<float>::max());
			blocker.ReachMax = glm::vec2(-std::numeric_limits<float>::max());
			for (int corner = 0; corner < 8; ++corner)
			{
				const glm::vec3 offset((corner & 1) ? extent.x : -extent.x, (corner & 2) ? extent.y : -extent.y, (corner & 4) ? extent.z : -extent.z);
				const glm::vec3 inScatter(shapeToScatter * glm::vec4(offset, 1.0f));
				blocker.ReachMin = glm::min(blocker.ReachMin, glm::vec2(inScatter.x, inScatter.z));
				blocker.ReachMax = glm::max(blocker.ReachMax, glm::vec2(inScatter.x, inScatter.z));
			}
			blocker.ReachMin -= BlockerReachPadding;
			blocker.ReachMax += BlockerReachPadding;
			return blocker;
		}

		// What a scatter stands on now
		enum class EScatterGround : uint8_t
		{
			// Its surface has no mesh, or is no entity of the world
			Nothing,
			Plane,
			Mesh
		};
	}

	std::vector<ScatterBlocker> GatherScatterBlockers(const Registry& world, const glm::mat4& scatterToWorld)
	{
		std::vector<ScatterBlocker> blockers;
		for (const auto& [values, hash] : DescribeBlockers(world, scatterToWorld))
		{
			blockers.push_back(PlaceBlocker(values, hash, scatterToWorld));
		}
		return blockers;
	}

	float GetBlockerDensity(const ScatterBlocker& blocker, const glm::vec3& point)
	{
		const glm::vec3 inShape(blocker.ScatterToShape * glm::vec4(point, 1.0f));
		float distance = 0.0f;
		switch (blocker.Kind)
		{
		case ScatterBlocker::EKind::Box:
		{
			// Outside, to the nearest point of the box; inside, less than 0
			const glm::vec3 beyond = glm::abs(inShape) - blocker.Size;
			distance = glm::length(glm::max(beyond, glm::vec3(0.0f))) + std::min(std::max({ beyond.x, beyond.y, beyond.z }), 0.0f);
			break;
		}
		case ScatterBlocker::EKind::Sphere:
			distance = glm::length(inShape) - blocker.Size.x;
			break;
		case ScatterBlocker::EKind::Capsule:
		{
			const glm::vec3 fromMiddle(inShape.x, inShape.y - std::clamp(inShape.y, -blocker.Size.y, blocker.Size.y), inShape.z);
			distance = glm::length(fromMiddle) - blocker.Size.x;
			break;
		}
		default:
			distance = std::sqrt(inShape.x * inShape.x + inShape.z * inShape.z) - blocker.Size.x;
			break;
		}
		if (!(distance > blocker.Margin))
		{
			return 0.0f;
		}
		if (!(blocker.Falloff > 0.0f) || distance >= blocker.Margin + blocker.Falloff)
		{
			return 1.0f;
		}
		return SmoothStep(blocker.Margin, blocker.Margin + blocker.Falloff, distance);
	}

	float SampleScatterNoise(const ScatterComponent& scatter, float x, float z)
	{
		// Three layers, each twice as fine and half as strong, each with its own seed and shifted, so
		// their lattices' zeros don't line up
		float sum = 0.0f;
		float weights = 0.0f;
		float weight = 1.0f;
		float frequency = std::max(scatter.NoiseFrequency, 0.0f);
		for (uint32_t octave = 0; octave < 3; ++octave)
		{
			const float shift = 17.31f * static_cast<float>(octave);
			sum += weight * SampleGradientNoise(x * frequency + shift, z * frequency - shift, (scatter.NoiseSeed ^ 0xA511E9B3u) + octave * 0x632BE5ABu);
			weights += weight;
			weight *= 0.5f;
			frequency *= 2.0f;
		}
		return 0.5f + 0.5f * sum / weights;
	}

	float GetNoiseMaskDensity(const ScatterComponent& scatter, float x, float z)
	{
		if (!scatter.bNoiseMask)
		{
			return 1.0f;
		}
		const float halfSoftness = 0.5f * std::max(scatter.NoiseSoftness, 0.0f);
		const float kept = SmoothStep(scatter.NoiseThreshold - halfSoftness, scatter.NoiseThreshold + halfSoftness, SampleScatterNoise(scatter, x, z));
		return scatter.bNoiseInvert ? 1.0f - kept : kept;
	}

	bool UpdateScatterCells(ScatterCells& cells, const ScatterComponent& scatter, const MeshData& surface, const glm::mat4& surfaceToScatter, uint64_t surfaceKey,
		std::span<const ScatterBlocker> blockers)
	{
		NYX_PROFILE_FUNCTION();
		cells.CellsThrown = 0;
		cells.CellsKeptApart = 0;
		const uint64_t values = HashScatterValues(scatter);
		std::vector<uint8_t> marked;
		if (!cells.bMade || cells.Values != values)
		{
			SetUpCells(cells, scatter);
			marked.assign(cells.Cells.size(), 1);
			CutSurface(cells, surface, surfaceToScatter, marked);
		}
		else
		{
			marked.assign(cells.Cells.size(), 0);
			if (cells.Surface != surfaceKey)
			{
				CutSurface(cells, surface, surfaceToScatter, marked);
			}
			MarkChangedBlockers(cells, cells.Blockers, blockers, marked);
		}
		cells.bMade = true;
		cells.Values = values;
		cells.Surface = surfaceKey;
		cells.Blockers.assign(blockers.begin(), blockers.end());
		if (std::find(marked.begin(), marked.end(), uint8_t{ 1 }) == marked.end())
		{
			return false;
		}

		// At most a cell's share of the instances and points each, so that no cell depends on how
		// many the others have
		const ScatterLimits limits(scatter);
		const uint32_t cellCount = static_cast<uint32_t>(cells.Cells.size());
		const uint32_t maxPoints = std::max(MaxScatterPoints / cellCount, 1u);
		const uint32_t maxKept = std::max(MaxScatterInstances / cellCount, 1u);
		for (uint32_t row = 0; row < cells.Rows; ++row)
		{
			for (uint32_t column = 0; column < cells.Columns; ++column)
			{
				if (marked[static_cast<size_t>(row) * cells.Columns + column])
				{
					ThrowPoints(cells, column, row, scatter, limits, maxPoints, maxKept);
					++cells.CellsThrown;
				}
			}
		}

		// Each cell thrown again, and those after it within the spacing, keep apart from the cells
		// before them again
		const uint32_t reach = GetSpacingReach(cells, scatter.Spacing);
		std::vector<uint8_t> keepApart = marked;
		for (uint32_t row = 0; row < cells.Rows; ++row)
		{
			for (uint32_t column = 0; column < cells.Columns; ++column)
			{
				if (!marked[static_cast<size_t>(row) * cells.Columns + column])
				{
					continue;
				}
				for (uint32_t laterRow = row; laterRow <= std::min(row + reach, cells.Rows - 1); ++laterRow)
				{
					const uint32_t firstColumn = laterRow == row ? column + 1 : (column > reach ? column - reach : 0);
					for (uint32_t laterColumn = firstColumn; laterColumn <= std::min(column + reach, cells.Columns - 1); ++laterColumn)
					{
						keepApart[static_cast<size_t>(laterRow) * cells.Columns + laterColumn] = 1;
					}
				}
			}
		}
		for (uint32_t row = 0; row < cells.Rows; ++row)
		{
			for (uint32_t column = 0; column < cells.Columns; ++column)
			{
				if (keepApart[static_cast<size_t>(row) * cells.Columns + column])
				{
					KeepApart(cells, column, row, scatter.Spacing, reach);
					++cells.CellsKeptApart;
				}
			}
		}
		return true;
	}

	MeshInstances CollectScatterInstances(const ScatterCells& cells)
	{
		MeshInstances instances;
		size_t count = 0;
		for (const ScatterCells::Cell& cell : cells.Cells)
		{
			count += cell.Kept.size();
		}
		instances.Transforms.reserve(count);
		for (const ScatterCells::Cell& cell : cells.Cells)
		{
			if (cell.Kept.empty())
			{
				continue;
			}
			MeshInstanceCell bounds{ .BoundsMin = glm::vec3(cell.Kept.front()[3]), .BoundsMax = glm::vec3(cell.Kept.front()[3]),
				.First = static_cast<uint32_t>(instances.Transforms.size()), .Count = static_cast<uint32_t>(cell.Kept.size()) };
			for (const glm::mat4& transform : cell.Kept)
			{
				bounds.BoundsMin = glm::min(bounds.BoundsMin, glm::vec3(transform[3]));
				bounds.BoundsMax = glm::max(bounds.BoundsMax, glm::vec3(transform[3]));
			}
			instances.Transforms.insert(instances.Transforms.end(), cell.Kept.begin(), cell.Kept.end());
			instances.Cells.push_back(bounds);
		}
		return instances;
	}

	MeshInstances ScatterOnMesh(const ScatterComponent& scatter, const MeshData& surface, const glm::mat4& surfaceToScatter, std::span<const ScatterBlocker> blockers)
	{
		NYX_PROFILE_FUNCTION();
		ScatterCells cells;
		UpdateScatterCells(cells, scatter, surface, surfaceToScatter, 0, blockers);
		return CollectScatterInstances(cells);
	}

	MeshData MakeScatterPlane(const ScatterComponent& scatter)
	{
		const glm::vec2 halfSize = GetHalfSize(scatter);
		MeshData plane;
		for (const glm::vec2& corner : { glm::vec2(-1.0f, -1.0f), glm::vec2(1.0f, -1.0f), glm::vec2(1.0f, 1.0f), glm::vec2(-1.0f, 1.0f) })
		{
			plane.Vertices.push_back(Vertex{ .Position = glm::vec3(corner.x * halfSize.x, 0.0f, corner.y * halfSize.y), .Normal = glm::vec3(0.0f, 1.0f, 0.0f) });
		}
		plane.Indices = { 0, 3, 2, 2, 1, 0 };
		return plane;
	}

	MeshInstances ScatterOnPlane(const ScatterComponent& scatter, std::span<const ScatterBlocker> blockers)
	{
		return ScatterOnMesh(scatter, MakeScatterPlane(scatter), glm::mat4(1.0f), blockers);
	}

	uint64_t HashScatterValues(const ScatterComponent& scatter)
	{
		uint64_t hash = 14695981039346656037ull;
		AddToHash(hash, scatter.Surface.Value);
		for (const glm::vec2& pair : { scatter.Size, scatter.SlopeRange, scatter.HeightRange, scatter.ScaleRange })
		{
			AddToHash(hash, pair.x);
			AddToHash(hash, pair.y);
		}
		for (const float value : { scatter.Density, scatter.Spacing, scatter.YawRange, scatter.AlignToNormal, scatter.SinkDepth, scatter.CellSize, scatter.NoiseFrequency,
				 scatter.NoiseThreshold, scatter.NoiseSoftness })
		{
			AddToHash(hash, value);
		}
		for (const bool flag : { scatter.bUseBlockers, scatter.bNoiseMask, scatter.bNoiseInvert })
		{
			AddToHash(hash, flag);
		}
		AddToHash(hash, scatter.Seed);
		AddToHash(hash, scatter.NoiseSeed);
		return hash;
	}

	void UpdateScatter(ScatterComponent& scatter, Registry& world, Entity entity)
	{
		if (!world.IsAlive(entity))
		{
			return;
		}

		EScatterGround ground = EScatterGround::Plane;
		const RuntimeMeshComponent* surfaceMesh = nullptr;
		const glm::mat4 scatterToWorld = GetTransform(world, entity);
		glm::mat4 surfaceToScatter(1.0f);
		if (scatter.Surface.IsValid())
		{
			ground = EScatterGround::Nothing;
			const std::optional<Entity> surface = FindEntityByGuid(world, scatter.Surface);
			if (surface && world.Has<RuntimeMeshComponent>(*surface) && world.Get<RuntimeMeshComponent>(*surface).Mesh)
			{
				ground = EScatterGround::Mesh;
				surfaceMesh = &world.Get<RuntimeMeshComponent>(*surface);
				surfaceToScatter = glm::inverse(scatterToWorld) * GetTransform(world, *surface);
			}
		}

		// What the surface's pieces are cut from, and what the instances are made from: that, the
		// values and the blockers
		uint64_t surfaceKey = 14695981039346656037ull;
		AddToHash(surfaceKey, ground);
		if (surfaceMesh)
		{
			AddToHash(surfaceKey, surfaceMesh->Revision);
			AddMatrixToHash(surfaceKey, surfaceToScatter);
		}
		std::vector<std::pair<BlockerValues, uint64_t>> blockers;
		if (scatter.bUseBlockers && ground != EScatterGround::Nothing)
		{
			blockers = DescribeBlockers(world, scatterToWorld);
		}
		uint64_t madeFrom = HashScatterValues(scatter);
		AddToHash(madeFrom, surfaceKey);
		AddToHash(madeFrom, blockers.size());
		for (const auto& blocker : blockers)
		{
			AddToHash(madeFrom, blocker.second);
		}
		const std::shared_ptr<const MeshData> mesh = surfaceMesh ? surfaceMesh->Mesh : nullptr;
		const bool bHasInstances = world.Has<MeshInstancesComponent>(entity) && world.Get<MeshInstancesComponent>(entity).Instances;
		if (scatter.MadeFrom == madeFrom && scatter.MadeOn.lock() == mesh && bHasInstances)
		{
			return;
		}

		std::shared_ptr<MeshInstances> instances;
		if (ground == EScatterGround::Nothing)
		{
			instances = std::make_shared<MeshInstances>();
			scatter.Cells.reset();
		}
		else
		{
			// The cells say what they were made from, so any will do to start from; ones a copy of
			// the component shares are copied before they change
			if (!scatter.Cells)
			{
				scatter.Cells = std::make_shared<ScatterCells>();
			}
			else if (scatter.Cells.use_count() > 1)
			{
				scatter.Cells = std::make_shared<ScatterCells>(*scatter.Cells);
			}
			// Another mesh at the same revision: cut it again
			if (scatter.MadeOn.lock() != mesh)
			{
				scatter.Cells->Surface = ~surfaceKey;
			}

			std::vector<ScatterBlocker> placed;
			placed.reserve(blockers.size());
			for (const auto& [values, hash] : blockers)
			{
				placed.push_back(PlaceBlocker(values, hash, scatterToWorld));
			}
			const bool bChanged = ground == EScatterGround::Plane
				? UpdateScatterCells(*scatter.Cells, scatter, MakeScatterPlane(scatter), glm::mat4(1.0f), surfaceKey, placed)
				: UpdateScatterCells(*scatter.Cells, scatter, *mesh, surfaceToScatter, surfaceKey, placed);
			if (bChanged || !bHasInstances)
			{
				instances = std::make_shared<MeshInstances>(CollectScatterInstances(*scatter.Cells));
			}
		}

		if (instances)
		{
			MeshInstancesComponent& component = world.Has<MeshInstancesComponent>(entity) ? world.Get<MeshInstancesComponent>(entity)
																						  : world.Add<MeshInstancesComponent>(entity);
			SetMeshInstances(component, std::move(instances));
		}
		scatter.MadeFrom = madeFrom;
		scatter.MadeOn = mesh;
	}

	void UpdateScatters(Registry& world)
	{
		NYX_PROFILE_FUNCTION();
		world.Each<ScatterComponent>(
			[&world](Entity entity, ScatterComponent& scatter)
			{
				UpdateScatter(scatter, world, entity);
			});
	}

	void RemoveScatter(Registry& world, Entity entity)
	{
		world.Remove<MeshInstancesComponent>(entity);
		if (world.Has<ScatterComponent>(entity))
		{
			world.Get<ScatterComponent>(entity).Cells.reset();
		}
	}
}
