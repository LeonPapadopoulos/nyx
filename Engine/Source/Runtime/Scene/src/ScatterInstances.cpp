#include "NyxPCH.h"
#include "ScatterInstances.h"

#include "GuidComponent.h"
#include "Profiler.h"
#include "RuntimeMeshComponent.h"
#include "TransformComponent.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <optional>
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

		// The points kept so far, in squares over the area at least the spacing a side, so a new
		// point only looks at those in its own square and the eight around it
		class SpacingGrid
		{
		public:
			SpacingGrid(const glm::vec2& halfSize, float spacing)
				: HalfSize(halfSize)
				, SpacingSquared(spacing * spacing)
			{
				SquareSize = std::max(spacing, 2.0f * std::max(halfSize.x, halfSize.y) / static_cast<float>(MaxSpacingSquares));
				Columns = std::clamp(static_cast<uint32_t>(std::ceil(2.0f * halfSize.x / SquareSize)), 1u, MaxSpacingSquares);
				Rows = std::clamp(static_cast<uint32_t>(std::ceil(2.0f * halfSize.y / SquareSize)), 1u, MaxSpacingSquares);
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
				return std::min(static_cast<uint32_t>(std::max(std::floor((x + HalfSize.x) / SquareSize), 0.0f)), Columns - 1);
			}

			uint32_t GetRow(float z) const
			{
				return std::min(static_cast<uint32_t>(std::max(std::floor((z + HalfSize.y) / SquareSize), 0.0f)), Rows - 1);
			}

			glm::vec2 HalfSize;
			float SpacingSquared = 0.0f;
			float SquareSize = 1.0f;
			uint32_t Columns = 1;
			uint32_t Rows = 1;
			// The last point added to each square, and for each point the one added there before it
			std::vector<uint32_t> Heads;
			std::vector<uint32_t> Next;
			std::vector<glm::vec3> Points;
		};

		// The scatter's limits, ready to test a point against. Masks, such as painted maps or room
		// kept around other entities, are tests here too, when they come.
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

		// Where a kept point's copy goes: scaled, turned about its up, tilted to the normal, sunk
		glm::mat4 MakeInstanceTransform(const ScatterComponent& scatter, const glm::vec3& point, const glm::vec3& normal, uint32_t pointNumber)
		{
			const glm::vec2 scales = Ordered(scatter.ScaleRange);
			const float scale = scales.x + (scales.y - scales.x) * ToUnit(HashPoint(scatter.Seed, pointNumber, 4));
			const float yaw = ToRadians(scatter.YawRange) * (ToUnit(HashPoint(scatter.Seed, pointNumber, 5)) - 0.5f);
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

		// The transforms in cells of CellSize over the area, each cell's one after another in the
		// order they were kept; only cells with instances
		MeshInstances GroupIntoCells(const ScatterComponent& scatter, const std::vector<glm::mat4>& transforms)
		{
			const glm::vec2 halfSize = GetHalfSize(scatter);
			const float cellSize = std::max({ scatter.CellSize, 0.5f, 2.0f * halfSize.x / static_cast<float>(MaxCellsPerSide), 2.0f * halfSize.y / static_cast<float>(MaxCellsPerSide) });
			const uint32_t columns = std::clamp(static_cast<uint32_t>(std::ceil(2.0f * halfSize.x / cellSize)), 1u, MaxCellsPerSide);
			const uint32_t rows = std::clamp(static_cast<uint32_t>(std::ceil(2.0f * halfSize.y / cellSize)), 1u, MaxCellsPerSide);
			const auto cellOf = [&](const glm::mat4& transform)
			{
				const uint32_t column = std::min(static_cast<uint32_t>(std::max(std::floor((transform[3].x + halfSize.x) / cellSize), 0.0f)), columns - 1);
				const uint32_t row = std::min(static_cast<uint32_t>(std::max(std::floor((transform[3].z + halfSize.y) / cellSize), 0.0f)), rows - 1);
				return static_cast<size_t>(row) * columns + column;
			};

			// Counted, then each put after the cells before its own
			std::vector<uint32_t> firsts(static_cast<size_t>(columns) * rows + 1, 0);
			for (const glm::mat4& transform : transforms)
			{
				++firsts[cellOf(transform) + 1];
			}
			for (size_t cell = 1; cell < firsts.size(); ++cell)
			{
				firsts[cell] += firsts[cell - 1];
			}

			MeshInstances instances;
			instances.Transforms.resize(transforms.size());
			std::vector<uint32_t> placed(firsts.begin(), firsts.end() - 1);
			for (const glm::mat4& transform : transforms)
			{
				instances.Transforms[placed[cellOf(transform)]++] = transform;
			}

			for (size_t cell = 0; cell + 1 < firsts.size(); ++cell)
			{
				const uint32_t first = firsts[cell];
				const uint32_t count = firsts[cell + 1] - first;
				if (count == 0)
				{
					continue;
				}
				MeshInstanceCell bounds{ .BoundsMin = glm::vec3(instances.Transforms[first][3]), .BoundsMax = glm::vec3(instances.Transforms[first][3]), .First = first, .Count = count };
				for (uint32_t instance = first + 1; instance < first + count; ++instance)
				{
					const glm::vec3 origin(instances.Transforms[instance][3]);
					bounds.BoundsMin = glm::min(bounds.BoundsMin, origin);
					bounds.BoundsMax = glm::max(bounds.BoundsMax, origin);
				}
				instances.Cells.push_back(bounds);
			}
			return instances;
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

		glm::mat4 GetTransform(const Registry& world, Entity entity)
		{
			return world.Has<TransformComponent>(entity) ? world.Get<TransformComponent>(entity).ToMatrix() : glm::mat4(1.0f);
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

	MeshInstances ScatterOnMesh(const ScatterComponent& scatter, const MeshData& surface, const glm::mat4& surfaceToScatter)
	{
		NYX_PROFILE_FUNCTION();
		const ScatterLimits limits(scatter);

		// The vertices in the scatter's space; normals by the inverse transpose, so they stay at right
		// angles to the surface under any scale
		const glm::mat3 normalTransform = glm::transpose(glm::inverse(glm::mat3(surfaceToScatter)));
		std::vector<glm::vec3> positions(surface.Vertices.size());
		std::vector<glm::vec3> normals(surface.Vertices.size());
		for (size_t vertex = 0; vertex < surface.Vertices.size(); ++vertex)
		{
			positions[vertex] = glm::vec3(surfaceToScatter * glm::vec4(surface.Vertices[vertex].Position, 1.0f));
			normals[vertex] = normalTransform * surface.Vertices[vertex].Normal;
		}

		// The triangles that reach into the area, and their areas added up, to pick one by its area
		std::vector<uint32_t> triangles;
		std::vector<double> areasUpTo;
		double totalArea = 0.0;
		for (size_t index = 0; index + 2 < surface.Indices.size(); index += 3)
		{
			// A mesh that isn't valid (CheckMeshData()) loses the triangles that name no vertex
			if (std::max({ surface.Indices[index], surface.Indices[index + 1], surface.Indices[index + 2] }) >= positions.size())
			{
				continue;
			}
			const glm::vec3& a = positions[surface.Indices[index]];
			const glm::vec3& b = positions[surface.Indices[index + 1]];
			const glm::vec3& c = positions[surface.Indices[index + 2]];
			if (std::max({ a.x, b.x, c.x }) < -limits.HalfSize.x || std::min({ a.x, b.x, c.x }) > limits.HalfSize.x ||
				std::max({ a.z, b.z, c.z }) < -limits.HalfSize.y || std::min({ a.z, b.z, c.z }) > limits.HalfSize.y)
			{
				continue;
			}
			const float area = 0.5f * glm::length(glm::cross(b - a, c - a));
			if (!(area > 0.0f))
			{
				continue;
			}
			totalArea += static_cast<double>(area);
			triangles.push_back(static_cast<uint32_t>(index));
			areasUpTo.push_back(totalArea);
		}

		std::vector<glm::mat4> transforms;
		if (triangles.empty() || !(scatter.Density > 0.0f))
		{
			return GroupIntoCells(scatter, transforms);
		}

		const double wanted = std::floor(static_cast<double>(scatter.Density) * totalArea + 0.5);
		const uint32_t points = static_cast<uint32_t>(std::min(wanted, static_cast<double>(MaxScatterPoints)));
		std::optional<SpacingGrid> spacing;
		if (scatter.Spacing > 0.0f)
		{
			spacing.emplace(limits.HalfSize, scatter.Spacing);
		}

		for (uint32_t point = 0; point < points && transforms.size() < MaxScatterInstances; ++point)
		{
			// A triangle by its area, then a point on it, evenly over its area
			const double pick = ToUnitDouble(HashPoint(scatter.Seed, point, 0)) * totalArea;
			const size_t picked = std::min(static_cast<size_t>(std::upper_bound(areasUpTo.begin(), areasUpTo.end(), pick) - areasUpTo.begin()), triangles.size() - 1);
			const uint32_t index = triangles[picked];
			const uint32_t ia = surface.Indices[index];
			const uint32_t ib = surface.Indices[index + 1];
			const uint32_t ic = surface.Indices[index + 2];
			const float root = std::sqrt(ToUnit(HashPoint(scatter.Seed, point, 1)));
			const float along = ToUnit(HashPoint(scatter.Seed, point, 2));
			const float weightA = 1.0f - root;
			const float weightB = root * (1.0f - along);
			const float weightC = root * along;
			const glm::vec3 position = positions[ia] * weightA + positions[ib] * weightB + positions[ic] * weightC;

			glm::vec3 normal = normals[ia] * weightA + normals[ib] * weightB + normals[ic] * weightC;
			if (glm::dot(normal, normal) < 1e-12f)
			{
				// No normals to go by: the triangle's own
				normal = glm::cross(positions[ib] - positions[ia], positions[ic] - positions[ia]);
			}
			normal = glm::normalize(normal);

			if (!limits.Accepts(position, normal) || (spacing && !spacing->IsFree(position)))
			{
				continue;
			}
			if (spacing)
			{
				spacing->Add(position);
			}
			transforms.push_back(MakeInstanceTransform(scatter, position, normal, point));
		}
		return GroupIntoCells(scatter, transforms);
	}

	MeshInstances ScatterOnPlane(const ScatterComponent& scatter)
	{
		const glm::vec2 halfSize = GetHalfSize(scatter);
		MeshData plane;
		for (const glm::vec2& corner : { glm::vec2(-1.0f, -1.0f), glm::vec2(1.0f, -1.0f), glm::vec2(1.0f, 1.0f), glm::vec2(-1.0f, 1.0f) })
		{
			plane.Vertices.push_back(Vertex{ .Position = glm::vec3(corner.x * halfSize.x, 0.0f, corner.y * halfSize.y), .Normal = glm::vec3(0.0f, 1.0f, 0.0f) });
		}
		plane.Indices = { 0, 3, 2, 2, 1, 0 };
		return ScatterOnMesh(scatter, plane, glm::mat4(1.0f));
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
		for (const float value : { scatter.Density, scatter.Spacing, scatter.YawRange, scatter.AlignToNormal, scatter.SinkDepth, scatter.CellSize })
		{
			AddToHash(hash, value);
		}
		AddToHash(hash, scatter.Seed);
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
		glm::mat4 surfaceToScatter(1.0f);
		if (scatter.Surface.IsValid())
		{
			ground = EScatterGround::Nothing;
			const std::optional<Entity> surface = FindEntityByGuid(world, scatter.Surface);
			if (surface && world.Has<RuntimeMeshComponent>(*surface) && world.Get<RuntimeMeshComponent>(*surface).Mesh)
			{
				ground = EScatterGround::Mesh;
				surfaceMesh = &world.Get<RuntimeMeshComponent>(*surface);
				surfaceToScatter = glm::inverse(GetTransform(world, entity)) * GetTransform(world, *surface);
			}
		}

		uint64_t madeFrom = HashScatterValues(scatter);
		AddToHash(madeFrom, ground);
		if (surfaceMesh)
		{
			AddToHash(madeFrom, surfaceMesh->Revision);
			for (int column = 0; column < 4; ++column)
			{
				for (int row = 0; row < 4; ++row)
				{
					AddToHash(madeFrom, surfaceToScatter[column][row]);
				}
			}
		}
		const std::shared_ptr<const MeshData> mesh = surfaceMesh ? surfaceMesh->Mesh : nullptr;
		if (scatter.MadeFrom == madeFrom && scatter.MadeOn.lock() == mesh && world.Has<MeshInstancesComponent>(entity) &&
			world.Get<MeshInstancesComponent>(entity).Instances)
		{
			return;
		}

		std::shared_ptr<MeshInstances> instances = std::make_shared<MeshInstances>();
		if (ground == EScatterGround::Plane)
		{
			*instances = ScatterOnPlane(scatter);
		}
		else if (mesh)
		{
			*instances = ScatterOnMesh(scatter, *mesh, surfaceToScatter);
		}
		MeshInstancesComponent& component = world.Has<MeshInstancesComponent>(entity) ? world.Get<MeshInstancesComponent>(entity)
																					  : world.Add<MeshInstancesComponent>(entity);
		SetMeshInstances(component, std::move(instances));
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
	}
}
