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

	// Every mesh pipeline's second vertex stream: the instance's transform, a glm::mat4 per
	// instance, relative to the draw's ObjectConstants::Model (MeshInstanceCache; the identity for
	// a draw without instances). Its columns at locations 5 to 8, after the vertex's.
	constexpr uint32_t InstanceBinding = 1;

	inline vk::VertexInputBindingDescription GetInstanceBindingDescription()
	{
		vk::VertexInputBindingDescription binding{};
		binding.binding = InstanceBinding;
		binding.stride = sizeof(glm::mat4);
		binding.inputRate = vk::VertexInputRate::eInstance;
		return binding;
	}

	inline std::array<vk::VertexInputAttributeDescription, 4> GetInstanceAttributeDescriptions()
	{
		std::array<vk::VertexInputAttributeDescription, 4> attributes{};
		for (uint32_t column = 0; column < 4; ++column)
		{
			attributes[column].binding = InstanceBinding;
			attributes[column].location = 5 + column;
			attributes[column].format = vk::Format::eR32G32B32A32Sfloat;
			attributes[column].offset = column * sizeof(glm::vec4);
		}
		return attributes;
	}

	// The built-in meshes wind counter-clockwise seen from outside, as glTF's do: the side their
	// normals face is their front, which the renderer draws (SetMeshFaces()).

	// A cube of side `size` around the origin, each face its own four vertices and the whole UV
	// square, with tangents
	MeshData MakeCube(float size);

	// A sphere of `radius` around the origin: `rings` from pole to pole, `segments` around, u
	// around it and v from top to bottom, with tangents
	MeshData MakeSphere(float radius, uint32_t rings, uint32_t segments);

	// A capsule of `radius`, `height` from end to end, standing upright on the origin as a character
	// does (CharacterComponent): `rings` (even) over its two round ends from pole to pole, `segments`
	// around, u around it and v from top to bottom, with tangents
	MeshData MakeCapsule(float radius, float height, uint32_t rings, uint32_t segments);

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
		// What each material slot draws (GetMeshSections()), from the last upload
		const std::vector<MeshSection>& GetSections() const { return Sections; }
		// New with every upload of any mesh, never 0 once uploaded: tells buffers apart that the
		// driver may give the same handles as ones destroyed before
		uint64_t GetUploadId() const { return UploadId; }

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

		std::vector<MeshSection> Sections;
		uint64_t UploadId = 0;

		glm::vec3 LocalBoundsMin{ -0.5f, -0.5f, -0.5f };
		glm::vec3 LocalBoundsMax{ 0.5f, 0.5f, 0.5f };
	};

} // Nyx