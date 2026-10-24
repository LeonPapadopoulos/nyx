// The built-in meshes' tangents, without a GPU: on a flat face the tangent runs along rising u and
// the bitangent, cross(normal, tangent) * w, up the image (falling v); on the sphere every tangent
// is of unit length, perpendicular to its normal, and the faces wind counter-clockwise outside; the
// capsule stands on the origin, its vertices on its surface. Every mesh Nyx makes or ships winds
// counter-clockwise seen from the side its normals face, as the renderer's back-face culling
// needs: the cube, the sphere, the capsule, the plane, a generated ground and the glTF files in
// Assets/Models. Meshes as data (MeshData): which are
// valid (indices of whole triangles below the vertex count, sections one after the other over every
// index), the sections a mesh without any is drawn in, and the bounds around its vertices. The
// vertex as the GPU reads it (GpuVertex): every field where its attribute says, the custom channels
// back from half precision as close as it keeps them, the instance stream laid out alike; and the
// custom channels zero in every mesh Nyx makes or imports.
#include "GltfImporter.h"
#include "GroundComponent.h"
#include "GroundMesh.h"
#include "Mesh.h"
#include "Paths.h"
#include "TestSupport.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
	using Nyx::Tests::Require;

	bool Near(const glm::vec3& a, const glm::vec3& b, float tolerance = 1e-4f)
	{
		return glm::all(glm::lessThanEqual(glm::abs(a - b), glm::vec3(tolerance)));
	}

	// A quad facing +Z with its texture's v rising upwards, like the cube's front face
	void TestFlatFaces()
	{
		Nyx::MeshData quad;
		const glm::vec3 normal(0.0f, 0.0f, 1.0f);
		quad.Vertices = {
			{ { -0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f }, normal },
			{ { 0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f }, normal },
			{ { 0.5f, 0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f }, normal },
			{ { -0.5f, 0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f }, normal },
		};
		quad.Indices = { 0, 1, 2, 2, 3, 0 };
		Nyx::ComputeTangents(quad);
		for (const Nyx::Vertex& vertex : quad.Vertices)
		{
			Require(Near(glm::vec3(vertex.Tangent), glm::vec3(1.0f, 0.0f, 0.0f)), "The tangent doesn't run along rising u");
			// v rises upwards, so up the image is down here
			const glm::vec3 bitangent = glm::cross(vertex.Normal, glm::vec3(vertex.Tangent)) * vertex.Tangent.w;
			Require(Near(bitangent, glm::vec3(0.0f, -1.0f, 0.0f)), "The bitangent doesn't point up the image (falling v)");
		}

		// The same face with v falling upwards, as images are usually laid on: the bitangent turns
		for (Nyx::Vertex& vertex : quad.Vertices)
		{
			vertex.UV.y = 1.0f - vertex.UV.y;
		}
		Nyx::ComputeTangents(quad);
		const glm::vec3 bitangent = glm::cross(quad.Vertices[0].Normal, glm::vec3(quad.Vertices[0].Tangent)) * quad.Vertices[0].Tangent.w;
		Require(Near(bitangent, glm::vec3(0.0f, 1.0f, 0.0f)), "With v falling upwards, the bitangent doesn't point up");
	}

	void TestSphere()
	{
		const Nyx::MeshData sphere = Nyx::MakeSphere(0.5f, 16, 32);
		Require(sphere.Vertices.size() == 17 * 33 && sphere.Indices.size() == 16 * 32 * 6, "The sphere has the wrong number of vertices or indices");
		for (const Nyx::Vertex& vertex : sphere.Vertices)
		{
			Require(std::abs(glm::length(vertex.Position) - 0.5f) < 1e-4f, "A vertex isn't on the sphere");
			const glm::vec3 tangent(vertex.Tangent);
			Require(std::abs(glm::length(tangent) - 1.0f) < 1e-3f, "A tangent isn't of unit length");
			Require(std::abs(glm::dot(tangent, vertex.Normal)) < 1e-3f, "A tangent isn't perpendicular to its normal");
			Require(vertex.Tangent.w == 1.0f || vertex.Tangent.w == -1.0f, "A tangent's w isn't +1 or -1");
		}
		// Counter-clockwise seen from outside: each triangle's normal points away from the middle
		for (size_t index = 0; index + 2 < sphere.Indices.size(); index += 3)
		{
			const glm::vec3 a = sphere.Vertices[sphere.Indices[index]].Position;
			const glm::vec3 b = sphere.Vertices[sphere.Indices[index + 1]].Position;
			const glm::vec3 c = sphere.Vertices[sphere.Indices[index + 2]].Position;
			const glm::vec3 faceNormal = glm::cross(b - a, c - a);
			if (glm::dot(faceNormal, faceNormal) > 1e-12f)
			{
				Require(glm::dot(faceNormal, a + b + c) > 0.0f, "A triangle of the sphere winds clockwise seen from outside");
			}
		}
	}

	void TestCapsule()
	{
		const float radius = 0.3f;
		const float height = 1.8f;
		const Nyx::MeshData capsule = Nyx::MakeCapsule(radius, height, 16, 24);
		Require(capsule.Vertices.size() == 18 * 25 && capsule.Indices.size() == 17 * 24 * 6, "The capsule has the wrong number of vertices or indices");
		// The nearest point of its middle line, from radius to height - radius
		const auto axisPointOf = [&](const glm::vec3& position)
		{
			return glm::vec3(0.0f, std::clamp(position.y, radius, height - radius), 0.0f);
		};
		float lowest = height;
		float highest = 0.0f;
		for (const Nyx::Vertex& vertex : capsule.Vertices)
		{
			const glm::vec3 outwards = vertex.Position - axisPointOf(vertex.Position);
			Require(std::abs(glm::length(outwards) - radius) < 1e-4f, "A vertex isn't on the capsule");
			Require(glm::length(glm::normalize(outwards) - vertex.Normal) < 1e-3f, "A normal doesn't point away from the middle line");
			Require(std::abs(glm::dot(glm::vec3(vertex.Tangent), vertex.Normal)) < 1e-3f, "A tangent isn't perpendicular to its normal");
			lowest = std::min(lowest, vertex.Position.y);
			highest = std::max(highest, vertex.Position.y);
		}
		Require(std::abs(lowest) < 1e-5f && std::abs(highest - height) < 1e-5f, "The capsule should stand on the origin, as tall as asked");
		for (size_t index = 0; index + 2 < capsule.Indices.size(); index += 3)
		{
			const glm::vec3 a = capsule.Vertices[capsule.Indices[index]].Position;
			const glm::vec3 b = capsule.Vertices[capsule.Indices[index + 1]].Position;
			const glm::vec3 c = capsule.Vertices[capsule.Indices[index + 2]].Position;
			const glm::vec3 faceNormal = glm::cross(b - a, c - a);
			const glm::vec3 middle = (a + b + c) / 3.0f;
			if (glm::dot(faceNormal, faceNormal) > 1e-12f)
			{
				Require(glm::dot(faceNormal, middle - axisPointOf(middle)) > 0.0f, "A triangle of the capsule winds clockwise seen from outside");
			}
		}
	}

	// Two quads, a section each in slots 0 and 1, as a tree's trunk and leaves
	Nyx::MeshData MakeTwoQuads()
	{
		Nyx::MeshData mesh;
		for (int quad = 0; quad < 2; ++quad)
		{
			const uint32_t first = static_cast<uint32_t>(mesh.Vertices.size());
			for (const glm::vec2 corner : { glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 0.0f), glm::vec2(1.0f, 1.0f), glm::vec2(0.0f, 1.0f) })
			{
				mesh.Vertices.push_back(Nyx::Vertex{ .Position = glm::vec3(corner.x + 2.0f * quad, corner.y, -1.0f * quad) });
			}
			mesh.Indices.insert(mesh.Indices.end(), { first, first + 1, first + 2, first + 2, first + 3, first });
		}
		mesh.Sections = { Nyx::MeshSection{ .FirstIndex = 0, .IndexCount = 6, .MaterialSlot = 0 }, Nyx::MeshSection{ .FirstIndex = 6, .IndexCount = 6, .MaterialSlot = 1 } };
		return mesh;
	}

	void RequireProblem(const Nyx::MeshData& mesh, const std::string& what)
	{
		Require(!Nyx::CheckMeshData(mesh).empty(), "A mesh with " + what + " should not be valid");
	}

	void TestMeshDataValidity()
	{
		const Nyx::MeshData valid = MakeTwoQuads();
		Require(Nyx::CheckMeshData(valid).empty(), "Two quads in two sections should be valid: " + Nyx::CheckMeshData(valid));
		Require(Nyx::CheckMeshData(Nyx::MeshData{}).empty(), "An empty mesh should be valid");
		Require(Nyx::CheckMeshData(Nyx::MakeSphere(0.5f, 8, 16)).empty(), "The sphere should be valid");

		Nyx::MeshData mesh = valid;
		mesh.Indices[7] = 8;
		RequireProblem(mesh, "an index past its vertices");
		mesh = valid;
		mesh.Indices.pop_back();
		mesh.Sections[1].IndexCount = 5;
		RequireProblem(mesh, "indices of no whole triangles");
		mesh = valid;
		mesh.Sections[1].FirstIndex = 3;
		mesh.Sections[1].IndexCount = 9;
		RequireProblem(mesh, "sections that overlap");
		mesh = valid;
		mesh.Sections.pop_back();
		RequireProblem(mesh, "sections that leave indices out");
		mesh = valid;
		mesh.Sections[1].IndexCount = 12;
		RequireProblem(mesh, "a section past the indices");
		mesh = valid;
		mesh.Sections.push_back(Nyx::MeshSection{ .FirstIndex = 12, .IndexCount = 0, .MaterialSlot = 2 });
		RequireProblem(mesh, "an empty section");
		mesh = valid;
		std::swap(mesh.Sections[0], mesh.Sections[1]);
		RequireProblem(mesh, "sections out of order");
	}

	void TestMeshSectionsAndBounds()
	{
		Nyx::MeshData mesh = MakeTwoQuads();
		Require(Nyx::GetMeshSections(mesh).size() == 2, "A mesh's own sections should be drawn");
		mesh.Sections.clear();
		const std::vector<Nyx::MeshSection> whole = Nyx::GetMeshSections(mesh);
		Require(whole.size() == 1 && whole[0].FirstIndex == 0 && whole[0].IndexCount == 12 && whole[0].MaterialSlot == 0,
			"A mesh without sections should be drawn as one of every index in slot 0");
		Require(Nyx::GetMeshSections(Nyx::MeshData{}).empty(), "An empty mesh should have nothing to draw");

		const Nyx::MeshBounds bounds = Nyx::ComputeMeshBounds(mesh);
		Require(Near(bounds.Min, glm::vec3(0.0f, 0.0f, -1.0f)) && Near(bounds.Max, glm::vec3(3.0f, 1.0f, 0.0f)), "The bounds should be around the two quads");
		const Nyx::MeshBounds empty = Nyx::ComputeMeshBounds(Nyx::MeshData{});
		Require(empty.Min == glm::vec3(0.0f) && empty.Max == glm::vec3(0.0f), "An empty mesh's bounds should be nothing at the origin");
	}
}

namespace
{
	// How many of the mesh's triangles wind clockwise seen from the side their vertices' normals
	// face, i.e. would be culled where they should be seen; degenerate ones aside
	size_t CountWrongWinding(const Nyx::MeshData& mesh)
	{
		size_t wrong = 0;
		for (size_t index = 0; index + 2 < mesh.Indices.size(); index += 3)
		{
			const Nyx::Vertex& a = mesh.Vertices[mesh.Indices[index]];
			const Nyx::Vertex& b = mesh.Vertices[mesh.Indices[index + 1]];
			const Nyx::Vertex& c = mesh.Vertices[mesh.Indices[index + 2]];
			const glm::vec3 face = glm::cross(b.Position - a.Position, c.Position - a.Position);
			const glm::vec3 normals = a.Normal + b.Normal + c.Normal;
			if (glm::length(face) > 1e-9f && glm::dot(face, normals) <= 0.0f)
			{
				++wrong;
			}
		}
		return wrong;
	}

	void RequireFrontOutside(const Nyx::MeshData& mesh, const std::string& what)
	{
		Require(!mesh.Indices.empty(), what + " has no triangles");
		const size_t wrong = CountWrongWinding(mesh);
		Require(wrong == 0, what + ": " + std::to_string(wrong) + " of " + std::to_string(mesh.Indices.size() / 3) + " triangles wind clockwise seen from where their normals face");
	}

	void TestWinding()
	{
		RequireFrontOutside(Nyx::MakeCube(1.0f), "The cube");
		RequireFrontOutside(Nyx::MakeSphere(0.5f, 32, 64), "The sphere");
		RequireFrontOutside(Nyx::MakeCapsule(0.3f, 1.8f, 32, 48), "The capsule");
		RequireFrontOutside(Nyx::MakePlane(1.0f), "The plane");

		// A ground with hills, a basin and terraces, so its normals lean every way
		Nyx::GroundComponent ground;
		ground.Size = glm::vec2(64.0f);
		ground.Spacing = 1.0f;
		RequireFrontOutside(Nyx::MakeGroundMesh(ground), "The ground");

		const std::filesystem::path models = Nyx::Paths::GetAssetsDir() / "Models";
		size_t files = 0;
		for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(models))
		{
			if (entry.path().extension() != ".gltf")
			{
				continue;
			}
			Nyx::ImportedScene scene;
			Require(Nyx::LoadStaticGltfScene(entry.path().string(), scene), "Can't read " + entry.path().string());
			for (size_t primitive = 0; primitive < scene.Primitives.size(); ++primitive)
			{
				RequireFrontOutside(scene.Primitives[primitive].Mesh, entry.path().filename().string() + ", primitive " + std::to_string(primitive));
			}
			++files;
		}
		Require(files > 0, "No glTF files in " + models.string());
	}

	// The custom channels in the vertex buffer: four half floats, as close as they keep them (one
	// part in 2048 of the value), 0 and 1 and small whole numbers exactly
	void TestVertexLayout()
	{
		const Nyx::Vertex vertex{ .Position = glm::vec3(1.0f, -2.0f, 3.5f),
			.Color = glm::vec3(0.1f, 0.2f, 0.3f),
			.UV = glm::vec2(0.25f, 0.75f),
			.Normal = glm::vec3(0.0f, 0.6f, 0.8f),
			.Tangent = glm::vec4(0.8f, 0.0f, -0.6f, -1.0f),
			.Custom = glm::vec4(0.0f, 1.0f, 0.3333f, -12.5f) };
		const Nyx::GpuVertex gpu = Nyx::ToGpuVertex(vertex);
		Require(gpu.Position == vertex.Position && gpu.Color == vertex.Color && gpu.UV == vertex.UV && gpu.Normal == vertex.Normal && gpu.Tangent == vertex.Tangent,
			"The GPU's vertex should keep everything but the custom channels as it is");
		const glm::vec4 custom = Nyx::GetGpuVertexCustom(gpu);
		Require(custom.x == 0.0f && custom.y == 1.0f && custom.w == -12.5f, "0, 1 and -12.5 should come back from half precision exactly");
		Require(std::abs(custom.z - 0.3333f) < 0.3333f / 2048.0f, "A third should come back within half precision, not as " + std::to_string(custom.z));
		Require(Nyx::GetGpuVertexCustom(Nyx::ToGpuVertex(Nyx::Vertex{})) == glm::vec4(0.0f), "A vertex's custom channels should be zero unless set");

		// Every attribute inside the stride, none over another, each where its field is
		const vk::VertexInputBindingDescription binding = Nyx::GetVertexBindingDescription();
		Require(binding.stride == sizeof(Nyx::GpuVertex) && binding.stride == 68, "The vertex buffer's stride should be 68 bytes");
		const auto attributes = Nyx::GetVertexAttributeDescriptions();
		const auto instanceAttributes = Nyx::GetInstanceAttributeDescriptions();
		const auto size = [](vk::Format format)
		{
			switch (format)
			{
			case vk::Format::eR32G32Sfloat:
				return 8u;
			case vk::Format::eR32G32B32Sfloat:
				return 12u;
			case vk::Format::eR32G32B32A32Sfloat:
				return 16u;
			case vk::Format::eR16G16B16A16Sfloat:
				return 8u;
			default:
				return 0u;
			}
		};
		std::vector<bool> taken(binding.stride, false);
		for (const vk::VertexInputAttributeDescription& attribute : attributes)
		{
			Require(size(attribute.format) > 0 && attribute.offset + size(attribute.format) <= binding.stride, "A vertex attribute reaches past the vertex");
			for (uint32_t byte = attribute.offset; byte < attribute.offset + size(attribute.format); ++byte)
			{
				Require(!taken[byte], "Two vertex attributes overlap");
				taken[byte] = true;
			}
		}
		Require(std::all_of(taken.begin(), taken.end(), [](bool bTaken) { return bTaken; }), "Some bytes of the vertex aren't read by any attribute");
		Require(attributes[5].offset == offsetof(Nyx::GpuVertex, Custom) && attributes[5].format == vk::Format::eR16G16B16A16Sfloat && attributes[5].location == Nyx::VertexCustomLocation,
			"The custom channels should be read as four half floats at location 9");

		// The instance stream: the transform's columns, then its custom numbers
		Require(Nyx::GetInstanceBindingDescription().stride == sizeof(Nyx::MeshInstanceData) && sizeof(Nyx::MeshInstanceData) == 80, "The instance stream's stride should be 80 bytes");
		for (uint32_t column = 0; column < 4; ++column)
		{
			Require(instanceAttributes[column].offset == column * 16 && instanceAttributes[column].location == 5 + column, "The instance's columns should be at locations 5 to 8");
		}
		Require(instanceAttributes[4].offset == 64 && instanceAttributes[4].location == Nyx::InstanceCustomLocation, "The instance's custom numbers should be at location 10, after its transform");

		// Each location once
		std::vector<uint32_t> locations;
		for (const auto& attribute : attributes)
		{
			locations.push_back(attribute.location);
		}
		for (const auto& attribute : instanceAttributes)
		{
			locations.push_back(attribute.location);
		}
		std::sort(locations.begin(), locations.end());
		Require(std::adjacent_find(locations.begin(), locations.end()) == locations.end(), "Two attributes share a location");
	}

	void RequireNoCustom(const Nyx::MeshData& mesh, const std::string& what)
	{
		for (const Nyx::Vertex& vertex : mesh.Vertices)
		{
			Require(vertex.Custom == glm::vec4(0.0f), what + " has custom channels that aren't zero");
		}
	}

	// What Nyx makes and imports has no custom channels of its own: all zero
	void TestCustomChannelsZero()
	{
		RequireNoCustom(Nyx::MakeCube(1.0f), "The cube");
		RequireNoCustom(Nyx::MakeSphere(0.5f, 8, 16), "The sphere");
		RequireNoCustom(Nyx::MakeCapsule(0.3f, 1.8f, 8, 12), "The capsule");
		RequireNoCustom(Nyx::MakePlane(1.0f), "The plane");
		Nyx::GroundComponent ground;
		ground.Size = glm::vec2(16.0f);
		RequireNoCustom(Nyx::MakeGroundMesh(ground), "The ground");
		for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(Nyx::Paths::GetAssetsDir() / "Models"))
		{
			if (entry.path().extension() != ".gltf")
			{
				continue;
			}
			Nyx::ImportedScene scene;
			Require(Nyx::LoadStaticGltfScene(entry.path().string(), scene), "Can't read " + entry.path().string());
			for (const auto& primitive : scene.Primitives)
			{
				RequireNoCustom(primitive.Mesh, entry.path().filename().string());
			}
		}
	}
}

int main()
{
	try
	{
		TestFlatFaces();
		TestSphere();
		TestCapsule();
		TestMeshDataValidity();
		TestMeshSectionsAndBounds();
		TestWinding();
		TestVertexLayout();
		TestCustomChannelsZero();
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All mesh tests passed\n";
	return 0;
}
