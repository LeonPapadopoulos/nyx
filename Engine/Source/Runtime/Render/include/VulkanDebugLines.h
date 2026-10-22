#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace Nyx
{
	class DebugDraw;
	class ShaderPrograms;
	class VulkanContext;
	struct SceneViewInstance;

	struct DebugLineVertex
	{
		glm::vec3 Position{ 0.0f };
		glm::vec3 Color{ 1.0f };

		static vk::VertexInputBindingDescription GetBindingDescription()
		{
			vk::VertexInputBindingDescription binding{};
			binding.binding = 0;
			binding.stride = sizeof(DebugLineVertex);
			binding.inputRate = vk::VertexInputRate::eVertex;
			return binding;
		}

		static std::array<vk::VertexInputAttributeDescription, 2> GetAttributeDescriptions()
		{
			std::array<vk::VertexInputAttributeDescription, 2> attributes{};

			attributes[0].binding = 0;
			attributes[0].location = 0;
			attributes[0].format = vk::Format::eR32G32B32Sfloat;
			attributes[0].offset = offsetof(DebugLineVertex, Position);

			attributes[1].binding = 0;
			attributes[1].location = 1;
			attributes[1].format = vk::Format::eR32G32B32Sfloat;
			attributes[1].offset = offsetof(DebugLineVertex, Color);

			return attributes;
		}
	};

	// Where one DebugDraw's lines are in this frame's line buffer
	struct DebugLineRange
	{
		uint32_t FirstVertex = 0;
		uint32_t VertexCount = 0;
	};

	// Draws DebugDraw lines into the scene views' pictures, depth tested against the scene: every
	// drawing's lines gathered into one buffer each frame (Reset(), Add() per drawing, Upload()),
	// then drawn range by range. Used for the worlds' debug drawing and by the editor's overlays.
	class VulkanDebugLines
	{
	public:
		// Binds the scene pipelines' two sets, the global one and the view's, for a draw into the view
		using BindSceneSetsFunction = std::function<void(SceneViewInstance& view, vk::raii::CommandBuffer& cmd)>;

		// Both must outlive it
		VulkanDebugLines(VulkanContext& context, ShaderPrograms& programs);

		// The line buffer; drawn with the scene pipelines' layout, which must outlive it
		void Initialize(vk::PipelineLayout scenePipelineLayout, BindSceneSetsFunction bindSceneSets);
		// Once the first view is made, and again when DebugLines.vert or .frag changed: the pipeline
		// for its picture pass, which every view's is like
		void CreatePipeline(const SceneViewInstance& firstView);

		// The frame's lines: emptied, added drawing by drawing, then uploaded
		void Reset();
		DebugLineRange Add(const DebugDraw& drawing);
		void Upload();

		// Inside the view's picture pass
		void Draw(SceneViewInstance& view, DebugLineRange range, vk::raii::CommandBuffer& cmd);

	private:
		static constexpr uint32_t MaxVertices = 131072;

		VulkanContext& Context;
		ShaderPrograms& Programs;
		vk::PipelineLayout ScenePipelineLayout;
		BindSceneSetsFunction BindSceneSets;

		std::vector<DebugLineVertex> Vertices;
		vk::raii::Buffer VertexBuffer{ nullptr };
		vk::raii::DeviceMemory VertexBufferMemory{ nullptr };
		vk::raii::Pipeline Pipeline{ nullptr };
	};
}
