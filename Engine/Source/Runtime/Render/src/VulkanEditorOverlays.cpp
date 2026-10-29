#include "NyxPCH.h"
#include "VulkanEditorOverlays.h"

#include "Assertions.h"
#include "CameraProjection.h"
#include "Log.h"
#include "SceneView.h"
#include "ShaderPrograms.h"
#include "VulkanContext.h"
#include "VulkanDebugLines.h"
#include "VulkanRenderer.h"
#include "VulkanUtil.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <span>
#include <vector>

namespace Nyx
{
	VulkanEditorOverlays::VulkanEditorOverlays(VulkanContext& context, ShaderPrograms& programs, VulkanDebugLines& debugLines)
		: Context(context)
		, Programs(programs)
		, DebugLines(debugLines)
	{
	}

	void VulkanEditorOverlays::Initialize(vk::PipelineLayout scenePipelineLayout, BindSceneSetsFunction bindSceneSets, const Mesh& decalMesh, const Mesh& iconMesh, vk::Buffer identityInstance)
	{
		ScenePipelineLayout = scenePipelineLayout;
		BindSceneSets = std::move(bindSceneSets);
		DecalMesh = &decalMesh;
		IconMesh = &iconMesh;
		IdentityInstance = identityInstance;
		CreateOutlineDescriptorSetLayout();
	}

	void VulkanEditorOverlays::DrawObjectMesh(const RenderWorld& world, const RenderObject& obj, ObjectConstants object, EMeshPass pass, vk::Pipeline& bound, vk::raii::CommandBuffer& cmd)
	{
		cmd.bindVertexBuffers(0, { obj.MeshAsset->GetVertexBuffer(), obj.InstanceBuffer }, { vk::DeviceSize{ 0 }, vk::DeviceSize{ 0 } });
		cmd.bindIndexBuffer(obj.MeshAsset->GetIndexBuffer(), 0, vk::IndexType::eUint32);
		// Every instance its cells draw, culled or not: a click may be anywhere in the view
		std::vector<InstanceRun> runs;
		if (obj.CellCount == 0)
		{
			runs.push_back(InstanceRun{ .First = 0, .Count = 1 });
		}
		else
		{
			AppendInstanceRuns(std::span(world.Cells).subspan(obj.FirstCell, obj.CellCount), nullptr, runs);
		}
		// Drawn once, from the identity buffer: its custom numbers with the draw, for a material's
		// vertex function
		if (obj.CellCount == 0)
		{
			object.InstanceCustom = obj.Custom;
		}
		// Section by section, with the faces its material draws, and its vertex function if it has one
		for (uint32_t index = obj.FirstSection; index < obj.FirstSection + obj.SectionCount; ++index)
		{
			const RenderSection& section = world.Sections[index];
			const Material* material = section.MaterialAsset;
			vk::raii::Pipeline* own = nullptr;
			vk::Pipeline pipeline;
			switch (pass)
			{
			case EMeshPass::Picking:
				own = material ? material->PickingPipeline : nullptr;
				pipeline = *PickingPipeline;
				break;
			case EMeshPass::SelectionMaskVisible:
				own = material ? material->SelectionMaskVisiblePipeline : nullptr;
				pipeline = *SelectionMaskPipelineVisible;
				break;
			case EMeshPass::SelectionMaskFull:
				own = material ? material->SelectionMaskFullPipeline : nullptr;
				pipeline = *SelectionMaskPipelineFull;
				break;
			}
			if (own)
			{
				pipeline = **own;
			}
			if (pipeline != bound)
			{
				cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
				bound = pipeline;
			}
			object.MaterialOffset = material ? material->MaterialOffset : 0;
			cmd.pushConstants<ObjectConstants>(ScenePipelineLayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, { object });
			SetMeshFaces(cmd, material && material->bTwoSided, obj.bMirrored);
			for (const InstanceRun& run : runs)
			{
				cmd.drawIndexed(section.IndexCount, run.Count, section.FirstIndex, 0, run.First);
			}
		}
	}

	void VulkanEditorOverlays::CreatePipelines(const SceneViewInstance& firstView)
	{
		CreateSelectionMaskPipelines(firstView);
		CreateOutlineCompositePipeline(firstView);
		CreateGridPipeline(firstView);
		CreatePickingPipeline(firstView);
	}

	void VulkanEditorOverlays::RebuildPipelines(const std::function<bool(const char* shader)>& uses, const SceneViewInstance& firstView)
	{
		if (uses("Grid"))
		{
			CreateGridPipeline(firstView);
		}
		if (uses("Picking"))
		{
			CreatePickingPipeline(firstView);
		}
		if (uses("SelectionMask"))
		{
			CreateSelectionMaskPipelines(firstView);
		}
		if (uses("OutlineComposite"))
		{
			CreateOutlineCompositePipeline(firstView);
		}
	}

	void VulkanEditorOverlays::SetWorld(const Nyx::Registry* world)
	{
		World = world;
		SelectedEntity.reset();
	}

	void VulkanEditorOverlays::SetSelectedEntity(std::optional<Nyx::Entity> entity)
	{
		SelectedEntity = entity;
	}

	void VulkanEditorOverlays::CreateViewResources(SceneViewInstance& view)
	{
		ASSERT(OutlineDescriptorSetLayout != nullptr);
		ASSERT(OutlineCompositeSampler != nullptr);

		CreatePickingResourcesForView(view);
		CreateSelectionMaskResourcesForView(view);
		CreateOutlineDescriptorSetForView(view);
	}

	void VulkanEditorOverlays::RecreateViewResources(SceneViewInstance& view)
	{
		RecreatePickingResourcesForView(view);
		RecreateSelectionMaskResourcesForView(view);
	}

	void VulkanEditorOverlays::DestroyViewResources(SceneViewInstance& view)
	{
		DestroyPickingResourcesForView(view);
		DestroySelectionMaskResourcesForView(view);
	}

	void VulkanEditorOverlays::DrawPasses(SceneViewInstance& view, const RenderWorld& world, vk::raii::CommandBuffer& cmd)
	{
		// Picking only for a click: the frame after it, as when it ran every frame
		if (view.Overlays.bPickRequestPending)
		{
			if (view.Overlays.PickWorldRevision == world.Revision)
			{
				DrawPickingPass(view, world, cmd);
			}
			ResolvePickRequest(view, world, cmd);
		}
		// This view shows its world's current revision: clicks on it are taken from now on.
		view.Overlays.RenderedWorldRevision = world.Revision;

		// The masks only while something is selected, and only when what they show
		// changed; between those, they keep what they last held.
		const std::optional<uint64_t> fingerprint = GetSelectionMaskFingerprint(view, world);
		if (fingerprint.has_value() && fingerprint != view.Overlays.SelectionMaskFingerprint)
		{
			DrawVisibleSelectionMaskPass(view, world, cmd);
			DrawFullSelectionMaskPass(view, world, cmd);
		}
		view.Overlays.SelectionMaskFingerprint = fingerprint;
	}

	void VulkanEditorOverlays::Draw(SceneViewInstance& view, const RenderWorld* world, vk::raii::CommandBuffer& cmd)
	{
		DrawGrid(view, cmd);

		// The outline is drawn from the selection masks, which are only rendered with a world
		if (world)
		{
			DebugLines.Draw(view, world->OverlayLines, cmd);
			DrawSelectionOutline(view, cmd);
		}
	}

	void VulkanEditorOverlays::AddDebugLines(RenderWorld& world)
	{
		// Each object's axes and bounds
		OverlayDrawing.Clear();
		for (const RenderObject& obj : world.Objects)
		{
			OverlayDrawing.Axes(obj.WorldTransform, 0.75f);
			OverlayDrawing.Box(obj.WorldTransform, obj.LocalBoundsMin, obj.LocalBoundsMax, glm::vec3(1.0f, 1.0f, 0.2f));
		}

		// Each point light's icon, a star of seven lines in its colour at its brightest: along the
		// axes and the diagonals, few lines for many lights. A click on the ball round it picks the
		// light (DrawPickingPass()).
		for (const RenderPointLight& light : world.PointLights)
		{
			if (!light.bIcon)
			{
				continue;
			}
			const glm::vec3 color = light.Color / std::max(light.Color.r, std::max(light.Color.g, light.Color.b));
			const float diagonal = PointLightIconRadius / std::sqrt(3.0f);
			for (const glm::vec3 arm : { glm::vec3(PointLightIconRadius, 0.0f, 0.0f), glm::vec3(0.0f, PointLightIconRadius, 0.0f), glm::vec3(0.0f, 0.0f, PointLightIconRadius),
					 glm::vec3(diagonal, diagonal, diagonal), glm::vec3(-diagonal, diagonal, diagonal), glm::vec3(diagonal, -diagonal, diagonal), glm::vec3(diagonal, diagonal, -diagonal) })
			{
				OverlayDrawing.Line(light.WorldPosition - arm, light.WorldPosition + arm, color);
			}
		}

		// Each particle emitter's icon: a small box of lines, which a click picks too
		for (const RenderParticleEmitter& emitter : world.Particles.Emitters)
		{
			OverlayDrawing.Box(emitter.WorldPosition - glm::vec3(ParticleEmitterIconRadius * 0.6f), emitter.WorldPosition + glm::vec3(ParticleEmitterIconRadius * 0.6f),
				glm::vec3(0.85f, 0.85f, 0.85f));
		}

		// Each lightning entity's icon: a small zig-zag of lines, pale yellow, which a click picks too
		for (const RenderLightningBolt& bolt : world.Lightning)
		{
			const glm::vec3 color(1.0f, 0.95f, 0.55f);
			const float r = LightningIconRadius;
			const glm::vec3 p = bolt.WorldPosition;
			OverlayDrawing.Line(p + glm::vec3(0.15f * r, r, 0.0f), p + glm::vec3(-0.35f * r, 0.05f * r, 0.0f), color);
			OverlayDrawing.Line(p + glm::vec3(-0.35f * r, 0.05f * r, 0.0f), p + glm::vec3(0.3f * r, 0.0f, 0.0f), color);
			OverlayDrawing.Line(p + glm::vec3(0.3f * r, 0.0f, 0.0f), p + glm::vec3(-0.15f * r, -r, 0.0f), color);
		}

		// A selected decal's quad, a selected light's range, a selected emitter's particles' bounds
		// and a selected lightning entity's reach, in the selection outline's colour
		if (SelectedEntity && world.Registry == World)
		{
			const glm::vec3 outlineColor = glm::vec3(OutlineCompositePushConstants{}.VisibleOutlineColor);
			for (const RenderDecal& decal : world.Decals)
			{
				if (decal.SourceEntity != *SelectedEntity)
				{
					continue;
				}
				for (size_t corner = 0; corner < decal.WorldCorners.size(); ++corner)
				{
					OverlayDrawing.Line(decal.WorldCorners[corner], decal.WorldCorners[(corner + 1) % decal.WorldCorners.size()], outlineColor);
				}
			}
			for (const RenderPointLight& light : world.PointLights)
			{
				if (light.SourceEntity == *SelectedEntity && light.bIcon)
				{
					OverlayDrawing.Sphere(light.WorldPosition, light.Range, outlineColor);
					OverlayDrawing.Sphere(light.WorldPosition, PointLightIconRadius, outlineColor);
				}
			}
			for (const RenderParticleEmitter& emitter : world.Particles.Emitters)
			{
				if (emitter.SourceEntity == *SelectedEntity)
				{
					OverlayDrawing.Box(emitter.Bounds.Min, emitter.Bounds.Max, outlineColor);
				}
			}
			// A selected cloud modifier's shapes: a circle of each one's radius (and a ring's width round
			// it) at its bottom, or its entity's height for one over the whole band
			for (const RenderCloudModifier& modifier : world.CloudModifiers)
			{
				if (modifier.SourceEntity != *SelectedEntity)
				{
					continue;
				}
				const float height = modifier.Bottom > -1.0e5f ? modifier.Bottom : modifier.Height;
				const glm::vec3 middle(modifier.Center.x, height, modifier.Center.y);
				constexpr int Segments = 64;
				for (const float radius : { modifier.Radius, modifier.Shape == ECloudModifierShape::ThickenRing ? modifier.Radius + modifier.Width * 0.5f : -1.0f })
				{
					for (int segment = 0; radius > 0.0f && segment < Segments; ++segment)
					{
						const float a0 = glm::two_pi<float>() * static_cast<float>(segment) / Segments;
						const float a1 = glm::two_pi<float>() * static_cast<float>(segment + 1) / Segments;
						OverlayDrawing.Line(middle + glm::vec3(std::cos(a0), 0.0f, std::sin(a0)) * radius, middle + glm::vec3(std::cos(a1), 0.0f, std::sin(a1)) * radius, outlineColor);
					}
				}
			}
			// A selected lightning entity's reach: where its strikes land (a column's radius) as a
			// circle on its ground, and a line up to where they start
			for (const RenderLightningBolt& bolt : world.Lightning)
			{
				if (bolt.SourceEntity != *SelectedEntity)
				{
					continue;
				}
				OverlayDrawing.Line(bolt.WorldPosition, bolt.WorldPosition + glm::vec3(0.0f, bolt.Height, 0.0f), outlineColor);
				if (bolt.StrikeRadius > 0.0f)
				{
					constexpr int Segments = 48;
					for (int segment = 0; segment < Segments; ++segment)
					{
						const float a0 = glm::two_pi<float>() * static_cast<float>(segment) / Segments;
						const float a1 = glm::two_pi<float>() * static_cast<float>(segment + 1) / Segments;
						OverlayDrawing.Line(bolt.WorldPosition + glm::vec3(std::cos(a0), 0.0f, std::sin(a0)) * bolt.StrikeRadius,
							bolt.WorldPosition + glm::vec3(std::cos(a1), 0.0f, std::sin(a1)) * bolt.StrikeRadius, outlineColor);
					}
				}
			}
		}

		// The world's axes
		OverlayDrawing.Line(glm::vec3(0, 0, 0), glm::vec3(1, 0, 0), glm::vec3(1, 0, 0));
		OverlayDrawing.Line(glm::vec3(0, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, 1, 0));
		OverlayDrawing.Line(glm::vec3(0, 0, 0), glm::vec3(0, 0, 1), glm::vec3(0, 0, 1));
		world.OverlayLines = DebugLines.Add(OverlayDrawing);
	}

	bool VulkanEditorOverlays::ShowsSelection(const SceneViewInstance& view) const
	{
		return SelectedEntity.has_value() && World && (!view.OwnWorld || view.OwnWorld == World);
	}

	void VulkanEditorOverlays::CreateGridPipeline(const SceneViewInstance& firstView)
	{
		const std::vector<uint32_t> vertCode = Programs.GetSpirv("Grid.vert");
		const std::vector<uint32_t> fragCode = Programs.GetSpirv("Grid.frag");

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

		vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};

		vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
		inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

		vk::PipelineViewportStateCreateInfo viewportState{};
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;

		vk::PipelineRasterizationStateCreateInfo rasterizer{};
		rasterizer.polygonMode = vk::PolygonMode::eFill;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = vk::CullModeFlagBits::eNone;
		rasterizer.frontFace = vk::FrontFace::eCounterClockwise;

		vk::PipelineMultisampleStateCreateInfo multisampling{};
		multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

		vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
		colorBlendAttachment.blendEnable = VK_TRUE;

		// Add source color on top of destination, modulated by source alpha
		colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
		colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOne;
		colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;

		// Alpha channel itself does not matter much here
		colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
		colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
		colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;

		colorBlendAttachment.colorWriteMask =
			vk::ColorComponentFlagBits::eR |
			vk::ColorComponentFlagBits::eG |
			vk::ColorComponentFlagBits::eB |
			vk::ColorComponentFlagBits::eA;

		vk::PipelineColorBlendStateCreateInfo colorBlending{};
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &colorBlendAttachment;

		std::array<vk::DynamicState, 2> dynamicStates = {
			vk::DynamicState::eViewport,
			vk::DynamicState::eScissor
		};

		vk::PipelineDynamicStateCreateInfo dynamicState{};
		dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
		dynamicState.pDynamicStates = dynamicStates.data();

		// @todo: Is the depthStencil something we want for the grid?
		// Disabled it for now because it was hiding the rendered Quad
		//vk::PipelineDepthStencilStateCreateInfo depthStencil{};
		//depthStencil.depthTestEnable = VK_TRUE;
		//depthStencil.depthWriteEnable = VK_TRUE;
		//depthStencil.depthCompareOp = vk::CompareOp::eLess;
		//depthStencil.depthBoundsTestEnable = VK_FALSE;
		//depthStencil.stencilTestEnable = VK_FALSE;

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

		GridPipeline = std::move(vk::raii::Pipeline(Context.GetDevice(), Context.GetPipelineCache(), pipelineInfo));
	}

	void VulkanEditorOverlays::CreatePickingPipeline(const SceneViewInstance& firstView)
	{
		PickingPipeline = MakePickingPipeline(firstView, Programs.GetSpirv("Picking.vert"), "main");
	}

	vk::raii::Pipeline VulkanEditorOverlays::MakePickingPipeline(const SceneViewInstance& firstView, const std::vector<uint32_t>& vertexSpirv, const std::string& entryPoint) const
	{
		const std::vector<uint32_t> fragCode = Programs.GetSpirv("Picking.frag");

		vk::raii::ShaderModule vertShaderModule = CreateShaderModule(Context, vertexSpirv);
		vk::raii::ShaderModule fragShaderModule = CreateShaderModule(Context, fragCode);

		vk::PipelineShaderStageCreateInfo vertStageInfo{};
		vertStageInfo.stage = vk::ShaderStageFlagBits::eVertex;
		vertStageInfo.module = *vertShaderModule;
		vertStageInfo.pName = entryPoint.c_str();

		vk::PipelineShaderStageCreateInfo fragStageInfo{};
		fragStageInfo.stage = vk::ShaderStageFlagBits::eFragment;
		fragStageInfo.module = *fragShaderModule;
		fragStageInfo.pName = "main";

		vk::PipelineShaderStageCreateInfo shaderStages[] = { vertStageInfo, fragStageInfo };

		// What the vertex shader reads of the vertices and of each instance from the second stream:
		// Picking.vert the positions and transforms, a material's what its vertex function uses
		const std::array<vk::VertexInputBindingDescription, 2> bindingDescriptions = { GetVertexBindingDescription(), GetInstanceBindingDescription() };
		const std::vector<vk::VertexInputAttributeDescription> attributeDescriptions = GetMeshAttributesRead(vertexSpirv);

		vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
		vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
		vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();
		vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
		vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

		vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
		inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		vk::PipelineViewportStateCreateInfo viewportState{};
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;

		vk::PipelineRasterizationStateCreateInfo rasterizer{};
		rasterizer.depthClampEnable = VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode = vk::PolygonMode::eFill;
		rasterizer.lineWidth = 1.0f;
		// The faces the scene pass draws, section by section (DrawObjectMesh()): a click on what a
		// one-sided material leaves out goes to what is behind it
		rasterizer.cullMode = vk::CullModeFlagBits::eBack;
		rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
		rasterizer.depthBiasEnable = VK_FALSE;

		vk::PipelineMultisampleStateCreateInfo multisampling{};
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

		const std::array<vk::DynamicState, 4> dynamicStates = MeshDynamicStates;

		vk::PipelineDynamicStateCreateInfo dynamicState{};
		dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
		dynamicState.pDynamicStates = dynamicStates.data();

		vk::PipelineDepthStencilStateCreateInfo depthStencil{};
		depthStencil.depthTestEnable = VK_TRUE;
		depthStencil.depthWriteEnable = VK_TRUE;
		depthStencil.depthCompareOp = DepthTestNearer;
		depthStencil.depthBoundsTestEnable = VK_FALSE;
		depthStencil.stencilTestEnable = VK_FALSE;

		vk::RenderPass pickingRenderPass = *firstView.Overlays.PickingRenderPass;

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
		pipelineInfo.renderPass = pickingRenderPass;
		pipelineInfo.subpass = 0;

		return vk::raii::Pipeline(Context.GetDevice(), Context.GetPipelineCache(), pipelineInfo);
	}

	void VulkanEditorOverlays::CreatePickingResourcesForView(SceneViewInstance& view)
	{
		const vk::Extent2D extent = view.RenderTarget.GetExtent();
		const uint32_t width = std::max(1u, extent.width);
		const uint32_t height = std::max(1u, extent.height);

		// ID target: R32_UINT
		CreateImage(
			width,
			height,
			vk::Format::eR32Uint,
			vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc,
			vk::ImageAspectFlagBits::eColor,
			view.Overlays.PickingImage,
			view.Overlays.PickingImageMemory,
			view.Overlays.PickingImageView);

		// Depth target
		CreateImage(
			width,
			height,
			vk::Format::eD32Sfloat,
			vk::ImageUsageFlagBits::eDepthStencilAttachment,
			vk::ImageAspectFlagBits::eDepth,
			view.Overlays.PickingDepthImage,
			view.Overlays.PickingDepthImageMemory,
			view.Overlays.PickingDepthImageView);

		// Readback buffer for one uint32
		CreateBuffer(
			Context,
			sizeof(uint32_t),
			vk::BufferUsageFlagBits::eTransferDst,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
			view.Overlays.PickingReadbackBuffer,
			view.Overlays.PickingReadbackBufferMemory);

		// Render pass
		{
			vk::AttachmentDescription colorAttachment{};
			colorAttachment.format = vk::Format::eR32Uint;
			colorAttachment.samples = vk::SampleCountFlagBits::e1;
			colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
			colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
			colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
			colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
			colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
			colorAttachment.finalLayout = vk::ImageLayout::eTransferSrcOptimal;

			vk::AttachmentDescription depthAttachment{};
			depthAttachment.format = vk::Format::eD32Sfloat;
			depthAttachment.samples = vk::SampleCountFlagBits::e1;
			depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
			depthAttachment.storeOp = vk::AttachmentStoreOp::eDontCare;
			depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
			depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
			depthAttachment.initialLayout = vk::ImageLayout::eUndefined;
			depthAttachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

			vk::AttachmentReference colorRef{};
			colorRef.attachment = 0;
			colorRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

			vk::AttachmentReference depthRef{};
			depthRef.attachment = 1;
			depthRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

			vk::SubpassDescription subpass{};
			subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
			subpass.colorAttachmentCount = 1;
			subpass.pColorAttachments = &colorRef;
			subpass.pDepthStencilAttachment = &depthRef;

			std::array<vk::AttachmentDescription, 2> attachments = {
				colorAttachment,
				depthAttachment
			};

			vk::RenderPassCreateInfo renderPassInfo{};
			renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
			renderPassInfo.pAttachments = attachments.data();
			renderPassInfo.subpassCount = 1;
			renderPassInfo.pSubpasses = &subpass;

			view.Overlays.PickingRenderPass = vk::raii::RenderPass(Context.GetDevice(), renderPassInfo);
		}

		// Framebuffer
		{
			std::array<vk::ImageView, 2> attachments = {
				*view.Overlays.PickingImageView,
				*view.Overlays.PickingDepthImageView
			};

			vk::FramebufferCreateInfo framebufferInfo{};
			framebufferInfo.renderPass = *view.Overlays.PickingRenderPass;
			framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
			framebufferInfo.pAttachments = attachments.data();
			framebufferInfo.width = width;
			framebufferInfo.height = height;
			framebufferInfo.layers = 1;

			view.Overlays.PickingFramebuffer = vk::raii::Framebuffer(Context.GetDevice(), framebufferInfo);
		}
	}

	void VulkanEditorOverlays::DestroyPickingResourcesForView(SceneViewInstance& view)
	{
		view.Overlays.ResetPicking();

		view.Overlays.PickingFramebuffer = nullptr;
		view.Overlays.PickingRenderPass = nullptr;

		view.Overlays.PickingImageView = nullptr;
		view.Overlays.PickingImage = nullptr;
		view.Overlays.PickingImageMemory = nullptr;

		view.Overlays.PickingDepthImageView = nullptr;
		view.Overlays.PickingDepthImage = nullptr;
		view.Overlays.PickingDepthImageMemory = nullptr;

		view.Overlays.PickingReadbackBuffer = nullptr;
		view.Overlays.PickingReadbackBufferMemory = nullptr;
	}

	void VulkanEditorOverlays::RecreatePickingResourcesForView(SceneViewInstance& view)
	{
		// Debug
		{
			const vk::Extent2D extent = view.RenderTarget.GetExtent();
			LOG_INFO("Creating picking resources for view {} at {}x{}", view.Id, extent.width, extent.height);
		}

		DestroyPickingResourcesForView(view);
		CreatePickingResourcesForView(view);
	}

	std::optional<uint64_t> VulkanEditorOverlays::GetSelectionMaskFingerprint(const SceneViewInstance& view, const RenderWorld& world) const
	{
		if (!ShowsSelection(view))
		{
			return std::nullopt;
		}

		// FNV-1a over everything the mask passes draw from. The visible mask has every object's
		// depth in it, so every object counts, not only the selected one.
		uint64_t hash = 14695981039346656037ull;
		const auto add = [&hash](const void* data, size_t size)
		{
			const auto* bytes = static_cast<const unsigned char*>(data);
			for (size_t index = 0; index < size; ++index)
			{
				hash = (hash ^ bytes[index]) * 1099511628211ull;
			}
		};
		const auto addValue = [&add](const auto& value)
		{
			add(&value, sizeof(value));
		};

		addValue(SelectedEntity.value());
		addValue(world.Revision);
		addValue(view.SceneGlobals.ViewProjection);
		const vk::Extent2D extent = view.RenderTarget.GetExtent();
		addValue(extent.width);
		addValue(extent.height);
		bool bMoving = false;
		for (const RenderObject& obj : world.Objects)
		{
			if (!obj.MeshAsset || !obj.MaterialAsset)
			{
				continue;
			}
			addValue(obj.SourceEntity);
			addValue(obj.WorldTransform);
			const VkBuffer vertexBuffer = obj.MeshAsset->GetVertexBuffer();
			const VkBuffer indexBuffer = obj.MeshAsset->GetIndexBuffer();
			addValue(vertexBuffer);
			addValue(indexBuffer);
			addValue(obj.MeshAsset->GetIndexCount());
			// A mesh made at run time and uploaded anew may get the old buffers' handles back; so may
			// instances
			addValue(obj.MeshAsset->GetUploadId());
			addValue(obj.InstanceUploadId);
			addValue(obj.CellCount);
			addValue(obj.Custom);
			for (uint32_t section = obj.FirstSection; section < obj.FirstSection + obj.SectionCount; ++section)
			{
				const Material* material = world.Sections[section].MaterialAsset;
				bMoving = bMoving || (material && material->SelectionMaskVisiblePipeline);
			}
		}
		// A material's vertex function may move its meshes by the time: drawn every frame then
		if (bMoving)
		{
			addValue(world.Time);
		}
		return hash;
	}

	void VulkanEditorOverlays::CreateSelectionMaskResourcesForView(SceneViewInstance& view)
	{
		// New images hold nothing yet: the next frame draws them.
		view.Overlays.SelectionMaskFingerprint.reset();
		const vk::Extent2D extent = view.RenderTarget.GetExtent();
		const uint32_t width = std::max(1u, extent.width);
		const uint32_t height = std::max(1u, extent.height);

		// Visible mask
		CreateImage(
			width,
			height,
			vk::Format::eR8Unorm,
			vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
			vk::ImageAspectFlagBits::eColor,
			view.Overlays.VisibleSelectionMaskImage,
			view.Overlays.VisibleSelectionMaskImageMemory,
			view.Overlays.VisibleSelectionMaskImageView);

		// Full silhouette mask
		CreateImage(
			width,
			height,
			vk::Format::eR8Unorm,
			vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
			vk::ImageAspectFlagBits::eColor,
			view.Overlays.FullSelectionMaskImage,
			view.Overlays.FullSelectionMaskImageMemory,
			view.Overlays.FullSelectionMaskImageView);

		// Shared depth image for both passes
		CreateImage(
			width,
			height,
			vk::Format::eD32Sfloat,
			vk::ImageUsageFlagBits::eDepthStencilAttachment,
			vk::ImageAspectFlagBits::eDepth,
			view.Overlays.SelectionMaskDepthImage,
			view.Overlays.SelectionMaskDepthImageMemory,
			view.Overlays.SelectionMaskDepthImageView);

		// Shared render pass
		{
			vk::AttachmentDescription colorAttachment{};
			colorAttachment.format = vk::Format::eR8Unorm;
			colorAttachment.samples = vk::SampleCountFlagBits::e1;
			colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
			colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
			colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
			colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
			colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
			colorAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

			vk::AttachmentDescription depthAttachment{};
			depthAttachment.format = vk::Format::eD32Sfloat;
			depthAttachment.samples = vk::SampleCountFlagBits::e1;
			depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
			depthAttachment.storeOp = vk::AttachmentStoreOp::eDontCare;
			depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
			depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
			depthAttachment.initialLayout = vk::ImageLayout::eUndefined;
			depthAttachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

			vk::AttachmentReference colorRef{};
			colorRef.attachment = 0;
			colorRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

			vk::AttachmentReference depthRef{};
			depthRef.attachment = 1;
			depthRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

			vk::SubpassDescription subpass{};
			subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
			subpass.colorAttachmentCount = 1;
			subpass.pColorAttachments = &colorRef;
			subpass.pDepthStencilAttachment = &depthRef;

			std::array<vk::AttachmentDescription, 2> attachments = {
				colorAttachment,
				depthAttachment
			};

			vk::RenderPassCreateInfo renderPassInfo{};
			renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
			renderPassInfo.pAttachments = attachments.data();
			renderPassInfo.subpassCount = 1;
			renderPassInfo.pSubpasses = &subpass;

			view.Overlays.SelectionMaskRenderPass = vk::raii::RenderPass(Context.GetDevice(), renderPassInfo);
		}

		// Visible framebuffer
		{
			std::array<vk::ImageView, 2> attachments = {
				*view.Overlays.VisibleSelectionMaskImageView,
				*view.Overlays.SelectionMaskDepthImageView
			};

			vk::FramebufferCreateInfo framebufferInfo{};
			framebufferInfo.renderPass = *view.Overlays.SelectionMaskRenderPass;
			framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
			framebufferInfo.pAttachments = attachments.data();
			framebufferInfo.width = width;
			framebufferInfo.height = height;
			framebufferInfo.layers = 1;

			view.Overlays.VisibleSelectionMaskFramebuffer = vk::raii::Framebuffer(Context.GetDevice(), framebufferInfo);
		}

		// Full framebuffer
		{
			std::array<vk::ImageView, 2> attachments = {
				*view.Overlays.FullSelectionMaskImageView,
				*view.Overlays.SelectionMaskDepthImageView
			};

			vk::FramebufferCreateInfo framebufferInfo{};
			framebufferInfo.renderPass = *view.Overlays.SelectionMaskRenderPass;
			framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
			framebufferInfo.pAttachments = attachments.data();
			framebufferInfo.width = width;
			framebufferInfo.height = height;
			framebufferInfo.layers = 1;

			view.Overlays.FullSelectionMaskFramebuffer = vk::raii::Framebuffer(Context.GetDevice(), framebufferInfo);
		}
	}

	void VulkanEditorOverlays::DestroySelectionMaskResourcesForView(SceneViewInstance& view)
	{
		view.Overlays.OutlineDescriptorSets.reset();
		view.Overlays.OutlineDescriptorPool = nullptr;

		view.Overlays.VisibleSelectionMaskFramebuffer = nullptr;
		view.Overlays.FullSelectionMaskFramebuffer = nullptr;
		view.Overlays.SelectionMaskRenderPass = nullptr;

		view.Overlays.VisibleSelectionMaskImageView = nullptr;
		view.Overlays.VisibleSelectionMaskImage = nullptr;
		view.Overlays.VisibleSelectionMaskImageMemory = nullptr;

		view.Overlays.FullSelectionMaskImageView = nullptr;
		view.Overlays.FullSelectionMaskImage = nullptr;
		view.Overlays.FullSelectionMaskImageMemory = nullptr;

		view.Overlays.SelectionMaskDepthImageView = nullptr;
		view.Overlays.SelectionMaskDepthImage = nullptr;
		view.Overlays.SelectionMaskDepthImageMemory = nullptr;
	}

	void VulkanEditorOverlays::RecreateSelectionMaskResourcesForView(SceneViewInstance& view)
	{
		DestroySelectionMaskResourcesForView(view);
		CreateSelectionMaskResourcesForView(view);
		CreateOutlineDescriptorSetForView(view);
	}

	void VulkanEditorOverlays::CreateOutlineDescriptorSetLayout()
	{
		vk::DescriptorSetLayoutBinding visibleMaskBinding{};
		visibleMaskBinding.binding = 0;
		visibleMaskBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
		visibleMaskBinding.descriptorCount = 1;
		visibleMaskBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

		vk::DescriptorSetLayoutBinding fullMaskBinding{};
		fullMaskBinding.binding = 1;
		fullMaskBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
		fullMaskBinding.descriptorCount = 1;
		fullMaskBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

		std::array<vk::DescriptorSetLayoutBinding, 2> bindings = {
			visibleMaskBinding,
			fullMaskBinding
		};

		vk::DescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		layoutInfo.pBindings = bindings.data();

		OutlineDescriptorSetLayout = vk::raii::DescriptorSetLayout(Context.GetDevice(), layoutInfo);

		vk::SamplerCreateInfo samplerInfo{};
		samplerInfo.magFilter = vk::Filter::eLinear;
		samplerInfo.minFilter = vk::Filter::eLinear;
		samplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
		samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = 0.0f;

		OutlineCompositeSampler = vk::raii::Sampler(Context.GetDevice(), samplerInfo);
	}

	void VulkanEditorOverlays::CreateOutlineDescriptorSetForView(SceneViewInstance& view)
	{
		ASSERT(OutlineDescriptorSetLayout != nullptr && "OutlineDescriptorSetLayout must exist before CreateOutlineDescriptorSetForView.");
		ASSERT(OutlineCompositeSampler != nullptr && "OutlineCompositeSampler must exist before CreateOutlineDescriptorSetForView.");
		ASSERT(view.Overlays.VisibleSelectionMaskImageView != nullptr && "VisibleSelectionMaskImageView must exist.");
		ASSERT(view.Overlays.FullSelectionMaskImageView != nullptr && "FullSelectionMaskImageView must exist.");

		std::array<vk::DescriptorPoolSize, 1> poolSizes{};
		poolSizes[0].type = vk::DescriptorType::eCombinedImageSampler;
		poolSizes[0].descriptorCount = 2;

		vk::DescriptorPoolCreateInfo poolInfo{};
		poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
		poolInfo.maxSets = 1;
		poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		poolInfo.pPoolSizes = poolSizes.data();

		view.Overlays.OutlineDescriptorPool = vk::raii::DescriptorPool(Context.GetDevice(), poolInfo);

		vk::DescriptorSetLayout layout = *OutlineDescriptorSetLayout;

		vk::DescriptorSetAllocateInfo allocInfo{};
		allocInfo.descriptorPool = *view.Overlays.OutlineDescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &layout;

		view.Overlays.OutlineDescriptorSets.emplace(Context.GetDevice(), allocInfo);

		vk::DescriptorImageInfo visibleInfo{};
		visibleInfo.sampler = *OutlineCompositeSampler;
		visibleInfo.imageView = *view.Overlays.VisibleSelectionMaskImageView;
		visibleInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

		vk::DescriptorImageInfo fullInfo{};
		fullInfo.sampler = *OutlineCompositeSampler;
		fullInfo.imageView = *view.Overlays.FullSelectionMaskImageView;
		fullInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

		std::array<vk::WriteDescriptorSet, 2> writes{};

		writes[0].dstSet = *view.Overlays.OutlineDescriptorSets->front();
		writes[0].dstBinding = 0;
		writes[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
		writes[0].descriptorCount = 1;
		writes[0].pImageInfo = &visibleInfo;

		writes[1].dstSet = *view.Overlays.OutlineDescriptorSets->front();
		writes[1].dstBinding = 1;
		writes[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
		writes[1].descriptorCount = 1;
		writes[1].pImageInfo = &fullInfo;

		Context.GetDevice().updateDescriptorSets(writes, {});
	}

	void VulkanEditorOverlays::CreateSelectionMaskPipelines(const SceneViewInstance& firstView)
	{
		const std::vector<uint32_t> vertCode = Programs.GetSpirv("SelectionMask.vert");
		SelectionMaskPipelineVisible = MakeSelectionMaskPipeline(firstView, vertCode, "main", true);
		SelectionMaskPipelineFull = MakeSelectionMaskPipeline(firstView, vertCode, "main", false);
	}

	vk::raii::Pipeline VulkanEditorOverlays::MakeSelectionMaskPipeline(const SceneViewInstance& firstView, const std::vector<uint32_t>& vertexSpirv, const std::string& entryPoint, bool bVisibleOnly) const
	{
		const std::vector<uint32_t> fragCode = Programs.GetSpirv("SelectionMask.frag");

		vk::raii::ShaderModule vertShaderModule = CreateShaderModule(Context, vertexSpirv);
		vk::raii::ShaderModule fragShaderModule = CreateShaderModule(Context, fragCode);

		vk::PipelineShaderStageCreateInfo vertStageInfo{};
		vertStageInfo.stage = vk::ShaderStageFlagBits::eVertex;
		vertStageInfo.module = *vertShaderModule;
		vertStageInfo.pName = entryPoint.c_str();

		vk::PipelineShaderStageCreateInfo fragStageInfo{};
		fragStageInfo.stage = vk::ShaderStageFlagBits::eFragment;
		fragStageInfo.module = *fragShaderModule;
		fragStageInfo.pName = "main";

		vk::PipelineShaderStageCreateInfo shaderStages[] = { vertStageInfo, fragStageInfo };

		// What the vertex shader reads of the vertices and of each instance from the second stream:
		// SelectionMask.vert the positions and transforms, a material's what its vertex function uses
		const std::array<vk::VertexInputBindingDescription, 2> bindingDescriptions = { GetVertexBindingDescription(), GetInstanceBindingDescription() };
		const std::vector<vk::VertexInputAttributeDescription> attributeDescriptions = GetMeshAttributesRead(vertexSpirv);

		vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
		vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
		vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();
		vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
		vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

		vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
		inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

		vk::PipelineViewportStateCreateInfo viewportState{};
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;

		vk::PipelineRasterizationStateCreateInfo rasterizer{};
		rasterizer.polygonMode = vk::PolygonMode::eFill;
		rasterizer.lineWidth = 1.0f;
		// The faces the scene pass draws, section by section (DrawObjectMesh())
		rasterizer.cullMode = vk::CullModeFlagBits::eBack;
		rasterizer.frontFace = vk::FrontFace::eCounterClockwise;

		vk::PipelineMultisampleStateCreateInfo multisampling{};
		multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

		vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
		colorBlendAttachment.blendEnable = VK_FALSE;
		colorBlendAttachment.colorWriteMask =
			vk::ColorComponentFlagBits::eR |
			vk::ColorComponentFlagBits::eG |
			vk::ColorComponentFlagBits::eB |
			vk::ColorComponentFlagBits::eA;

		vk::PipelineColorBlendStateCreateInfo colorBlending{};
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &colorBlendAttachment;

		const std::array<vk::DynamicState, 4> dynamicStates = MeshDynamicStates;

		vk::PipelineDynamicStateCreateInfo dynamicState{};
		dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
		dynamicState.pDynamicStates = dynamicStates.data();

		vk::RenderPass renderPass = *firstView.Overlays.SelectionMaskRenderPass;

		// Visible-only: depth tested and written; the full silhouette: neither
		vk::PipelineDepthStencilStateCreateInfo depthStencil{};
		depthStencil.depthTestEnable = bVisibleOnly ? VK_TRUE : VK_FALSE;
		depthStencil.depthWriteEnable = bVisibleOnly ? VK_TRUE : VK_FALSE;
		depthStencil.depthCompareOp = bVisibleOnly ? DepthTestNearer : vk::CompareOp::eAlways;

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
		pipelineInfo.renderPass = renderPass;
		pipelineInfo.subpass = 0;

		return vk::raii::Pipeline(Context.GetDevice(), Context.GetPipelineCache(), pipelineInfo);
	}

	void VulkanEditorOverlays::DrawVisibleSelectionMaskPass(SceneViewInstance& view, const RenderWorld& world, vk::raii::CommandBuffer& cmd)
	{
		std::array<vk::ClearValue, 2> clearValues{};
		clearValues[0].color = vk::ClearColorValue(std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 0.0f });
		clearValues[1].depthStencil = vk::ClearDepthStencilValue(FarPlaneDepth, 0);

		const vk::Extent2D extent = view.RenderTarget.GetExtent();

		vk::RenderPassBeginInfo renderPassInfo{};
		renderPassInfo.renderPass = *view.Overlays.SelectionMaskRenderPass;
		renderPassInfo.framebuffer = *view.Overlays.VisibleSelectionMaskFramebuffer;
		renderPassInfo.renderArea.offset = vk::Offset2D{ 0, 0 };
		renderPassInfo.renderArea.extent = extent;
		renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		renderPassInfo.pClearValues = clearValues.data();

		cmd.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

		vk::Viewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(extent.width);
		viewport.height = static_cast<float>(extent.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		vk::Rect2D scissor{};
		scissor.offset = vk::Offset2D{ 0, 0 };
		scissor.extent = extent;

		cmd.setViewport(0, viewport);
		cmd.setScissor(0, scissor);

		vk::Pipeline bound = *SelectionMaskPipelineVisible;
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, bound);

		BindSceneSets(view, cmd);

		for (const RenderObject& obj : world.Objects)
		{
			if (!obj.MeshAsset || !obj.MaterialAsset)
			{
				continue;
			}

			const bool bIsSelected =
				SelectedEntity.has_value() && SelectedEntity.value() == obj.SourceEntity;

			ObjectConstants object{};
			object.Model = obj.WorldTransform;
			object.CustomData.x = bIsSelected ? 1.0f : 0.0f;
			DrawObjectMesh(world, obj, object, EMeshPass::SelectionMaskVisible, bound, cmd);
		}

		cmd.endRenderPass();
	}

	void VulkanEditorOverlays::DrawFullSelectionMaskPass(SceneViewInstance& view, const RenderWorld& world, vk::raii::CommandBuffer& cmd)
	{
		std::array<vk::ClearValue, 2> clearValues{};
		clearValues[0].color = vk::ClearColorValue(std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 0.0f });
		clearValues[1].depthStencil = vk::ClearDepthStencilValue(FarPlaneDepth, 0);

		const vk::Extent2D extent = view.RenderTarget.GetExtent();

		vk::RenderPassBeginInfo renderPassInfo{};
		renderPassInfo.renderPass = *view.Overlays.SelectionMaskRenderPass;
		renderPassInfo.framebuffer = *view.Overlays.FullSelectionMaskFramebuffer;
		renderPassInfo.renderArea.offset = vk::Offset2D{ 0, 0 };
		renderPassInfo.renderArea.extent = extent;
		renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		renderPassInfo.pClearValues = clearValues.data();

		cmd.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

		vk::Viewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(extent.width);
		viewport.height = static_cast<float>(extent.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		vk::Rect2D scissor{};
		scissor.offset = vk::Offset2D{ 0, 0 };
		scissor.extent = extent;

		cmd.setViewport(0, viewport);
		cmd.setScissor(0, scissor);

		vk::Pipeline bound = *SelectionMaskPipelineFull;
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, bound);

		BindSceneSets(view, cmd);

		if (!SelectedEntity.has_value())
		{
			cmd.endRenderPass();
			return;
		}

		for (const RenderObject& obj : world.Objects)
		{
			if (!obj.MeshAsset || !obj.MaterialAsset)
			{
				continue;
			}

			if (obj.SourceEntity != SelectedEntity.value())
			{
				continue;
			}

			ObjectConstants object{};
			object.Model = obj.WorldTransform;
			object.CustomData.x = 1.0f;
			DrawObjectMesh(world, obj, object, EMeshPass::SelectionMaskFull, bound, cmd);
		}

		cmd.endRenderPass();
	}

	void VulkanEditorOverlays::CreateOutlineCompositePipeline(const SceneViewInstance& firstView)
	{
		const std::vector<uint32_t> vertCode = Programs.GetSpirv("OutlineComposite.vert");
		const std::vector<uint32_t> fragCode = Programs.GetSpirv("OutlineComposite.frag");

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

		vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};

		vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
		inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

		vk::PipelineViewportStateCreateInfo viewportState{};
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;

		vk::PipelineRasterizationStateCreateInfo rasterizer{};
		rasterizer.polygonMode = vk::PolygonMode::eFill;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = vk::CullModeFlagBits::eNone;
		rasterizer.frontFace = vk::FrontFace::eCounterClockwise;

		vk::PipelineMultisampleStateCreateInfo multisampling{};
		multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

		vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
		colorBlendAttachment.blendEnable = VK_TRUE;
		colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
		colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
		colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
		colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
		colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
		colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
		colorBlendAttachment.colorWriteMask =
			vk::ColorComponentFlagBits::eR |
			vk::ColorComponentFlagBits::eG |
			vk::ColorComponentFlagBits::eB |
			vk::ColorComponentFlagBits::eA;

		vk::PipelineColorBlendStateCreateInfo colorBlending{};
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
		depthStencil.depthTestEnable = VK_FALSE;
		depthStencil.depthWriteEnable = VK_FALSE;

		vk::PushConstantRange pushConstantRange{};
		pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eFragment;
		pushConstantRange.offset = 0;
		pushConstantRange.size = sizeof(OutlineCompositePushConstants);

		vk::DescriptorSetLayout setLayouts[] = { *OutlineDescriptorSetLayout };

		vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
		pipelineLayoutInfo.setLayoutCount = 1;
		pipelineLayoutInfo.pSetLayouts = setLayouts;
		pipelineLayoutInfo.pushConstantRangeCount = 1;
		pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

		OutlineCompositePipelineLayout = vk::raii::PipelineLayout(Context.GetDevice(), pipelineLayoutInfo);

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
		pipelineInfo.layout = *OutlineCompositePipelineLayout;
		pipelineInfo.renderPass = *firstView.RenderTarget.GetRenderPass();
		pipelineInfo.subpass = 0;

		OutlineCompositePipeline = std::move(vk::raii::Pipeline(Context.GetDevice(), Context.GetPipelineCache(), pipelineInfo));
	}

	void VulkanEditorOverlays::DrawSelectionOutline(SceneViewInstance& view, vk::raii::CommandBuffer& cmd)
	{
		if (!ShowsSelection(view))
		{
			return;
		}

		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *OutlineCompositePipeline);

		cmd.bindDescriptorSets(
			vk::PipelineBindPoint::eGraphics,
			*OutlineCompositePipelineLayout,
			0,
			{ *view.Overlays.OutlineDescriptorSets->front() },
			{});

		const vk::Extent2D extent = view.RenderTarget.GetExtent();

		OutlineCompositePushConstants push{};
		push.Params0 = glm::vec4(
			1.0f / static_cast<float>(std::max(1u, extent.width)),
			1.0f / static_cast<float>(std::max(1u, extent.height)),
			2.0f,
			static_cast<float>(static_cast<uint32_t>(SelectionOutlineMode)));

		push.VisibleOutlineColor = glm::vec4(1.0f, 0.65f, 0.0f, 1.0f);
		push.OccludedOutlineColor = glm::vec4(0.15f, 0.85f, 1.0f, 1.0f);
		push.HatchColor = glm::vec4(0.15f, 0.85f, 1.0f, 0.85f);
		push.HatchParams = glm::vec4(
			14.0f, // spacing
			2.0f,  // thickness
			8.0f,  // dash length
			6.0f   // dash gap
		);

		cmd.pushConstants<OutlineCompositePushConstants>(
			*OutlineCompositePipelineLayout,
			vk::ShaderStageFlagBits::eFragment,
			0,
			{ push });

		cmd.draw(3, 1, 0, 0);
	}

	void VulkanEditorOverlays::CreateImage(
		uint32_t width,
		uint32_t height,
		vk::Format format,
		vk::ImageUsageFlags usage,
		vk::ImageAspectFlags aspectFlags,
		vk::raii::Image& outImage,
		vk::raii::DeviceMemory& outMemory,
		vk::raii::ImageView& outImageView)
	{
		vk::ImageCreateInfo imageInfo{};
		imageInfo.imageType = vk::ImageType::e2D;
		imageInfo.extent.width = width;
		imageInfo.extent.height = height;
		imageInfo.extent.depth = 1;
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = format;
		imageInfo.tiling = vk::ImageTiling::eOptimal;
		imageInfo.initialLayout = vk::ImageLayout::eUndefined;
		imageInfo.usage = usage;
		imageInfo.samples = vk::SampleCountFlagBits::e1;
		imageInfo.sharingMode = vk::SharingMode::eExclusive;

		outImage = vk::raii::Image(Context.GetDevice(), imageInfo);

		const vk::MemoryRequirements memRequirements = outImage.getMemoryRequirements();

		vk::MemoryAllocateInfo allocInfo{};
		allocInfo.allocationSize = memRequirements.size;
		allocInfo.memoryTypeIndex = FindMemoryType(
			*Context.GetPhysicalDevice(),
			memRequirements.memoryTypeBits,
			vk::MemoryPropertyFlagBits::eDeviceLocal);

		outMemory = vk::raii::DeviceMemory(Context.GetDevice(), allocInfo);
		outImage.bindMemory(*outMemory, 0);

		vk::ImageViewCreateInfo viewInfo{};
		viewInfo.image = *outImage;
		viewInfo.viewType = vk::ImageViewType::e2D;
		viewInfo.format = format;
		viewInfo.subresourceRange.aspectMask = aspectFlags;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;

		outImageView = vk::raii::ImageView(Context.GetDevice(), viewInfo);
	}

	void VulkanEditorOverlays::RequestPick(SceneViewInstance& view, uint64_t worldRevision, uint32_t pixelX, uint32_t pixelY)
	{
		// The UI can replace the world after this frame's rendering. Wait for a view
		// of the new world before accepting another click.
		if (worldRevision == 0 || view.Overlays.RenderedWorldRevision != worldRevision)
		{
			return;
		}

		const vk::Extent2D extent = view.RenderTarget.GetExtent();
		LOG_INFO(
			"Pick request view {}: ({}, {}) in extent {}x{}",
			view.Id,
			view.Overlays.PendingPickX,
			view.Overlays.PendingPickY,
			extent.width,
			extent.height);

		if (extent.width == 0 || extent.height == 0)
		{
			return;
		}

		view.Overlays.PendingPickX = std::min(pixelX, extent.width - 1);
		view.Overlays.PendingPickY = std::min(pixelY, extent.height - 1);
		view.Overlays.bPickRequestPending = true;
		view.Overlays.PickWorldRevision = worldRevision;
	}

	IEditorRenderer::PickResult VulkanEditorOverlays::ConsumeLastPickResult(SceneViewInstance& view, uint64_t worldRevision)
	{
		IEditorRenderer::PickResult result{};

		if (!view.Overlays.bPickResultReady)
		{
			return result;
		}

		if (view.Overlays.PickWorldRevision != worldRevision)
		{
			view.Overlays.ResetPicking();
			return result;
		}

		result.bHasNewResult = true;
		result.HitEntity = view.Overlays.LastPickedEntity;

		view.Overlays.bPickResultReady = false;
		view.Overlays.LastPickedEntity.reset();

		return result;
	}

	void VulkanEditorOverlays::DrawPickingPass(SceneViewInstance& view, const RenderWorld& world, vk::raii::CommandBuffer& cmd)
	{
		std::array<vk::ClearValue, 2> clearValues{};
		clearValues[0].color = vk::ClearColorValue(std::array<uint32_t, 4>{ 0u, 0u, 0u, 0u });
		clearValues[1].depthStencil = vk::ClearDepthStencilValue(FarPlaneDepth, 0);

		const vk::Extent2D extent = view.RenderTarget.GetExtent();

		vk::RenderPassBeginInfo renderPassInfo{};
		renderPassInfo.renderPass = *view.Overlays.PickingRenderPass;
		renderPassInfo.framebuffer = *view.Overlays.PickingFramebuffer;
		renderPassInfo.renderArea.offset = vk::Offset2D{ 0, 0 };
		renderPassInfo.renderArea.extent = extent;
		renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		renderPassInfo.pClearValues = clearValues.data();

		cmd.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

		vk::Viewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(extent.width);
		viewport.height = static_cast<float>(extent.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		vk::Rect2D scissor{};
		scissor.offset = vk::Offset2D{ 0, 0 };
		scissor.extent = extent;

		cmd.setViewport(0, viewport);
		cmd.setScissor(0, scissor);

		vk::Pipeline bound = *PickingPipeline;
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, bound);

		BindSceneSets(view, cmd);

		for (const RenderObject& obj : world.Objects)
		{
			if (!obj.MeshAsset || !obj.MaterialAsset)
			{
				continue;
			}

			ObjectConstants object{};
			object.Model = obj.WorldTransform;
			object.EntityId = obj.PickingId;
			DrawObjectMesh(world, obj, object, EMeshPass::Picking, bound, cmd);
		}

		// Then the decals, each with its material's picking pipeline, which leaves out where the
		// material doesn't cover the quad, so a click there picks what is beneath
		if (!world.Decals.empty())
		{
			cmd.bindVertexBuffers(0, { DecalMesh->GetVertexBuffer(), IdentityInstance }, { vk::DeviceSize{ 0 }, vk::DeviceSize{ 0 } });
			cmd.bindIndexBuffer(DecalMesh->GetIndexBuffer(), 0, vk::IndexType::eUint32);
			for (const RenderDecal& decal : world.Decals)
			{
				cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **decal.MaterialAsset->DecalPickingPipeline);

				ObjectConstants object{};
				object.Model = decal.WorldModel;
				object.MaterialOffset = decal.MaterialAsset->MaterialOffset;
				object.EntityId = decal.PickingId;
				object.CustomData = decal.CustomData;
				object.CustomData2 = decal.CustomData2;
				cmd.pushConstants<ObjectConstants>(ScenePipelineLayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, { object });
				cmd.drawIndexed(DecalMesh->GetIndexCount(), 1, 0, 0, 0);
			}
		}

		// Then the point lights', the particle emitters' and the lightning entities' icons, as balls, so a click on one selects
		// its entity
		if (!world.PointLights.empty() || !world.Particles.Emitters.empty() || !world.Lightning.empty())
		{
			cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *PickingPipeline);
			SetMeshFaces(cmd, false, false);
			cmd.bindVertexBuffers(0, { IconMesh->GetVertexBuffer(), IdentityInstance }, { vk::DeviceSize{ 0 }, vk::DeviceSize{ 0 } });
			cmd.bindIndexBuffer(IconMesh->GetIndexBuffer(), 0, vk::IndexType::eUint32);
			for (const RenderPointLight& light : world.PointLights)
			{
				if (!light.bIcon)
				{
					continue;
				}
				ObjectConstants object{};
				// The sphere mesh is a metre across
				object.Model = glm::scale(glm::translate(glm::mat4(1.0f), light.WorldPosition), glm::vec3(PointLightIconRadius * 2.0f));
				object.EntityId = light.PickingId;
				cmd.pushConstants<ObjectConstants>(ScenePipelineLayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, { object });
				cmd.drawIndexed(IconMesh->GetIndexCount(), 1, 0, 0, 0);
			}
			for (const RenderParticleEmitter& emitter : world.Particles.Emitters)
			{
				ObjectConstants object{};
				object.Model = glm::scale(glm::translate(glm::mat4(1.0f), emitter.WorldPosition), glm::vec3(ParticleEmitterIconRadius * 2.0f));
				object.EntityId = emitter.PickingId;
				cmd.pushConstants<ObjectConstants>(ScenePipelineLayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, { object });
				cmd.drawIndexed(IconMesh->GetIndexCount(), 1, 0, 0, 0);
			}
			for (const RenderLightningBolt& bolt : world.Lightning)
			{
				ObjectConstants object{};
				object.Model = glm::scale(glm::translate(glm::mat4(1.0f), bolt.WorldPosition), glm::vec3(LightningIconRadius * 2.0f));
				object.EntityId = bolt.PickingId;
				cmd.pushConstants<ObjectConstants>(ScenePipelineLayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, { object });
				cmd.drawIndexed(IconMesh->GetIndexCount(), 1, 0, 0, 0);
			}
		}

		cmd.endRenderPass();
	}

	void VulkanEditorOverlays::ResolvePickRequest(SceneViewInstance& view, const RenderWorld& world, vk::raii::CommandBuffer& cmd)
	{
		if (!view.Overlays.bPickRequestPending)
		{
			return;
		}

		if (view.Overlays.PickWorldRevision != world.Revision)
		{
			view.Overlays.ResetPicking();
			return;
		}

		vk::BufferImageCopy region{};
		region.bufferOffset = 0;
		region.bufferRowLength = 0;
		region.bufferImageHeight = 0;
		region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		region.imageSubresource.mipLevel = 0;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount = 1;
		region.imageOffset = vk::Offset3D{
			static_cast<int32_t>(view.Overlays.PendingPickX),
			static_cast<int32_t>(view.Overlays.PendingPickY),
			0
		};
		region.imageExtent = vk::Extent3D{ 1, 1, 1 };

		cmd.copyImageToBuffer(
			*view.Overlays.PickingImage,
			vk::ImageLayout::eTransferSrcOptimal,
			*view.Overlays.PickingReadbackBuffer,
			region);

		view.Overlays.bPickRequestPending = false;
		view.Overlays.bPickReadbackPending = true;
	}

	void VulkanEditorOverlays::ReadBackPick(SceneViewInstance& view, const RenderWorld* world)
	{
		if (!view.Overlays.bPickReadbackPending)
		{
			return;
		}

		// The copy may have been recorded before a scene switch in the UI. Never
		// decode its ID against the replacement world's entity table.
		if (!world || view.Overlays.PickWorldRevision != world->Revision)
		{
			view.Overlays.ResetPicking();
			return;
		}

		void* mapped = view.Overlays.PickingReadbackBufferMemory.mapMemory(0, sizeof(uint32_t));

		uint32_t encoded = 0;
		std::memcpy(&encoded, mapped, sizeof(uint32_t));

		view.Overlays.PickingReadbackBufferMemory.unmapMemory();

		view.Overlays.bPickReadbackPending = false;
		view.Overlays.bPickResultReady = true;

		if (encoded == 0)
		{
			view.Overlays.LastPickedEntity.reset();
			return;
		}

		const uint32_t index = encoded - 1u;
		if (index < world->PickingIdToEntity.size())
		{
			view.Overlays.LastPickedEntity = world->PickingIdToEntity[index];
		}
		else
		{
			view.Overlays.LastPickedEntity.reset();
		}
	}

	void VulkanEditorOverlays::DrawGrid(SceneViewInstance& view, vk::raii::CommandBuffer& cmd)
	{
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *GridPipeline);
		BindSceneSets(view, cmd);
		cmd.draw(3, 1, 0, 0);
	}
}
