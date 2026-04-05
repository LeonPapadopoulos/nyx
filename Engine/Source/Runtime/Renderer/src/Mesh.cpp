#include "NyxPCH.h"
#include "Mesh.h"
#include "ResourceManager.h"
#include <vulkan/vulkan_raii.hpp>
#include "VulkanContext.h"

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