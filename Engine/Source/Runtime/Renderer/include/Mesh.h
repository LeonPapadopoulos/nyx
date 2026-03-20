#pragma once
// #include "ResourceManager.h"
#include <vulkan/vulkan.hpp>

// Placeholder
struct Vertex
{
	// @todo:
};

namespace Nyx
{
	namespace Renderer
	{
		// class Mesh : public Resource
		// {
		// public:
		// 	explicit Mesh(const std::string& id)
		// 		: Resource(id)
		// 	{
		// 	}

		// 	~Mesh() override;

		// 	bool DoLoad() override;
		// 	bool DoUnload() override;

		// 	vk::Buffer GetVertexBuffer() const;
		// 	vk::Buffer GetIndexBuffer() const;
		// 	uint32_t GetVertexCount() const;
		// 	uint32_t GetIndexCount() const;

		// private:
		// 	bool LoadMeshData(const std::string& filePath, std::vector<Vertex> vertices, std::vector<uint32_t>& indices);
		// 	void CreateVertexBuffer(const std::vector<Vertex>& vertices);
		// 	void CreateIndexBuffer(const std::vector<uint32_t>& indices);
		// 	vk::Device GetDevice();

		// private:
		// 	vk::Buffer VertexBuffer;
		// 	vk::DeviceMemory VertexBufferMemory;
		// 	vk::DeviceSize VertexBufferOffset;
		// 	uint32_t VertexCount = 0;

		// 	vk::Buffer IndexBuffer;
		// 	vk::DeviceMemory IndexBufferMemory;
		// 	vk::DeviceSize IndexBufferOffset;
		// 	uint32_t IndexCount = 0;
		// };

	} // Renderer
} // Nyx