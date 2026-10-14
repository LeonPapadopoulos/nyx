#include "NyxPCH.h"
#include "Mesh.h"
#include "ResourceManager.h"
#include <vulkan/vulkan_raii.hpp>
#include "VulkanContext.h"

#include <cmath>
#include <vector>

namespace
{
	uint32_t FindMemoryType(
		vk::PhysicalDevice physicalDevice,
		uint32_t typeFilter,
		vk::MemoryPropertyFlags properties)
	{
		const vk::PhysicalDeviceMemoryProperties memoryProperties = physicalDevice.getMemoryProperties();

		for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
		{
			const bool bTypeMatches = (typeFilter & (1u << i)) != 0;
			const bool bPropertyMatches =
				(memoryProperties.memoryTypes[i].propertyFlags & properties) == properties;

			if (bTypeMatches && bPropertyMatches)
			{
				return i;
			}
		}

		throw std::runtime_error("Failed to find suitable Vulkan memory type.");
	}

	void CreateBuffer(
		Nyx::VulkanContext& context,
		vk::DeviceSize size,
		vk::BufferUsageFlags usage,
		vk::MemoryPropertyFlags properties,
		vk::raii::Buffer& outBuffer,
		vk::raii::DeviceMemory& outMemory)
	{
		vk::BufferCreateInfo bufferInfo{};
		bufferInfo.size = size;
		bufferInfo.usage = usage;
		bufferInfo.sharingMode = vk::SharingMode::eExclusive;

		outBuffer = vk::raii::Buffer(context.GetDevice(), bufferInfo);

		const vk::MemoryRequirements memRequirements = outBuffer.getMemoryRequirements();

		vk::MemoryAllocateInfo allocInfo{};
		allocInfo.allocationSize = memRequirements.size;
		allocInfo.memoryTypeIndex = FindMemoryType(
			*context.GetPhysicalDevice(),
			memRequirements.memoryTypeBits,
			properties);

		outMemory = vk::raii::DeviceMemory(context.GetDevice(), allocInfo);
		outBuffer.bindMemory(*outMemory, 0);
	}
}

namespace Nyx
{
	void ComputeTangents(MeshData& mesh)
	{
		std::vector<glm::vec3> alongU(mesh.Vertices.size(), glm::vec3(0.0f));
		std::vector<glm::vec3> alongV(mesh.Vertices.size(), glm::vec3(0.0f));
		for (size_t index = 0; index + 2 < mesh.Indices.size(); index += 3)
		{
			const uint32_t corners[3] = { mesh.Indices[index], mesh.Indices[index + 1], mesh.Indices[index + 2] };
			const Vertex& a = mesh.Vertices[corners[0]];
			const Vertex& b = mesh.Vertices[corners[1]];
			const Vertex& c = mesh.Vertices[corners[2]];
			const glm::vec3 edge1 = b.Position - a.Position;
			const glm::vec3 edge2 = c.Position - a.Position;
			const glm::vec2 uv1 = b.UV - a.UV;
			const glm::vec2 uv2 = c.UV - a.UV;
			const float area = uv1.x * uv2.y - uv2.x * uv1.y;
			if (std::abs(area) < 1e-12f)
			{
				continue; // No texture on it, e.g. at a sphere's pole
			}
			const glm::vec3 u = (edge1 * uv2.y - edge2 * uv1.y) / area;
			const glm::vec3 v = (edge2 * uv1.x - edge1 * uv2.x) / area;
			for (const uint32_t corner : corners)
			{
				alongU[corner] += u;
				alongV[corner] += v;
			}
		}

		for (size_t index = 0; index < mesh.Vertices.size(); ++index)
		{
			Vertex& vertex = mesh.Vertices[index];
			const glm::vec3 n = glm::normalize(vertex.Normal);
			glm::vec3 t = alongU[index] - n * glm::dot(n, alongU[index]);
			if (glm::dot(t, t) < 1e-12f)
			{
				// No triangle with a texture: any direction along the surface
				t = glm::cross(std::abs(n.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f), n);
			}
			t = glm::normalize(t);
			// The bitangent points up the image, towards falling v: where a normal map's green points
			const float handedness = glm::dot(glm::cross(n, t), -alongV[index]) < 0.0f ? -1.0f : 1.0f;
			vertex.Tangent = glm::vec4(t, handedness);
		}
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

		const vk::DeviceSize size = sizeof(Vertex) * vertices.size();

		CreateBuffer(
			context,
			size,
			vk::BufferUsageFlagBits::eVertexBuffer,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
			VertexBuffer,
			VertexBufferMemory);

		void* mapped = VertexBufferMemory.mapMemory(0, size);
		std::memcpy(mapped, vertices.data(), static_cast<size_t>(size));
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