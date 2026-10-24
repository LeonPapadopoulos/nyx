#include "NyxPCH.h"
#include "Mesh.h"
#include "Assertions.h"
#include "Resource.h"
#include <vulkan/vulkan_raii.hpp>
#include "VulkanContext.h"
#include "VulkanUtil.h"

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <map>
#include <span>
#include <vector>

namespace Nyx
{
	GpuVertex ToGpuVertex(const Vertex& vertex)
	{
		GpuVertex gpu;
		gpu.Position = vertex.Position;
		gpu.Color = vertex.Color;
		gpu.UV = vertex.UV;
		gpu.Normal = vertex.Normal;
		gpu.Tangent = vertex.Tangent;
		gpu.Custom[0] = glm::packHalf2x16(glm::vec2(vertex.Custom.x, vertex.Custom.y));
		gpu.Custom[1] = glm::packHalf2x16(glm::vec2(vertex.Custom.z, vertex.Custom.w));
		return gpu;
	}

	glm::vec4 GetGpuVertexCustom(const GpuVertex& vertex)
	{
		return glm::vec4(glm::unpackHalf2x16(vertex.Custom[0]), glm::unpackHalf2x16(vertex.Custom[1]));
	}

	std::vector<vk::VertexInputAttributeDescription> GetMeshAttributesRead(const std::vector<uint32_t>& vertexSpirv)
	{
		// The Input variables' Location decorations
		constexpr uint32_t OpVariable = 59;
		constexpr uint32_t OpDecorate = 71;
		constexpr uint32_t DecorationLocation = 30;
		constexpr uint32_t StorageClassInput = 1;
		std::map<uint32_t, uint32_t> locations;
		std::vector<uint32_t> inputs;
		for (size_t index = 5; index < vertexSpirv.size();)
		{
			const uint32_t wordCount = vertexSpirv[index] >> 16;
			const uint32_t opcode = vertexSpirv[index] & 0xFFFF;
			if (wordCount == 0)
			{
				break;
			}
			if (opcode == OpDecorate && wordCount >= 4 && vertexSpirv[index + 2] == DecorationLocation)
			{
				locations[vertexSpirv[index + 1]] = vertexSpirv[index + 3];
			}
			else if (opcode == OpVariable && wordCount >= 4 && vertexSpirv[index + 3] == StorageClassInput)
			{
				inputs.push_back(vertexSpirv[index + 2]);
			}
			index += wordCount;
		}
		std::vector<uint32_t> read;
		for (const uint32_t input : inputs)
		{
			if (const auto found = locations.find(input); found != locations.end())
			{
				read.push_back(found->second);
			}
		}

		std::vector<vk::VertexInputAttributeDescription> attributes;
		const auto vertexAttributes = GetVertexAttributeDescriptions();
		const auto instanceAttributes = GetInstanceAttributeDescriptions();
		for (const std::span<const vk::VertexInputAttributeDescription> stream : { std::span<const vk::VertexInputAttributeDescription>(vertexAttributes),
				 std::span<const vk::VertexInputAttributeDescription>(instanceAttributes) })
		{
			for (const vk::VertexInputAttributeDescription& attribute : stream)
			{
				if (std::find(read.begin(), read.end(), attribute.location) != read.end())
				{
					attributes.push_back(attribute);
				}
			}
		}
		return attributes;
	}

	MeshData MakeCube(float size)
	{
		const float h = size * 0.5f;
		MeshData mesh;
		// Each face: its normal, then its corners from the bottom left, counter-clockwise seen from
		// outside, with UVs (0, 0), (1, 0), (1, 1), (0, 1)
		const auto face = [&mesh](const glm::vec3& normal, std::initializer_list<glm::vec3> corners)
		{
			const uint32_t first = static_cast<uint32_t>(mesh.Vertices.size());
			const glm::vec2 uvs[4] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };
			uint32_t corner = 0;
			for (const glm::vec3& position : corners)
			{
				Vertex vertex;
				vertex.Position = position;
				vertex.UV = uvs[corner++];
				vertex.Normal = normal;
				mesh.Vertices.push_back(vertex);
			}
			mesh.Indices.insert(mesh.Indices.end(), { first, first + 1, first + 2, first + 2, first + 3, first });
		};
		face({ 0.0f, 0.0f, 1.0f }, { { -h, -h, h }, { h, -h, h }, { h, h, h }, { -h, h, h } });
		face({ 0.0f, 0.0f, -1.0f }, { { h, -h, -h }, { -h, -h, -h }, { -h, h, -h }, { h, h, -h } });
		face({ -1.0f, 0.0f, 0.0f }, { { -h, -h, -h }, { -h, -h, h }, { -h, h, h }, { -h, h, -h } });
		face({ 1.0f, 0.0f, 0.0f }, { { h, -h, h }, { h, -h, -h }, { h, h, -h }, { h, h, h } });
		face({ 0.0f, 1.0f, 0.0f }, { { -h, h, h }, { h, h, h }, { h, h, -h }, { -h, h, -h } });
		face({ 0.0f, -1.0f, 0.0f }, { { -h, -h, -h }, { h, -h, -h }, { h, -h, h }, { -h, -h, h } });
		ComputeTangents(mesh);
		return mesh;
	}

	MeshData MakeSphere(float radius, uint32_t rings, uint32_t segments)
	{
		constexpr float Pi = 3.14159265358979f;
		MeshData mesh;
		for (uint32_t ring = 0; ring <= rings; ++ring)
		{
			const float v = static_cast<float>(ring) / static_cast<float>(rings);
			const float theta = v * Pi;
			for (uint32_t segment = 0; segment <= segments; ++segment)
			{
				const float u = static_cast<float>(segment) / static_cast<float>(segments);
				const float phi = u * 2.0f * Pi;
				const glm::vec3 normal(std::sin(theta) * std::cos(phi), std::cos(theta), -std::sin(theta) * std::sin(phi));
				Vertex vertex;
				vertex.Position = normal * radius;
				vertex.Normal = normal;
				vertex.UV = glm::vec2(u, v);
				mesh.Vertices.push_back(vertex);
			}
		}
		// Counter-clockwise seen from outside, like the cube's faces
		for (uint32_t ring = 0; ring < rings; ++ring)
		{
			for (uint32_t segment = 0; segment < segments; ++segment)
			{
				const uint32_t topLeft = ring * (segments + 1) + segment;
				const uint32_t bottomLeft = topLeft + segments + 1;
				mesh.Indices.insert(mesh.Indices.end(), { topLeft, bottomLeft, topLeft + 1, topLeft + 1, bottomLeft, bottomLeft + 1 });
			}
		}
		ComputeTangents(mesh);
		return mesh;
	}

	MeshData MakeCapsule(float radius, float height, uint32_t rings, uint32_t segments)
	{
		constexpr float Pi = 3.14159265358979f;
		const uint32_t halfRings = std::max(rings / 2, 1u);
		const float cylinder = std::max(height - 2.0f * radius, 0.0f);
		MeshData mesh;
		// The top end's rings down to its equator, then the bottom end's from its equator: the band
		// between the two equators is the straight middle
		for (uint32_t row = 0; row <= 2 * halfRings + 1; ++row)
		{
			const bool bTop = row <= halfRings;
			const uint32_t ring = bTop ? row : row - 1;
			const float theta = static_cast<float>(ring) / static_cast<float>(2 * halfRings) * Pi;
			const glm::vec3 center(0.0f, bTop ? radius + cylinder : radius, 0.0f);
			for (uint32_t segment = 0; segment <= segments; ++segment)
			{
				const float u = static_cast<float>(segment) / static_cast<float>(segments);
				const float phi = u * 2.0f * Pi;
				const glm::vec3 normal(std::sin(theta) * std::cos(phi), std::cos(theta), -std::sin(theta) * std::sin(phi));
				Vertex vertex;
				vertex.Position = center + normal * radius;
				vertex.Normal = normal;
				vertex.UV = glm::vec2(u, static_cast<float>(row) / static_cast<float>(2 * halfRings + 1));
				mesh.Vertices.push_back(vertex);
			}
		}
		// Counter-clockwise seen from outside, like the sphere's
		for (uint32_t row = 0; row < 2 * halfRings + 1; ++row)
		{
			for (uint32_t segment = 0; segment < segments; ++segment)
			{
				const uint32_t topLeft = row * (segments + 1) + segment;
				const uint32_t bottomLeft = topLeft + segments + 1;
				mesh.Indices.insert(mesh.Indices.end(), { topLeft, bottomLeft, topLeft + 1, topLeft + 1, bottomLeft, bottomLeft + 1 });
			}
		}
		ComputeTangents(mesh);
		return mesh;
	}

	MeshData MakePlane(float size)
	{
		const float half = size * 0.5f;
		MeshData mesh;
		for (const glm::vec2 corner : { glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 0.0f), glm::vec2(1.0f, 1.0f), glm::vec2(0.0f, 1.0f) })
		{
			Vertex vertex;
			vertex.Position = glm::vec3(corner.x * size - half, 0.0f, corner.y * size - half);
			vertex.Normal = glm::vec3(0.0f, 1.0f, 0.0f);
			vertex.UV = corner;
			mesh.Vertices.push_back(vertex);
		}
		// Counter-clockwise seen from above
		mesh.Indices = { 0, 3, 2, 0, 2, 1 };
		ComputeTangents(mesh);
		return mesh;
	}

	MeshData MakeCrossedQuads(float width, float height, uint32_t rows)
	{
		MeshData mesh;
		rows = std::max(rows, 1u);
		// Along each quad's width; its normal is across(width, up)
		for (const glm::vec3& across : { glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f) })
		{
			const uint32_t first = static_cast<uint32_t>(mesh.Vertices.size());
			const glm::vec3 normal = glm::cross(across, glm::vec3(0.0f, 1.0f, 0.0f));
			for (uint32_t row = 0; row <= rows; ++row)
			{
				const float up = static_cast<float>(row) / static_cast<float>(rows);
				for (const float side : { 0.0f, 1.0f })
				{
					Vertex vertex;
					vertex.Position = across * ((side - 0.5f) * width) + glm::vec3(0.0f, up * height, 0.0f);
					vertex.Normal = normal;
					vertex.UV = glm::vec2(side, 1.0f - up);
					vertex.Color = glm::vec3(0.55f + 0.45f * up);
					vertex.Custom = glm::vec4(up * up, 0.0f, 0.0f, 0.0f);
					mesh.Vertices.push_back(vertex);
				}
			}
			// Counter-clockwise seen from the side the normal faces
			for (uint32_t row = 0; row < rows; ++row)
			{
				const uint32_t below = first + row * 2;
				const uint32_t above = below + 2;
				mesh.Indices.insert(mesh.Indices.end(), { below, below + 1, above + 1, above + 1, above, below });
			}
		}
		ComputeTangents(mesh);
		return mesh;
	}

	Mesh::~Mesh()
	{
		Release();
	}

	void Mesh::SetContext(VulkanContext& context)
	{
		BoundContext = &context;
	}

	bool Mesh::Upload(VulkanContext& context, const MeshData& data)
	{
		BoundContext = &context;

		Release();

		if (!data.Vertices.empty())
		{
			CreateVertexBuffer(context, data.Vertices);
		}

		if (!data.Indices.empty())
		{
			CreateIndexBuffer(context, data.Indices);
		}

		VertexCount = static_cast<uint32_t>(data.Vertices.size());
		IndexCount = static_cast<uint32_t>(data.Indices.size());
		Sections = GetMeshSections(data);

		static uint64_t LastUploadId = 0;
		UploadId = ++LastUploadId;

		return true;
	}

	void Mesh::Release()
	{
		IndexBuffer = nullptr;
		IndexBufferMemory = nullptr;
		IndexBufferOffset = 0;
		IndexCount = 0;

		VertexBuffer = nullptr;
		VertexBufferMemory = nullptr;
		VertexBufferOffset = 0;
		VertexCount = 0;

		Sections.clear();
		UploadId = 0;
	}

	bool Mesh::DoLoad()
	{
		ASSERT(BoundContext && "Mesh::DoLoad requires a bound VulkanContext. Call SetContext() first.");
		if (!BoundContext)
		{
			return false;
		}

		std::string filePath = "models/" + GetId() + ".gltf";

		MeshData data;
		if (!LoadMeshData(filePath, data.Vertices, data.Indices))
		{
			return false;
		}

		return Upload(*BoundContext, data);
	}

	bool Mesh::DoUnload()
	{
		Release();
		return true;
	}

	vk::Buffer Mesh::GetVertexBuffer() const
	{
		return VertexBuffer;
	}

	vk::Buffer Mesh::GetIndexBuffer() const
	{
		return IndexBuffer;
	}

	uint32_t Mesh::GetVertexCount() const
	{
		return VertexCount;
	}

	uint32_t Mesh::GetIndexCount() const
	{
		return IndexCount;
	}

	bool Mesh::LoadMeshData(const std::string& filePath, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices)
	{
		(void)filePath;
		vertices.clear();
		indices.clear();

		ASSERT(false && "LoadMeshData is not implemented yet. Use Upload(context, meshData) for procedural meshes first.");
		return false;
	}

	void Mesh::CreateVertexBuffer(VulkanContext& context, const std::vector<Vertex>& vertices)
	{
		if (vertices.empty())
		{
			return;
		}

		const vk::DeviceSize size = sizeof(GpuVertex) * vertices.size();

		CreateBuffer(
			context,
			size,
			vk::BufferUsageFlagBits::eVertexBuffer,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
			VertexBuffer,
			VertexBufferMemory);

		auto* mapped = static_cast<GpuVertex*>(VertexBufferMemory.mapMemory(0, size));
		std::transform(vertices.begin(), vertices.end(), mapped, ToGpuVertex);
		VertexBufferMemory.unmapMemory();
	}

	void Mesh::CreateIndexBuffer(VulkanContext& context, const std::vector<uint32_t>& indices)
	{
		if (indices.empty())
		{
			return;
		}

		const vk::DeviceSize size = sizeof(uint32_t) * indices.size();

		CreateBuffer(
			context,
			size,
			vk::BufferUsageFlagBits::eIndexBuffer,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
			IndexBuffer,
			IndexBufferMemory);

		void* mapped = IndexBufferMemory.mapMemory(0, size);
		std::memcpy(mapped, indices.data(), static_cast<size_t>(size));
		IndexBufferMemory.unmapMemory();
	}

	vk::Device Mesh::GetDevice()
	{
		ASSERT(false && "Not implemented yet.");
		return vk::Device(); // placeholder!!
	}

} // Nyx