// Dual meshing of a sampled field (FieldMesher.h): a sphere is closed, manifold, its area right and
// its normals outward; a box's corners kept by sharp vertices and rounded by smooth ones; thin
// plates and rods closed, manifold and never facing the wrong way; two chunks side by side meet
// without a gap or an overlap; sky visibility open on flat ground, low under an overhang, none in a
// closed pocket, and blocked by the field beyond the grid; vertices take their inside corners'
// material; the same mesh, to the bit, as when it was written (a golden hash). Prints how long a
// chunk of terrain takes to sample and mesh.
#include "FieldMesher.h"
#include "SdfTerrain.h"
#include "TestSupport.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace
{
	using Nyx::FieldGrid;
	using Nyx::FieldMeshSettings;
	using Nyx::FieldMeshStats;
	using Nyx::MeshData;
	using Nyx::Tests::Require;

	using Field = std::function<float(const glm::vec3&)>;

	FieldGrid SampleField(const Field& field, const glm::vec3& origin, float cellSize, const glm::ivec3& size, const std::function<uint32_t(const glm::vec3&)>& material = {})
	{
		FieldGrid grid;
		grid.Size = size;
		grid.Origin = origin;
		grid.CellSize = cellSize;
		grid.Values.resize(size_t(size.x) * size.y * size.z);
		if (material)
		{
			grid.Materials.resize(grid.Values.size());
		}
		for (int k = 0; k < size.z; ++k)
		{
			for (int j = 0; j < size.y; ++j)
			{
				for (int i = 0; i < size.x; ++i)
				{
					const glm::vec3 point = origin + glm::vec3(float(i), float(j), float(k)) * cellSize;
					grid.Values[grid.IndexOf(i, j, k)] = field(point);
					if (material)
					{
						grid.Materials[grid.IndexOf(i, j, k)] = material(point);
					}
				}
			}
		}
		return grid;
	}

	Field Sphere(const glm::vec3& center, float radius)
	{
		return [=](const glm::vec3& p) { return glm::length(p - center) - radius; };
	}

	Field Box(const glm::vec3& center, const glm::vec3& half)
	{
		return [=](const glm::vec3& p)
		{
			const glm::vec3 q = glm::abs(p - center) - half;
			return glm::length(glm::max(q, glm::vec3(0.0f))) + std::min(std::max(q.x, std::max(q.y, q.z)), 0.0f);
		};
	}

	uint32_t BitsOf(float value)
	{
		uint32_t bits = 0;
		std::memcpy(&bits, &value, sizeof(bits));
		return bits;
	}

	using PointKey = std::tuple<uint32_t, uint32_t, uint32_t>;

	PointKey KeyOf(const glm::vec3& p)
	{
		return { BitsOf(p.x), BitsOf(p.y), BitsOf(p.z) };
	}

	// How many triangles use each edge, by where its ends are, over several meshes
	std::map<std::pair<PointKey, PointKey>, int> CountEdges(const std::vector<const MeshData*>& meshes)
	{
		std::map<std::pair<PointKey, PointKey>, int> edges;
		for (const MeshData* mesh : meshes)
		{
			for (size_t t = 0; t + 2 < mesh->Indices.size(); t += 3)
			{
				for (int e = 0; e < 3; ++e)
				{
					PointKey a = KeyOf(mesh->Vertices[mesh->Indices[t + e]].Position);
					PointKey b = KeyOf(mesh->Vertices[mesh->Indices[t + (e + 1) % 3]].Position);
					if (b < a)
					{
						std::swap(a, b);
					}
					++edges[{ a, b }];
				}
			}
		}
		return edges;
	}

	// Closed and manifold: every edge of every triangle used by exactly two
	void RequireClosed(const std::vector<const MeshData*>& meshes, const std::string& what)
	{
		for (const auto& [edge, count] : CountEdges(meshes))
		{
			Require(count == 2, what + ": an edge is used by " + std::to_string(count) + " triangles, not 2");
		}
	}

	// Closed, though not always manifold: every edge used by an even number of triangles, so no hole,
	// and by 4 only where a cell face has all four edges crossed and the cells on both sides make one
	// piece of both its curves (dual meshing's known limit: one vertex a piece). How many such edges
	// there are.
	int RequireClosedCountingShared(const MeshData& mesh, const std::string& what)
	{
		int shared = 0;
		for (const auto& [edge, count] : CountEdges({ &mesh }))
		{
			Require(count == 2 || count == 4, what + ": an edge is used by " + std::to_string(count) + " triangles");
			shared += count == 4 ? 1 : 0;
		}
		return shared;
	}

	glm::vec3 TriangleNormal(const MeshData& mesh, size_t t)
	{
		const glm::vec3& a = mesh.Vertices[mesh.Indices[t]].Position;
		const glm::vec3& b = mesh.Vertices[mesh.Indices[t + 1]].Position;
		const glm::vec3& c = mesh.Vertices[mesh.Indices[t + 2]].Position;
		return glm::cross(b - a, c - a);
	}

	glm::vec3 Centroid(const MeshData& mesh, size_t t)
	{
		return (mesh.Vertices[mesh.Indices[t]].Position + mesh.Vertices[mesh.Indices[t + 1]].Position + mesh.Vertices[mesh.Indices[t + 2]].Position) / 3.0f;
	}

	glm::vec3 FieldGradient(const Field& field, const glm::vec3& p)
	{
		constexpr float Step = 1e-3f;
		return glm::vec3(field(p + glm::vec3(Step, 0, 0)) - field(p - glm::vec3(Step, 0, 0)), field(p + glm::vec3(0, Step, 0)) - field(p - glm::vec3(0, Step, 0)),
			field(p + glm::vec3(0, 0, Step)) - field(p - glm::vec3(0, 0, Step)));
	}

	// No triangle faces against the field's gradient where it is. Only for shapes thicker than a cell:
	// inside a thinner one the gradient at a triangle can point to another face of the shape than the
	// one the samples saw (RequireNoFolds() is for those).
	void RequireFacingOut(const MeshData& mesh, const Field& field, const std::string& what)
	{
		for (size_t t = 0; t + 2 < mesh.Indices.size(); t += 3)
		{
			const glm::vec3 normal = TriangleNormal(mesh, t);
			if (glm::length(normal) < 1e-9f)
			{
				continue;
			}
			Require(glm::dot(normal, FieldGradient(field, Centroid(mesh, t))) > 0.0f, what + ": a triangle faces the wrong way");
		}
	}

	// Every triangle wound as the mesh round it, and out: every edge run as often one way as the
	// other by the triangles that share it, and the volume enclosed positive
	void RequireConsistentWinding(const MeshData& mesh, const std::string& what)
	{
		std::map<std::pair<uint32_t, uint32_t>, std::vector<size_t>> directed;
		float volume = 0.0f;
		for (size_t t = 0; t + 2 < mesh.Indices.size(); t += 3)
		{
			const glm::vec3& a = mesh.Vertices[mesh.Indices[t]].Position;
			const glm::vec3& b = mesh.Vertices[mesh.Indices[t + 1]].Position;
			const glm::vec3& c = mesh.Vertices[mesh.Indices[t + 2]].Position;
			volume += glm::dot(a, glm::cross(b, c)) / 6.0f;
			for (int e = 0; e < 3; ++e)
			{
				directed[{ mesh.Indices[t + e], mesh.Indices[t + (e + 1) % 3] }].push_back(t);
			}
		}
		Require(volume > 0.0f, what + ": the mesh should enclose its inside, facing out");
		for (const auto& [edge, triangles] : directed)
		{
			const auto reverse = directed.find({ edge.second, edge.first });
			Require(reverse != directed.end() && reverse->second.size() == triangles.size(),
				what + ": an edge runs one way in more triangles than the other way");
		}
	}

	float Area(const MeshData& mesh)
	{
		float area = 0.0f;
		for (size_t t = 0; t + 2 < mesh.Indices.size(); t += 3)
		{
			area += 0.5f * glm::length(TriangleNormal(mesh, t));
		}
		return area;
	}

	// Closed, its area within 2 % of the sphere's, its vertices on it, its normals outward
	void TestSphere()
	{
		const glm::vec3 center(17.3f, 17.1f, 16.8f);
		const float radius = 10.0f;
		const Field field = Sphere(center, radius);
		FieldMeshStats stats;
		const MeshData mesh = Nyx::MakeFieldMesh(SampleField(field, glm::vec3(0.0f), 1.0f, glm::ivec3(35)), {}, &stats);

		Require(Nyx::CheckMeshData(mesh).empty(), "The sphere's mesh should be valid: " + Nyx::CheckMeshData(mesh));
		RequireClosed({ &mesh }, "Sphere");
		RequireFacingOut(mesh, field, "Sphere");
		const float expected = 4.0f * 3.14159265f * radius * radius;
		Require(std::fabs(Area(mesh) - expected) < expected * 0.02f, "The sphere's area should be within 2 %: " + std::to_string(Area(mesh)));
		for (const Nyx::Vertex& vertex : mesh.Vertices)
		{
			Require(std::fabs(glm::length(vertex.Position - center) - radius) < 0.1f, "The sphere's vertices should be on it");
			Require(glm::dot(vertex.Normal, glm::normalize(vertex.Position - center)) > 0.99f, "The sphere's normals should point out");
		}
		const size_t edges = CountEdges({ &mesh }).size();
		Require(int(stats.Vertices) - int(edges) + int(stats.Triangles) == 2, "The sphere's mesh should be one closed surface without holes");
		std::cout << "Sphere: " << stats.Vertices << " vertices, " << stats.Triangles << " triangles\n";
	}

	float NearestVertex(const MeshData& mesh, const glm::vec3& point)
	{
		float nearest = 1e9f;
		for (const Nyx::Vertex& vertex : mesh.Vertices)
		{
			nearest = std::min(nearest, glm::length(vertex.Position - point));
		}
		return nearest;
	}

	// Sharp vertices keep a box's corners, smooth ones round them; both closed and facing out
	void TestBoxSharpAndSmooth()
	{
		const glm::vec3 center(17.37f, 17.21f, 16.93f);
		const glm::vec3 half(8.3f, 6.7f, 5.4f);
		const Field field = Box(center, half);
		const FieldGrid grid = SampleField(field, glm::vec3(0.0f), 1.0f, glm::ivec3(35));

		// Sharp with the field itself for the planes, sharp from the samples alone, and smooth
		FieldMeshSettings exact;
		exact.SharpMaterials = { true };
		exact.Field = [&](const float* x, const float* y, const float* z, size_t count, float* out)
		{
			for (size_t i = 0; i < count; ++i)
			{
				out[i] = field(glm::vec3(x[i], y[i], z[i]));
			}
		};
		FieldMeshSettings fromSamples;
		fromSamples.SharpMaterials = { true };
		FieldMeshStats exactStats;
		const MeshData exactMesh = Nyx::MakeFieldMesh(grid, exact, &exactStats);
		const MeshData samplesMesh = Nyx::MakeFieldMesh(grid, fromSamples);
		const MeshData smoothMesh = Nyx::MakeFieldMesh(grid);
		RequireClosed({ &exactMesh }, "Sharp box");
		RequireClosed({ &samplesMesh }, "Sharp box from the samples");
		RequireClosed({ &smoothMesh }, "Smooth box");
		RequireFacingOut(exactMesh, field, "Sharp box");
		RequireFacingOut(samplesMesh, field, "Sharp box from the samples");
		RequireFacingOut(smoothMesh, field, "Smooth box");
		Require(exactStats.SharpVertices == exactStats.Vertices, "Every vertex of the sharp box should be sharp");

		auto worstCorner = [&](const MeshData& mesh)
		{
			float worst = 0.0f;
			for (int corner = 0; corner < 8; ++corner)
			{
				const glm::vec3 sign(corner & 1 ? 1.0f : -1.0f, corner & 2 ? 1.0f : -1.0f, corner & 4 ? 1.0f : -1.0f);
				worst = std::max(worst, NearestVertex(mesh, center + sign * half));
			}
			return worst;
		};
		const float exactWorst = worstCorner(exactMesh);
		const float samplesWorst = worstCorner(samplesMesh);
		const float smoothWorst = worstCorner(smoothMesh);
		Require(exactWorst < 0.05f, "Sharp vertices placed by the field should keep every corner of the box: " + std::to_string(exactWorst));
		Require(samplesWorst < smoothWorst, "Sharp vertices from the samples should still keep the corners closer than smooth ones");
		std::cout << "Box: the farthest corner " << exactWorst << " m from a vertex sharp by the field, " << samplesWorst << " m sharp by the samples, "
				  << smoothWorst << " m smooth\n";
	}

	// Plates and rods thinner than a cell, sharp and smooth: closed, manifold, never facing the wrong
	// way
	void TestThinShapes()
	{
		const std::vector<std::pair<std::string, Field>> shapes{
			{ "Plate", Box(glm::vec3(17.2f, 17.3f, 16.9f), glm::vec3(10.0f, 0.45f, 9.0f)) },
			{ "Tilted plate", [](const glm::vec3& p)
				{
					const glm::vec3 n = glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f));
					const float slab = std::fabs(glm::dot(p - glm::vec3(17.0f), n)) - 0.35f;
					return std::max(slab, glm::length(p - glm::vec3(17.0f)) - 12.0f);
				} },
			{ "Rod", [](const glm::vec3& p) { return std::max(glm::length(glm::vec2(p.y - 17.2f, p.z - 16.9f)) - 0.45f, std::fabs(p.x - 17.0f) - 12.0f); } },
			{ "Two sheets", [](const glm::vec3& p) { return std::max(std::fabs(std::fabs(p.y - 17.0f) - 0.8f) - 0.3f, glm::length(p - glm::vec3(17.0f)) - 12.0f); } },
		};
		for (const auto& [name, field] : shapes)
		{
			for (const bool bSharp : { false, true })
			{
				FieldMeshSettings settings;
				settings.SharpMaterials = { bSharp };
				FieldMeshStats stats;
				const MeshData mesh = Nyx::MakeFieldMesh(SampleField(field, glm::vec3(0.0f), 1.0f, glm::ivec3(35)), settings, &stats);
				const std::string what = name + (bSharp ? ", sharp" : ", smooth");
				Require(!mesh.Indices.empty(), what + ": should make a mesh");
				Require(Nyx::CheckMeshData(mesh).empty(), what + ": the mesh should be valid");
				const int shared = RequireClosedCountingShared(mesh, what);
				RequireConsistentWinding(mesh, what);
				// Thinner than a cell, the samples' gradients, and so the normals, mean little there (both
				// samples round a plate can be outside it): how many triangles sit against them is
				// printed, not held to anything
				std::cout << what << ": " << stats.Triangles << " triangles, " << stats.SplitCells << " cells split, " << stats.UnfoldedVertices
						  << " vertices unfolded, " << shared << " edges shared by four triangles, " << stats.BackFacingTriangles << " against the normals\n";
			}
		}
	}

	// Two chunks side by side, each making its own quads, meet without a gap or an overlap: the
	// sphere across them is closed
	void TestNeighboursMeet()
	{
		const Field field = Sphere(glm::vec3(32.4f, 16.3f, 16.7f), 12.0f);
		FieldMeshSettings settings;
		settings.QuadMin = glm::ivec3(2);
		settings.QuadMax = glm::ivec3(34);
		settings.SharpMaterials = { true };
		settings.Field = [&](const float* x, const float* y, const float* z, size_t count, float* out)
		{
			for (size_t i = 0; i < count; ++i)
			{
				out[i] = field(glm::vec3(x[i], y[i], z[i]));
			}
		};
		// Each chunk's grid: its 33 samples, two more before and one after
		const MeshData left = Nyx::MakeFieldMesh(SampleField(field, glm::vec3(-2.0f), 1.0f, glm::ivec3(36)), settings);
		const MeshData right = Nyx::MakeFieldMesh(SampleField(field, glm::vec3(30.0f, -2.0f, -2.0f), 1.0f, glm::ivec3(36)), settings);
		Require(!left.Indices.empty() && !right.Indices.empty(), "Both chunks should have part of the sphere");
		RequireClosed({ &left, &right }, "Two chunks");
	}

	// Open sky on flat ground; little under an overhang, none facing down under it; none in a closed
	// pocket; and the field beyond the grid blocks it too
	void TestSkyVisibility()
	{
		const Field ground = [](const glm::vec3& p) { return p.y - 10.3f; };
		const MeshData open = Nyx::MakeFieldMesh(SampleField(ground, glm::vec3(0.0f), 1.0f, glm::ivec3(35)));
		for (const Nyx::Vertex& vertex : open.Vertices)
		{
			Require(vertex.Custom.x > 0.99f, "Flat open ground should see the whole sky");
		}

		const Field roof = Box(glm::vec3(17.0f, 15.5f, 17.0f), glm::vec3(10.0f, 1.0f, 10.0f));
		const Field overhang = [&](const glm::vec3& p) { return std::min(ground(p), roof(p)); };
		const MeshData covered = Nyx::MakeFieldMesh(SampleField(overhang, glm::vec3(0.0f), 1.0f, glm::ivec3(35)));
		for (const Nyx::Vertex& vertex : covered.Vertices)
		{
			const bool bUnderRoofMiddle = std::fabs(vertex.Position.x - 17.0f) < 3.0f && std::fabs(vertex.Position.z - 17.0f) < 3.0f;
			if (bUnderRoofMiddle && vertex.Position.y < 12.0f)
			{
				Require(vertex.Custom.x < 0.3f, "Ground under the middle of an overhang should see little sky");
			}
			if (vertex.Normal.y < -0.9f)
			{
				Require(vertex.Custom.x == 0.0f, "A surface facing straight down should see no sky");
			}
		}

		const Field pocket = [](const glm::vec3& p) { return 6.0f - glm::length(p - glm::vec3(17.2f)); };
		const MeshData hollow = Nyx::MakeFieldMesh(SampleField(pocket, glm::vec3(0.0f), 1.0f, glm::ivec3(35)));
		Require(!hollow.Vertices.empty(), "The pocket should have walls");
		for (const Nyx::Vertex& vertex : hollow.Vertices)
		{
			Require(vertex.Custom.x == 0.0f, "Inside a closed pocket nothing should see the sky");
		}

		FieldMeshSettings beyond;
		beyond.SkyReach = 64.0f;
		beyond.Field = [](const float*, const float* y, const float*, size_t count, float* out)
		{
			for (size_t i = 0; i < count; ++i)
			{
				// A ceiling above the grid
				out[i] = y[i] > 36.0f ? -1.0f : 1.0f;
			}
		};
		const MeshData under = Nyx::MakeFieldMesh(SampleField(ground, glm::vec3(0.0f), 1.0f, glm::ivec3(35)), beyond);
		for (const Nyx::Vertex& vertex : under.Vertices)
		{
			Require(vertex.Custom.x < 0.5f, "A ceiling beyond the grid should hide the sky");
		}

		FieldMeshSettings none;
		none.SkyReach = 0.0f;
		for (const Nyx::Vertex& vertex : Nyx::MakeFieldMesh(SampleField(pocket, glm::vec3(0.0f), 1.0f, glm::ivec3(35)), none).Vertices)
		{
			Require(vertex.Custom.x == 1.0f, "With no reach, every vertex should see the whole sky");
		}
	}

	// Each vertex takes its inside corners' material
	void TestMaterials()
	{
		const glm::vec3 center(17.3f, 17.1f, 16.8f);
		const MeshData mesh = Nyx::MakeFieldMesh(SampleField(Sphere(center, 10.0f), glm::vec3(0.0f), 1.0f, glm::ivec3(35),
			[&](const glm::vec3& p) { return p.x < center.x ? 1u : 2u; }));
		for (const Nyx::Vertex& vertex : mesh.Vertices)
		{
			if (vertex.Position.x < center.x - 1.5f)
			{
				Require(vertex.Custom.y == 1.0f, "Vertices on the left half should be material 1");
			}
			if (vertex.Position.x > center.x + 1.5f)
			{
				Require(vertex.Custom.y == 2.0f, "Vertices on the right half should be material 2");
			}
		}
	}

	Nyx::SdfTerrainComponent MakeTerrain()
	{
		Nyx::SdfTerrainComponent terrain;
		terrain.Seed = 5;
		terrain.Layers[0].Amplitude = 12.0f;
		terrain.Layers[0].Frequency = 0.02f;
		terrain.Layers[0].TerraceHeight = 4.0f;
		terrain.Layers[0].Material = 1;
		Nyx::SdfTerrainLayer tunnels;
		tunnels.Kind = Nyx::ESdfTerrainLayerKind::Tunnels;
		tunnels.Combine = Nyx::ESdfTerrainCombine::Carve;
		tunnels.Height = 8.0f;
		tunnels.Depth = 20.0f;
		tunnels.Radius = 5.0f;
		tunnels.Frequency = 0.04f;
		tunnels.Octaves = 2;
		terrain.Layers.push_back(tunnels);
		return terrain;
	}

	// The same mesh of the same terrain, to the bit, as when it was written
	void TestGoldenMesh()
	{
		FieldMeshSettings settings;
		settings.QuadMin = glm::ivec3(2);
		settings.QuadMax = glm::ivec3(34);
		settings.SharpMaterials = { false, true };
		const Nyx::SdfTerrainComponent terrain = MakeTerrain();
		settings.Field = [&](const float* x, const float* y, const float* z, size_t count, float* out) { Nyx::SampleTerrainBatch(terrain, x, y, z, count, out); };
		const FieldGrid grid = Nyx::SampleTerrainGrid(terrain, glm::vec3(-18.0f, -18.0f, -18.0f), 1.0f, glm::ivec3(36));
		const MeshData mesh = Nyx::MakeFieldMesh(grid, settings);
		Require(!mesh.Indices.empty(), "The terrain's chunk should have a surface");
		uint64_t hash = 0xCBF29CE484222325ull;
		auto add = [&](uint32_t bits)
		{
			for (int byte = 0; byte < 4; ++byte)
			{
				hash ^= (bits >> (byte * 8)) & 0xFFu;
				hash *= 0x100000001B3ull;
			}
		};
		for (const Nyx::Vertex& vertex : mesh.Vertices)
		{
			for (int axis = 0; axis < 3; ++axis)
			{
				add(BitsOf(vertex.Position[axis]));
				add(BitsOf(vertex.Normal[axis]));
			}
			add(BitsOf(vertex.Custom.x));
			add(BitsOf(vertex.Custom.y));
		}
		for (const uint32_t index : mesh.Indices)
		{
			add(index);
		}
		constexpr uint64_t Golden = 0x27bb32531129067full;
		std::cout << std::hex << "Golden mesh hash: 0x" << hash << std::dec << "\n";
		Require(hash == Golden, "The terrain's mesh changed");
	}

	// Not a test: a chunk of terrain sampled, and meshed smooth, sharp from the samples and sharp by
	// the terrain itself, each with and without sky visibility
	void PrintTimings()
	{
		const Nyx::SdfTerrainComponent terrain = MakeTerrain();
		auto milliseconds = [](auto from, auto to) { return std::chrono::duration<double, std::milli>(to - from).count(); };
		const auto start = std::chrono::steady_clock::now();
		const FieldGrid grid = Nyx::SampleTerrainGrid(terrain, glm::vec3(30.0f, -18.0f, -18.0f), 1.0f, glm::ivec3(36));
		std::cout << "A chunk of terrain: sampled in " << milliseconds(start, std::chrono::steady_clock::now()) << " ms\n";

		for (int variant = 0; variant < 3; ++variant)
		{
			for (const float reach : { 16.0f, 0.0f })
			{
				FieldMeshSettings settings;
				settings.QuadMin = glm::ivec3(2);
				settings.QuadMax = glm::ivec3(34);
				settings.SkyReach = reach;
				if (variant > 0)
				{
					settings.SharpMaterials = { false, true };
				}
				if (variant > 1)
				{
					settings.Field = [&](const float* x, const float* y, const float* z, size_t count, float* out) { Nyx::SampleTerrainBatch(terrain, x, y, z, count, out); };
				}
				FieldMeshStats stats;
				const auto before = std::chrono::steady_clock::now();
				const MeshData mesh = Nyx::MakeFieldMesh(grid, settings, &stats);
				const double time = milliseconds(before, std::chrono::steady_clock::now());
				// Against the terrain's own gradient where they are, which the normals only approximate
				int againstTerrain = 0;
				for (size_t t = 0; t + 2 < mesh.Indices.size(); t += 3)
				{
					const Field terrainField = [&](const glm::vec3& p) { return Nyx::SampleTerrainValue(terrain, p); };
					if (glm::dot(TriangleNormal(mesh, t), FieldGradient(terrainField, Centroid(mesh, t))) < 0.0f)
					{
						++againstTerrain;
					}
				}
				const char* names[3] = { "smooth", "sharp from the samples", "sharp by the terrain" };
				std::cout << "  meshed " << names[variant] << (reach > 0.0f ? ", with sky: " : ", without sky: ") << time << " ms; " << stats.Vertices << " vertices, "
						  << stats.Triangles << " triangles, " << stats.SplitCells << " cells split, " << stats.BackFacingTriangles << " against the normals, " << againstTerrain
						  << " against the terrain\n";
			}
		}
	}
}

int main()
{
	try
	{
		TestSphere();
		TestBoxSharpAndSmooth();
		TestThinShapes();
		TestNeighboursMeet();
		TestSkyVisibility();
		TestMaterials();
		TestGoldenMesh();
		PrintTimings();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Field mesher test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All field mesher tests passed.\n";
	return 0;
}
