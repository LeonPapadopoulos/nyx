#pragma once
#include "MeshData.h"
#include "Resource.h"
#include <vulkan/vulkan.hpp>
#include "VulkanContext.h"

#include <glm/glm.hpp>

namespace Nyx
{
	// How Vertex is laid out for the vertex shaders
	inline vk::VertexInputBindingDescription GetVertexBindingDescription()
	{
		vk::VertexInputBindingDescription binding{};
		binding.binding = 0;
		binding.stride = sizeof(Vertex);
		binding.inputRate = vk::VertexInputRate::eVertex;
		return binding;
	}

	inline std::array<vk::VertexInputAttributeDescription, 5> GetVertexAttributeDescriptions()
	{
		std::array<vk::VertexInputAttributeDescription, 5> attributes{};

		attributes[0].binding = 0;
		attributes[0].location = 0;
		attributes[0].format = vk::Format::eR32G32B32Sfloat;
		attributes[0].offset = offsetof(Vertex, Position);

		attributes[1].binding = 0;
		attributes[1].location = 1;
		attributes[1].format = vk::Format::eR32G32B32Sfloat;
		attributes[1].offset = offsetof(Vertex, Color);

		attributes[2].binding = 0;
		attributes[2].location = 2;
		attributes[2].format = vk::Format::eR32G32Sfloat;
		attributes[2].offset = offsetof(Vertex, UV);

		attributes[3].binding = 0;
		attributes[3].location = 3;
		attributes[3].format = vk::Format::eR32G32B32Sfloat;
		attributes[3].offset = offsetof(Vertex, Normal);

		attributes[4].binding = 0;
		attributes[4].location = 4;
		attributes[4].format = vk::Format::eR32G32B32A32Sfloat;
		attributes[4].offset = offsetof(Vertex, Tangent);

		return attributes;
	}

	// Each vertex's tangent from the triangles around it: their direction of rising u, made
	// perpendicular to the normal, and the side of falling v (Lengyel). Exact on flat faces; on
	// curved ones the mean of the faces, as close as needed for the built-in meshes. Imported meshes
	// will take their file's tangents, or MikkTSpace's.
	void ComputeTangents(MeshData& mesh);

	// A sphere of `radius` around the origin: `rings` from pole to pole, `segments` around, u
	// around it and v from top to bottom, with tangents
	MeshData MakeSphere(float radius, uint32_t rings, uint32_t segments);

	// A square of side `size` on the ground (y = 0) around the origin, facing up, with u along x
	// and v along z, with tangents
	MeshData MakePlane(float size);

	class Mesh : public Nyx::Resource
	{
	public:
		explicit Mesh(const std::string& id)
			: Resource(id)
		{
		}

		~Mesh() override;

		// @todo: How else can we get the VulkanContext info during DoLoad()
		void SetContext(VulkanContext& context);

		bool Upload(VulkanContext& context, const MeshData& data);
		void Release();

		bool DoLoad() override;
		bool DoUnload() override;

		vk::Buffer GetVertexBuffer() const;
		vk::Buffer GetIndexBuffer() const;
		uint32_t GetVertexCount() const;
		uint32_t GetIndexCount() const;

		void SetLocalBounds(const glm::vec3& minBounds, const glm::vec3& maxBounds)
		{
			LocalBoundsMin = minBounds;
			LocalBoundsMax = maxBounds;
		}

		const glm::vec3& GetLocalBoundsMin() const
		{
			return LocalBoundsMin;
		}

		const glm::vec3& GetLocalBoundsMax() const
		{
			return LocalBoundsMax;
		}

	private:
		bool LoadMeshData(const std::string& filePath, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);
		void CreateVertexBuffer(VulkanContext& context, const std::vector<Vertex>& vertices);
		void CreateIndexBuffer(VulkanContext& context, const std::vector<uint32_t>& indices);
		vk::Device GetDevice();

	private:
		VulkanContext* BoundContext = nullptr;

		vk::raii::Buffer VertexBuffer{ nullptr };
		vk::raii::DeviceMemory VertexBufferMemory{ nullptr };
		vk::DeviceSize VertexBufferOffset = 0;
		uint32_t VertexCount = 0;

		vk::raii::Buffer IndexBuffer{ nullptr };
		vk::raii::DeviceMemory IndexBufferMemory{ nullptr };
		vk::DeviceSize IndexBufferOffset = 0;
		uint32_t IndexCount = 0;

		glm::vec3 LocalBoundsMin{ -0.5f, -0.5f, -0.5f };
		glm::vec3 LocalBoundsMax{ 0.5f, 0.5f, 0.5f };
	};

} // Nyx