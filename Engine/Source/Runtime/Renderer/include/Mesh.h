#pragma once
#include "ResourceManager.h"
#include <vulkan/vulkan.hpp>
#include "VulkanContext.h"

#include <glm/glm.hpp>

namespace Nyx
{
	struct Vertex
	{
		glm::vec3 Position{ 0.0f, 0.0f ,0.0f };
		glm::vec3 Color{ 1.0f, 1.0f, 1.0f };
		glm::vec2 UV{ 0.0f, 0.0f };
		glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };

		static vk::VertexInputBindingDescription GetBindingDescription()
		{
			vk::VertexInputBindingDescription binding{};
			binding.binding = 0;
			binding.stride = sizeof(Vertex);
			binding.inputRate = vk::VertexInputRate::eVertex;
			return binding;
		}

		static std::array<vk::VertexInputAttributeDescription, 4> GetAttributeDescriptions()
		{
			std::array<vk::VertexInputAttributeDescription, 4> attributes{};

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

			return attributes;
		}
	};

	struct MeshData
	{
		std::vector<Vertex> Vertices;
		std::vector<uint32_t> Indices;
	};

	class Mesh : public Nyx::Engine::Resource
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
	};

} // Nyx