#include "NyxPCH.h"
#include "VulkanParticles.h"

#include "Mesh.h"
#include "SceneView.h"
#include "ShaderPrograms.h"
#include "VulkanContext.h"
#include "VulkanRenderer.h"
#include "VulkanUtil.h"
#include "VulkanViewportTarget.h"

#include <cstring>

namespace Nyx
{
	VulkanParticles::VulkanParticles(VulkanContext& context, ShaderPrograms& programs)
		: Context(context)
		, Programs(programs)
	{
	}

	void VulkanParticles::Initialize(vk::PipelineLayout scenePipelineLayout, BindSceneSetsFunction bindSceneSets)
	{
		ScenePipelineLayout = scenePipelineLayout;
		BindSceneSets = std::move(bindSceneSets);

		// The scene as the passes before left it, drawn over
		const vk::AttachmentReference colorReference(0, vk::ImageLayout::eColorAttachmentOptimal);
		vk::SubpassDescription subpass{};
		subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorReference;
		const std::array<vk::SubpassDependency, 2> dependencies = {
			vk::SubpassDependency(VK_SUBPASS_EXTERNAL, 0, vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eFragmentShader,
				vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eShaderRead,
				vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eShaderRead),
			vk::SubpassDependency(0, VK_SUBPASS_EXTERNAL, vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eFragmentShader,
				vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eShaderRead),
		};
		vk::AttachmentDescription color{};
		color.format = VulkanViewportTarget::HdrFormat;
		color.samples = vk::SampleCountFlagBits::e1;
		color.loadOp = vk::AttachmentLoadOp::eLoad;
		color.storeOp = vk::AttachmentStoreOp::eStore;
		color.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
		color.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		color.initialLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		color.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		Pass = vk::raii::RenderPass(Context.GetDevice(), vk::RenderPassCreateInfo({}, color, subpass, dependencies));

		CreatePipelines();
	}

	void VulkanParticles::Shutdown()
	{
		for (auto& shape : Pipelines)
		{
			for (vk::raii::Pipeline& pipeline : shape)
			{
				pipeline = nullptr;
			}
		}
		Pass = nullptr;
		Stream = nullptr;
		StreamMemory = nullptr;
		StreamCapacity = 0;
		RetiredStreams.clear();
	}

	void VulkanParticles::CreatePipelines()
	{
		vk::raii::Device& device = Context.GetDevice();
		vk::raii::ShaderModule fragmentModule = CreateShaderModule(Context, Programs.GetSpirv("Particle.slang", "particleFragment"));

		// The instance stream (ParticleInstanceData) after the vertex's own inputs: at locations 0 to
		// 2 for a billboard, 2 to 4 after a mesh's position and normal at 0 and 1
		const vk::VertexInputBindingDescription meshBinding(0, sizeof(GpuVertex), vk::VertexInputRate::eVertex);
		const std::array<vk::VertexInputAttributeDescription, 2> meshAttributes = {
			vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(GpuVertex, Position)),
			vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(GpuVertex, Normal)),
		};
		const auto instanceAttributes = [](uint32_t binding, uint32_t firstLocation)
		{
			return std::array<vk::VertexInputAttributeDescription, 3>{
				vk::VertexInputAttributeDescription(firstLocation, binding, vk::Format::eR32G32B32A32Sfloat, offsetof(ParticleInstanceData, Position)),
				vk::VertexInputAttributeDescription(firstLocation + 1, binding, vk::Format::eR32G32B32A32Sfloat, offsetof(ParticleInstanceData, Color)),
				vk::VertexInputAttributeDescription(firstLocation + 2, binding, vk::Format::eR32G32B32A32Sfloat, offsetof(ParticleInstanceData, Rotation)),
			};
		};

		vk::PipelineInputAssemblyStateCreateInfo inputAssembly({}, vk::PrimitiveTopology::eTriangleList);
		vk::PipelineViewportStateCreateInfo viewportState{};
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;
		vk::PipelineMultisampleStateCreateInfo multisampling{};
		multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;
		// No depth buffer: each pixel is tested against the scene's depth in the shader
		vk::PipelineDepthStencilStateCreateInfo depthStencil{};
		const std::array<vk::DynamicState, 2> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
		vk::PipelineDynamicStateCreateInfo dynamicState({}, static_cast<uint32_t>(dynamicStates.size()), dynamicStates.data());

		for (const EParticleShape shape : { EParticleShape::Billboard, EParticleShape::Mesh })
		{
			const bool bMesh = shape == EParticleShape::Mesh;
			const std::string vertexName = bMesh ? "particleMeshVertex" : "particleBillboardVertex";
			vk::raii::ShaderModule vertexModule = CreateShaderModule(Context, Programs.GetSpirv("Particle.slang", vertexName));
			const std::array<vk::PipelineShaderStageCreateInfo, 2> stages = {
				vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex, *vertexModule, vertexName.c_str()),
				vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, *fragmentModule, "particleFragment"),
			};

			// A billboard's corners come from the vertex's index: the instances are the only stream
			const uint32_t instanceBinding = bMesh ? 1 : 0;
			const vk::VertexInputBindingDescription instanceStream(instanceBinding, sizeof(ParticleInstanceData), vk::VertexInputRate::eInstance);
			std::vector<vk::VertexInputBindingDescription> bindings;
			std::vector<vk::VertexInputAttributeDescription> attributes;
			if (bMesh)
			{
				bindings.push_back(meshBinding);
				attributes.assign(meshAttributes.begin(), meshAttributes.end());
			}
			bindings.push_back(instanceStream);
			for (const vk::VertexInputAttributeDescription& attribute : instanceAttributes(instanceBinding, bMesh ? 2 : 0))
			{
				attributes.push_back(attribute);
			}
			vk::PipelineVertexInputStateCreateInfo vertexInput({}, bindings, attributes);

			// A mesh's back faces hidden (no depth buffer sorts its faces); the meshes are convex
			vk::PipelineRasterizationStateCreateInfo rasterizer{};
			rasterizer.polygonMode = vk::PolygonMode::eFill;
			rasterizer.lineWidth = 1.0f;
			rasterizer.cullMode = bMesh ? vk::CullModeFlagBits::eBack : vk::CullModeFlagBits::eNone;
			rasterizer.frontFace = vk::FrontFace::eCounterClockwise;

			for (const EParticleBlend blend : { EParticleBlend::Additive, EParticleBlend::Alpha })
			{
				// Premultiplied: additive adds, alpha covers by its alpha; the scene's alpha kept
				vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
				colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB;
				colorBlendAttachment.blendEnable = VK_TRUE;
				colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eOne;
				colorBlendAttachment.dstColorBlendFactor = blend == EParticleBlend::Additive ? vk::BlendFactor::eOne : vk::BlendFactor::eOneMinusSrcAlpha;
				colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
				colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eZero;
				colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eOne;
				colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
				vk::PipelineColorBlendStateCreateInfo colorBlending{};
				colorBlending.attachmentCount = 1;
				colorBlending.pAttachments = &colorBlendAttachment;

				vk::GraphicsPipelineCreateInfo info{};
				info.stageCount = static_cast<uint32_t>(stages.size());
				info.pStages = stages.data();
				info.pVertexInputState = &vertexInput;
				info.pInputAssemblyState = &inputAssembly;
				info.pViewportState = &viewportState;
				info.pRasterizationState = &rasterizer;
				info.pMultisampleState = &multisampling;
				info.pDepthStencilState = &depthStencil;
				info.pColorBlendState = &colorBlending;
				info.pDynamicState = &dynamicState;
				info.layout = ScenePipelineLayout;
				info.renderPass = *Pass;
				Pipelines[static_cast<size_t>(shape)][static_cast<size_t>(blend)] = vk::raii::Pipeline(device, Context.GetPipelineCache(), info);
			}
		}
	}

	void VulkanParticles::Prepare(SceneViewParticles& particles, const VulkanViewportTarget& target, bool bTargetsRemade)
	{
		const vk::Extent2D extent = target.GetExtent();
		if (particles.Extent == extent && !bTargetsRemade && *particles.Framebuffer)
		{
			return;
		}
		const vk::ImageView hdr = target.GetHdrImageView();
		particles.Framebuffer = vk::raii::Framebuffer(Context.GetDevice(), vk::FramebufferCreateInfo({}, *Pass, hdr, extent.width, extent.height, 1));
		particles.Extent = extent;
	}

	void VulkanParticles::Release(SceneViewParticles& particles)
	{
		particles.Framebuffer = nullptr;
		particles.Extent = vk::Extent2D{ 0, 0 };
		particles.Draws.clear();
	}

	void VulkanParticles::Upload(std::span<const ParticleInstanceData> instances)
	{
		if (instances.empty())
		{
			return;
		}
		if (instances.size() > StreamCapacity)
		{
			if (*Stream)
			{
				RetiredStreams.emplace_back(std::move(Stream), std::move(StreamMemory));
			}
			StreamCapacity = std::max<size_t>(instances.size() + instances.size() / 2, 4096);
			CreateBuffer(Context, StreamCapacity * sizeof(ParticleInstanceData), vk::BufferUsageFlagBits::eVertexBuffer,
				vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, Stream, StreamMemory);
		}
		const vk::DeviceSize size = instances.size_bytes();
		void* mapped = StreamMemory.mapMemory(0, size);
		std::memcpy(mapped, instances.data(), static_cast<size_t>(size));
		StreamMemory.unmapMemory();
	}

	void VulkanParticles::ReleaseRetired()
	{
		RetiredStreams.clear();
	}

	void VulkanParticles::Draw(SceneViewInstance& view, vk::raii::CommandBuffer& cmd)
	{
		SceneViewParticles& particles = view.Particles;
		if (particles.Draws.empty() || !*particles.Framebuffer || !*Stream)
		{
			return;
		}

		// The scene's depth, read rather than tested against, until the picture pass tests against it
		const vk::Image depthImage = view.RenderTarget.GetDepthImage();
		const vk::ImageSubresourceRange depthRange(vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1);
		TransitionImageLayout(*cmd, depthImage, depthRange, vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
			vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests, vk::AccessFlagBits::eDepthStencilAttachmentWrite,
			vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eShaderRead);

		const vk::Extent2D extent = particles.Extent;
		vk::RenderPassBeginInfo begin{};
		begin.renderPass = *Pass;
		begin.framebuffer = *particles.Framebuffer;
		begin.renderArea = vk::Rect2D({ 0, 0 }, extent);
		cmd.beginRenderPass(begin, vk::SubpassContents::eInline);
		cmd.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f));
		cmd.setScissor(0, vk::Rect2D({ 0, 0 }, extent));
		BindSceneSets(view, cmd);

		for (const SceneViewParticleDraw& draw : particles.Draws)
		{
			cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *Pipelines[static_cast<size_t>(draw.Shape)][static_cast<size_t>(draw.Blend)]);
			ObjectConstants object{};
			object.CustomData = glm::vec4(draw.SoftDistance, draw.bLit ? 1.0f : 0.0f, draw.Blend == EParticleBlend::Alpha ? 1.0f : 0.0f, 0.0f);
			cmd.pushConstants<ObjectConstants>(ScenePipelineLayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, { object });
			const vk::DeviceSize offset = static_cast<vk::DeviceSize>(draw.FirstInstance) * sizeof(ParticleInstanceData);
			if (draw.Shape == EParticleShape::Mesh && draw.MeshAsset)
			{
				cmd.bindVertexBuffers(0, { draw.MeshAsset->GetVertexBuffer(), *Stream }, { vk::DeviceSize{ 0 }, offset });
				cmd.bindIndexBuffer(draw.MeshAsset->GetIndexBuffer(), 0, vk::IndexType::eUint32);
				cmd.drawIndexed(draw.MeshAsset->GetIndexCount(), draw.InstanceCount, 0, 0, 0);
			}
			else if (draw.Shape == EParticleShape::Billboard)
			{
				cmd.bindVertexBuffers(0, { *Stream }, { offset });
				cmd.draw(6, draw.InstanceCount, 0, 0);
			}
		}
		cmd.endRenderPass();

		TransitionImageLayout(*cmd, depthImage, depthRange, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eDepthStencilAttachmentOptimal,
			vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eShaderRead, vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests,
			vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite);
	}
}
