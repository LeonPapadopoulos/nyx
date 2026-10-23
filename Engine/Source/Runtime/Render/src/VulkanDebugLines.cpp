#include "NyxPCH.h"
#include "VulkanDebugLines.h"

#include "DebugDraw.h"
#include "SceneView.h"
#include "ShaderPrograms.h"
#include "VulkanContext.h"
#include "VulkanUtil.h"

#include <cstring>

namespace Nyx
{
	VulkanDebugLines::VulkanDebugLines(VulkanContext& context, ShaderPrograms& programs)
		: Context(context)
		, Programs(programs)
	{
	}

	void VulkanDebugLines::Initialize(vk::PipelineLayout scenePipelineLayout, BindSceneSetsFunction bindSceneSets)
	{
		ScenePipelineLayout = scenePipelineLayout;
		BindSceneSets = std::move(bindSceneSets);
		CreateBuffer(
			Context,
			sizeof(DebugLineVertex) * MaxVertices,
			vk::BufferUsageFlagBits::eVertexBuffer,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
			VertexBuffer,
			VertexBufferMemory);
	}

	void VulkanDebugLines::CreatePipeline(const SceneViewInstance& firstView)
	{
		const std::vector<uint32_t> vertCode = Programs.GetSpirv("DebugLines.vert");
		const std::vector<uint32_t> fragCode = Programs.GetSpirv("DebugLines.frag");

		vk::raii::ShaderModule vertShaderModule = CreateShaderModule(Context, vertCode);
		vk::raii::ShaderModule fragShaderModule = CreateShaderModule(Context, fragCode);

		vk::PipelineShaderStageCreateInfo vertStageInfo{};
		vertStageInfo.stage = vk::ShaderStageFlagBits::eVertex;
		vertStageInfo.module = *vertShaderModule;
		vertStageInfo.pName = "main";

		vk::PipelineShaderStageCreateInfo fragStageInfo{};
		fragStageInfo.stage = vk::ShaderStageFlagBits::eFragment;
		fragStageInfo.module = *fragShaderModule;
		fragStageInfo.pName = "main";

		vk::PipelineShaderStageCreateInfo shaderStages[] = { vertStageInfo, fragStageInfo };

		const auto bindingDescription = DebugLineVertex::GetBindingDescription();
		const auto attributeDescriptions = DebugLineVertex::GetAttributeDescriptions();

		vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
		vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
		vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

		vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
		inputAssembly.topology = vk::PrimitiveTopology::eLineList;
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		vk::PipelineViewportStateCreateInfo viewportState{};
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;

		vk::PipelineRasterizationStateCreateInfo rasterizer{};
		rasterizer.depthClampEnable = VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode = vk::PolygonMode::eFill;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = vk::CullModeFlagBits::eNone;
		rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
		rasterizer.depthBiasEnable = VK_FALSE;

		vk::PipelineMultisampleStateCreateInfo multisampling{};
		multisampling.sampleShadingEnable = VK_FALSE;
		multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

		vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
		colorBlendAttachment.blendEnable = VK_FALSE;
		colorBlendAttachment.colorWriteMask =
			vk::ColorComponentFlagBits::eR |
			vk::ColorComponentFlagBits::eG |
			vk::ColorComponentFlagBits::eB |
			vk::ColorComponentFlagBits::eA;

		vk::PipelineColorBlendStateCreateInfo colorBlending{};
		colorBlending.logicOpEnable = VK_FALSE;
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &colorBlendAttachment;

		std::array<vk::DynamicState, 2> dynamicStates = {
			vk::DynamicState::eViewport,
			vk::DynamicState::eScissor
		};

		vk::PipelineDynamicStateCreateInfo dynamicState{};
		dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
		dynamicState.pDynamicStates = dynamicStates.data();

		vk::PipelineDepthStencilStateCreateInfo depthStencil{};
		depthStencil.depthTestEnable = VK_TRUE;
		depthStencil.depthWriteEnable = VK_FALSE;
		depthStencil.depthCompareOp = DepthTestNearerOrEqual;
		depthStencil.depthBoundsTestEnable = VK_FALSE;
		depthStencil.stencilTestEnable = VK_FALSE;

		vk::GraphicsPipelineCreateInfo pipelineInfo{};
		pipelineInfo.stageCount = 2;
		pipelineInfo.pStages = shaderStages;
		pipelineInfo.pVertexInputState = &vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState = &multisampling;
		pipelineInfo.pDepthStencilState = &depthStencil;
		pipelineInfo.pColorBlendState = &colorBlending;
		pipelineInfo.pDynamicState = &dynamicState;
		pipelineInfo.layout = ScenePipelineLayout;
		pipelineInfo.renderPass = *firstView.RenderTarget.GetRenderPass();
		pipelineInfo.subpass = 0;

		Pipeline = std::move(vk::raii::Pipeline(Context.GetDevice(), Context.GetPipelineCache(), pipelineInfo));
	}

	void VulkanDebugLines::Reset()
	{
		Vertices.clear();
	}

	DebugLineRange VulkanDebugLines::Add(const DebugDraw& drawing)
	{
		DebugLineRange range{ static_cast<uint32_t>(Vertices.size()), 0 };
		for (const DebugLine& line : drawing.GetLines())
		{
			// The buffer is full: the rest of this frame's lines aren't drawn
			if (Vertices.size() + 2 > MaxVertices)
			{
				break;
			}
			Vertices.push_back(DebugLineVertex{ line.From, line.Color });
			Vertices.push_back(DebugLineVertex{ line.To, line.Color });
		}
		range.VertexCount = static_cast<uint32_t>(Vertices.size()) - range.FirstVertex;
		return range;
	}

	void VulkanDebugLines::Upload()
	{
		if (Vertices.empty())
		{
			return;
		}

		const vk::DeviceSize uploadSize = sizeof(DebugLineVertex) * Vertices.size();
		void* mapped = VertexBufferMemory.mapMemory(0, uploadSize);
		std::memcpy(mapped, Vertices.data(), static_cast<size_t>(uploadSize));
		VertexBufferMemory.unmapMemory();
	}

	void VulkanDebugLines::Draw(SceneViewInstance& view, DebugLineRange range, vk::raii::CommandBuffer& cmd)
	{
		if (range.VertexCount == 0)
		{
			return;
		}

		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *Pipeline);
		BindSceneSets(view, cmd);

		vk::DeviceSize offsets[] = { 0 };
		cmd.bindVertexBuffers(0, { *VertexBuffer }, offsets);
		cmd.draw(range.VertexCount, 1, range.FirstVertex, 0);
	}
}
