#pragma once
#include "MeshData.h"
#include "Resource.h"
#include <vulkan/vulkan.hpp>
#include "VulkanContext.h"

#include <glm/glm.hpp>

#include <array>
#include <vector>

namespace Nyx
{
	// A vertex as the GPU reads it: Vertex with its custom channels in half precision, so a mesh
	// pays 8 bytes for them, not 16 (68 bytes a vertex, from 60 before they came). Made from a
	// Vertex on upload (ToGpuVertex()).
	struct GpuVertex
	{
		glm::vec3 Position{ 0.0f };
		glm::vec3 Color{ 1.0f };
		glm::vec2 UV{ 0.0f };
		glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };
		glm::vec4 Tangent{ 1.0f, 0.0f, 0.0f, 1.0f };
		// Vertex::Custom as four half floats, x and y in the first, z and w in the second
		// (glm::packHalf2x16())
		uint32_t Custom[2] = {};
	};
	static_assert(sizeof(GpuVertex) == 68, "GpuVertex is the vertex buffer's stride");

	GpuVertex ToGpuVertex(const Vertex& vertex);
	// Vertex::Custom back from the GPU's half floats, for tests
	glm::vec4 GetGpuVertexCustom(const GpuVertex& vertex);

	// How GpuVertex is laid out for the vertex shaders
	inline vk::VertexInputBindingDescription GetVertexBindingDescription()
	{
		vk::VertexInputBindingDescription binding{};
		binding.binding = 0;
		binding.stride = sizeof(GpuVertex);
		binding.inputRate = vk::VertexInputRate::eVertex;
		return binding;
	}

	// Locations 0 to 4 and 9: the custom channels after the instance's transform (5 to 8), which
	// the engine's GLSL shaders read where they always were
	constexpr uint32_t VertexCustomLocation = 9;

	inline std::array<vk::VertexInputAttributeDescription, 6> GetVertexAttributeDescriptions()
	{
		std::array<vk::VertexInputAttributeDescription, 6> attributes{};

		attributes[0].binding = 0;
		attributes[0].location = 0;
		attributes[0].format = vk::Format::eR32G32B32Sfloat;
		attributes[0].offset = offsetof(GpuVertex, Position);

		attributes[1].binding = 0;
		attributes[1].location = 1;
		attributes[1].format = vk::Format::eR32G32B32Sfloat;
		attributes[1].offset = offsetof(GpuVertex, Color);

		attributes[2].binding = 0;
		attributes[2].location = 2;
		attributes[2].format = vk::Format::eR32G32Sfloat;
		attributes[2].offset = offsetof(GpuVertex, UV);

		attributes[3].binding = 0;
		attributes[3].location = 3;
		attributes[3].format = vk::Format::eR32G32B32Sfloat;
		attributes[3].offset = offsetof(GpuVertex, Normal);

		attributes[4].binding = 0;
		attributes[4].location = 4;
		attributes[4].format = vk::Format::eR32G32B32A32Sfloat;
		attributes[4].offset = offsetof(GpuVertex, Tangent);

		attributes[5].binding = 0;
		attributes[5].location = VertexCustomLocation;
		attributes[5].format = vk::Format::eR16G16B16A16Sfloat;
		attributes[5].offset = offsetof(GpuVertex, Custom);

		return attributes;
	}

	// Every mesh pipeline's second vertex stream: an instance a MeshInstanceData, its transform
	// relative to the draw's ObjectConstants::Model and its custom numbers (MeshInstanceCache; the
	// identity and zero for a draw without instances). The transform's columns at locations 5 to 8,
	// after the vertex's, its custom numbers at 10.
	constexpr uint32_t InstanceBinding = 1;
	constexpr uint32_t InstanceCustomLocation = 10;

	struct MeshInstanceData
	{
		glm::mat4 Transform{ 1.0f };
		// MeshInstances::Custom of an instance, or MeshRendererComponent::Custom of an entity drawn
		// once (SurfaceInput.InstanceCustom)
		glm::vec4 Custom{ 0.0f };
	};
	static_assert(sizeof(MeshInstanceData) == 80, "MeshInstanceData is the instance stream's stride");

	inline vk::VertexInputBindingDescription GetInstanceBindingDescription()
	{
		vk::VertexInputBindingDescription binding{};
		binding.binding = InstanceBinding;
		binding.stride = sizeof(MeshInstanceData);
		binding.inputRate = vk::VertexInputRate::eInstance;
		return binding;
	}

	inline std::array<vk::VertexInputAttributeDescription, 5> GetInstanceAttributeDescriptions()
	{
		std::array<vk::VertexInputAttributeDescription, 5> attributes{};
		for (uint32_t column = 0; column < 4; ++column)
		{
			attributes[column].binding = InstanceBinding;
			attributes[column].location = 5 + column;
			attributes[column].format = vk::Format::eR32G32B32A32Sfloat;
			attributes[column].offset = offsetof(MeshInstanceData, Transform) + column * sizeof(glm::vec4);
		}
		attributes[4].binding = InstanceBinding;
		attributes[4].location = InstanceCustomLocation;
		attributes[4].format = vk::Format::eR32G32B32A32Sfloat;
		attributes[4].offset = offsetof(MeshInstanceData, Custom);
		return attributes;
	}

	// The attributes of both streams (GetVertexAttributeDescriptions(),
	// GetInstanceAttributeDescriptions()) that a vertex shader reads: the locations of its inputs.
	// Slang leaves out inputs nothing uses, and attributes a shader doesn't read only slow the GPU.
	std::vector<vk::VertexInputAttributeDescription> GetMeshAttributesRead(const std::vector<uint32_t>& vertexSpirv);

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

	// Two upright quads crossed at right angles, `width` wide and `height` tall, standing on the
	// origin, facing +Z and +X: the usual stand-in for small plants, drawn with a two-sided
	// material. Each is cut in `rows` from the foot up, u across and v from the top down, with
	// tangents. Custom.x is the weight a swaying material bends a vertex by (VertexColorWind): the
	// square of its height's fraction, 0 at the foot to 1 at the top, so a lean curves. The colour
	// darkens from white at the top to 0.55 at the foot, where less light reaches.
	MeshData MakeCrossedQuads(float width, float height, uint32_t rows);

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
		// As GpuVertex
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