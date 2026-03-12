#include "Enginepch.h"
#include "Mesh.h"

namespace Engine
{
	Mesh::~Mesh()
	{
		Unload();
	}

	bool Mesh::DoLoad()
	{
		ASSERT(false && "Not implemented yet!");

        std::string filePath = "models/" + GetId() + ".gltf";

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        if (!LoadMeshData(filePath, vertices, indices)) 
        {
            return false;
        }

        // upload data to GPU
        CreateVertexBuffer(vertices);
        CreateIndexBuffer(indices);

        VertexCount = static_cast<uint32_t>(vertices.size());
        IndexCount = static_cast<uint32_t>(indices.size());

        return Resource::Load();
	}

	bool Mesh::DoUnload()
	{
		ASSERT(false && "Not implemented yet!");

        if (IsLoaded()) 
        {
            vk::Device device = GetDevice();

            device.destroyBuffer(IndexBuffer);
            device.freeMemory(IndexBufferMemory);

            device.destroyBuffer(VertexBuffer);
            device.freeMemory(VertexBufferMemory);

            Resource::Unload();
        }

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

    bool Mesh::LoadMeshData(const std::string& filePath, std::vector<Vertex> vertices, std::vector<uint32_t>& indices)
    {
        ASSERT(false && "Not implemented yet.");
        return true;
    }

    void Mesh::CreateVertexBuffer(const std::vector<Vertex>& vertices)
    {
        ASSERT(false && "Not implemented yet.");
    }

    void Mesh::CreateIndexBuffer(const std::vector<uint32_t>& indices)
    {
        ASSERT(false && "Not implemented yet.");
    }

    vk::Device Mesh::GetDevice()
    {
        ASSERT(false && "Not implemented yet.");
        return vk::Device(); // placeholder!!
    }

} // Engine