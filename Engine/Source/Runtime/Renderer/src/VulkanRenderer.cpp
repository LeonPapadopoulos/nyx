#include "NyxPCH.h"
#include "Assertions.h"
#include "VulkanRenderer.h"
#include "VulkanImGuiBackend.h"
#include "VulkanContext.h"
#include "VulkanSwapchain.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <ranges>
#include <string>
#include <fstream>

#include "backends/imgui_impl_vulkan.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

namespace Nyx
{
	VulkanRenderer::VulkanRenderer()
	{
	}

	void VulkanRenderer::Initialize(const char* applicationName, GLFWwindow* window)
	{
		ASSERT(window != nullptr);
		Window = window;

		SetupVulkan(applicationName, window);
		ASSERT(Swapchain.GetMinImageCount() >= 2 && "Failed to fulfill VulkanImGuiBackend requirements.");
		ASSERT(Swapchain.GetImageCount() >= Swapchain.GetMinImageCount() && "Failed to fulfill VulkanImGuiBackend requirements.");
		SetupImGui();

		SceneViewport.Initialize(Context, 1280, 720, vk::Format::eR8G8B8A8Unorm);
		CreateSceneUniformBuffer();
		CreateSceneDescriptors();
		CreateGridPipeline();
		CreateTestMeshData();
		CreateTestMeshBuffers();
		CreateScenePipeline();
	}

	void VulkanRenderer::Shutdown()
	{
		if (*Context.GetDevice())
		{
			Context.GetDevice().waitIdle();
		}

		SceneViewport.Shutdown(Context);

		if (ImGuiBackend)
		{
			ImGuiBackend->Shutdown();
			ImGuiBackend.reset();
		}

		Swapchain.Shutdown();

		// destroy command buffers / semaphores / fences / pools
		CommandBuffers.clear();
		RenderFinishedSemaphores.clear();
		ImageAvailableSemaphore = nullptr;
		InFlightFence = nullptr;
		CommandPool = nullptr;

		TestIndexBuffer = nullptr;
		TestIndexBufferMemory = nullptr;
		TestVertexBuffer = nullptr;
		TestVertexBufferMemory = nullptr;

		Context.Shutdown();
	}

	void VulkanRenderer::BeginFrame()
	{
	}
	
	void VulkanRenderer::EndFrame()
	{
	}

	void VulkanRenderer::OnFramebufferResized()
	{
		bRecreateSwapChain = true;
	}

	void VulkanRenderer::SetupVulkan(const char* applicationName, GLFWwindow* window)
	{
		Context.Initialize(applicationName, window);
		Swapchain.Initialize(Context, window);

		CreateGraphicsPipeline();

		// @todo: check order
		CreateCommandPool();
		CreateCommandBuffers();
		CreateSyncObjects();
	}

	void VulkanRenderer::SetupImGui()
	{
		ImGuiBackend = std::make_unique<VulkanImGuiBackend>(Window, *this);
		ImGuiBackend->Initialize(
			static_cast<float>(Swapchain.GetExtent().width),
			static_cast<float>(Swapchain.GetExtent().height));
	}

	void VulkanRenderer::DrawFrame(const std::function<void()>& buildUI)
	{
		// 1) Wait until previous submitted frame is done
		vk::Result result = Context.GetDevice().waitForFences({ *InFlightFence }, true, UINT64_MAX);

		// 2) Acquire next swapchain image
		const vk::ResultValue<uint32_t> acquireResult =
			Swapchain.GetSwapchain().acquireNextImage(UINT64_MAX, *ImageAvailableSemaphore);

		if (acquireResult.result == vk::Result::eErrorOutOfDateKHR)
		{
			RecreateSwapChain();
			return;
		}

		if (acquireResult.result != vk::Result::eSuccess &&
			acquireResult.result != vk::Result::eSuboptimalKHR)
		{
			throw std::runtime_error("Failed to acquire swapchain image.");
		}

		const uint32_t imageIndex = acquireResult.value;

		// Only reset once we know we will submit work
		Context.GetDevice().resetFences({ *InFlightFence });

		// 3) Record command buffer for this image
		vk::raii::CommandBuffer& cmd = CommandBuffers[imageIndex];
		cmd.reset();

		vk::CommandBufferBeginInfo beginInfo{};
		cmd.begin(beginInfo);

		// Render Scene
		{
			if (bSceneViewportResizePending)
			{
				SceneViewport.Recreate(Context, PendingSceneViewportWidth, PendingSceneViewportHeight, vk::Format::eR8G8B8A8Unorm);
				GridPipeline = nullptr;
				GridPipelineLayout = nullptr;
				CreateGridPipeline();
				ScenePipeline = nullptr;
				ScenePipelineLayout = nullptr;
				CreateScenePipeline();
								
				bSceneViewportResizePending = false;
				bSceneViewportRecreatedThisFrame = true;
			}
			else
			{
				bSceneViewportRecreatedThisFrame = false;
			}

			vk::ClearColorValue clear = vk::ClearColorValue(std::array<float, 4>{ 0.2f, 0.05f, 0.35f, 1.0f });

			SceneViewport.BeginRenderPass(cmd, clear);
			{
				// @todo: Introduce a more proper 'Tick'
				const float deltaTime = ComputeDeltaTime();

				TickCameraFromInput(deltaTime);
				UpdateSceneUniforms();

				// Scene Grid
				{
					cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *GridPipeline);
					cmd.bindDescriptorSets(
						vk::PipelineBindPoint::eGraphics,
						*GridPipelineLayout,
						0,
						{ *SceneDescriptorSets.front() },
						{}
					);
					cmd.draw(3, 1, 0, 0);
				}

				cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *ScenePipeline);
				cmd.bindDescriptorSets(
					vk::PipelineBindPoint::eGraphics,
					*ScenePipelineLayout,
					0,
					{ *SceneDescriptorSets.front() },
					{}
				);
				
				// Draw Mesh
				vk::DeviceSize offsets[] = { 0 };
				cmd.bindVertexBuffers(0, { *TestVertexBuffer }, offsets);
				cmd.bindIndexBuffer(*TestIndexBuffer, 0, vk::IndexType::eUint32);
				cmd.drawIndexed(static_cast<uint32_t>(TestIndices.size()), 1, 0, 0, 0);
			}
			SceneViewport.EndRenderPass(cmd);
		}

		vk::ClearValue clearValue{};
		clearValue.color = vk::ClearColorValue(std::array<float, 4>{ 0.1f, 0.1f, 0.1f, 1.0f });

		vk::RenderPassBeginInfo renderPassInfo{};
		renderPassInfo.renderPass = *Swapchain.GetRenderPass();
		renderPassInfo.framebuffer = *Swapchain.GetFramebuffers()[imageIndex];
		renderPassInfo.renderArea.offset = vk::Offset2D{ 0, 0 };
		renderPassInfo.renderArea.extent = Swapchain.GetExtent();
		renderPassInfo.clearValueCount = 1;
		renderPassInfo.pClearValues = &clearValue;

		cmd.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

		// This is where ImGuiBackend records into the active frame command buffer.
		{
			ImGuiBackend->BeginFrame();
			buildUI();
			ImGuiBackend->DrawFrame(cmd);
		}

		cmd.endRenderPass();
		cmd.end();

		// 4) Submit
		const vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;

		vk::raii::Semaphore& renderFinishedSemaphore = RenderFinishedSemaphores[imageIndex];

		vk::SubmitInfo submitInfo{};
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &*ImageAvailableSemaphore;
		submitInfo.pWaitDstStageMask = &waitStage;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &*cmd;
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &*renderFinishedSemaphore;

		Context.GetGraphicsQueue().submit(submitInfo, *InFlightFence);

		// 5) Present
		vk::PresentInfoKHR presentInfo{};
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &*renderFinishedSemaphore;
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &*Swapchain.GetSwapchain();
		presentInfo.pImageIndices = &imageIndex;

		const vk::Result presentResult = Context.GetGraphicsQueue().presentKHR(presentInfo);
		if (presentResult == vk::Result::eErrorOutOfDateKHR ||
			presentResult == vk::Result::eSuboptimalKHR ||
			bRecreateSwapChain)
		{
			RecreateSwapChain();
			return;
		}

		if (presentResult != vk::Result::eSuccess)
		{
			throw std::runtime_error("Failed to present swapchain image.");
		}
	}

	void VulkanRenderer::WaitIdle()
	{
		if (*Context.GetDevice())
		{
			Context.GetDevice().waitIdle();
		}
	}

	VulkanContext& VulkanRenderer::GetContext()
	{
		return Context;
	}

	VulkanSwapchain& VulkanRenderer::GetSwapchain()
	{
		return Swapchain;
	}

	ImTextureID VulkanRenderer::GetSceneTextureId() const
	{
		return SceneViewport.GetImGuiTextureId();
	}

	Extent2D VulkanRenderer::GetSceneViewportExtent() const
	{
		const vk::Extent2D extent = SceneViewport.GetExtent();
		return Extent2D{ extent.width, extent.height };
	}

	void VulkanRenderer::EnsureSceneViewportSize(uint32_t width, uint32_t height)
	{
		SceneViewport.EnsureSize(Context, width, height);
	}

	void VulkanRenderer::SetSceneViewportSize(uint32_t width, uint32_t height)
	{
		width = std::max(1u, width);
		height = std::max(1u, height);

		if (width == PendingSceneViewportWidth && height == PendingSceneViewportHeight)
		{
			return;
		}

		PendingSceneViewportWidth = width;
		PendingSceneViewportHeight = height;
		bSceneViewportResizePending = true;
	}

	bool VulkanRenderer::WasSceneViewportRecreatedThisFrame() const
	{
		return bSceneViewportRecreatedThisFrame;
	}

	void VulkanRenderer::TickCameraFromInput(float deltaTime)
	{
		if (!Window)
		{
			return;
		}

		const bool bWantsMouseLook =
			glfwGetMouseButton(Window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

		// Enter / leave mouse-look mode
		if (bWantsMouseLook && !bMouseLookActive)
		{
			bMouseLookActive = true;
			bFirstMouseLookSample = true;
			glfwSetInputMode(Window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		}
		else if (!bWantsMouseLook && bMouseLookActive)
		{
			bMouseLookActive = false;
			bFirstMouseLookSample = true;
			glfwSetInputMode(Window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		}

		// Mouse look
		if (bMouseLookActive)
		{
			double mouseX = 0.0;
			double mouseY = 0.0;
			glfwGetCursorPos(Window, &mouseX, &mouseY);

			if (bFirstMouseLookSample)
			{
				LastMouseX = mouseX;
				LastMouseY = mouseY;
				bFirstMouseLookSample = false;
			}
			else
			{
				const double deltaX = mouseX - LastMouseX;
				const double deltaY = mouseY - LastMouseY;

				LastMouseX = mouseX;
				LastMouseY = mouseY;

				Camera.YawRadians += static_cast<float>(deltaX) * CameraMouseSensitivity;
				Camera.PitchRadians -= static_cast<float>(deltaY) * CameraMouseSensitivity;

				const float pitchLimit = glm::radians(89.0f);
				Camera.PitchRadians = glm::clamp(Camera.PitchRadians, -pitchLimit, pitchLimit);
			}
		}

		const float moveSpeed = CameraMoveSpeed * deltaTime;

		const glm::vec3 forward = Camera.GetForwardVector();
		const glm::vec3 right = Camera.GetRightVector();
		const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);

		// Flatten movement to the ground plane for WASD
		glm::vec3 flatForward = glm::normalize(glm::vec3(forward.x, 0.0f, forward.z));
		glm::vec3 flatRight = glm::normalize(glm::vec3(right.x, 0.0f, right.z));

		// avoid issues when looking straight up/down
		if (glm::length2(flatForward) > 1e-6f)
		{
			flatForward = glm::normalize(flatForward);
		}
		else
		{
			flatForward = glm::vec3(0.0f, 0.0f, -1.0f);
		}

		if (glm::length2(flatRight) > 1e-6f)
		{
			flatRight = glm::normalize(flatRight);
		}
		else
		{
			flatRight = glm::vec3(1.0f, 0.0f, 0.0f);
		}

		if (glfwGetKey(Window, GLFW_KEY_W) == GLFW_PRESS) Camera.Position += forward * moveSpeed;
		if (glfwGetKey(Window, GLFW_KEY_S) == GLFW_PRESS) Camera.Position -= forward * moveSpeed;
		if (glfwGetKey(Window, GLFW_KEY_A) == GLFW_PRESS) Camera.Position -= flatRight * moveSpeed;
		if (glfwGetKey(Window, GLFW_KEY_D) == GLFW_PRESS) Camera.Position += flatRight * moveSpeed;

		if (glfwGetKey(Window, GLFW_KEY_Q) == GLFW_PRESS) Camera.Position -= worldUp * moveSpeed;
		if (glfwGetKey(Window, GLFW_KEY_E) == GLFW_PRESS) Camera.Position += worldUp * moveSpeed;
	}

	void VulkanRenderer::CreateGraphicsPipeline()
	{
		// @todo:
	}

	void VulkanRenderer::CreateCommandPool()
	{
		vk::CommandPoolCreateInfo poolInfo{};
		poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
		poolInfo.queueFamilyIndex = Context.GetGraphicsQueueFamily();

		CommandPool = vk::raii::CommandPool(Context.GetDevice(), poolInfo);
	}

	void VulkanRenderer::CreateCommandBuffers()
	{
		CommandBuffers.clear();

		vk::CommandBufferAllocateInfo allocInfo{};
		allocInfo.commandPool = *CommandPool;
		allocInfo.level = vk::CommandBufferLevel::ePrimary;
		allocInfo.commandBufferCount = Swapchain.GetImageCount();

		CommandBuffers = vk::raii::CommandBuffers(Context.GetDevice(), allocInfo);
	}

	void VulkanRenderer::CreateSyncObjects()
	{
		vk::SemaphoreCreateInfo semaphoreInfo{};
		ImageAvailableSemaphore = vk::raii::Semaphore(Context.GetDevice(), semaphoreInfo);

		RenderFinishedSemaphores.clear();
		RenderFinishedSemaphores.reserve(Swapchain.GetImageCount());
		for (size_t i = 0; i < Swapchain.GetImageCount(); ++i)
		{
			RenderFinishedSemaphores.emplace_back(Context.GetDevice(), semaphoreInfo);
		}

		vk::FenceCreateInfo fenceInfo{};
		fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled;
		InFlightFence = vk::raii::Fence(Context.GetDevice(), fenceInfo);
	}

	void VulkanRenderer::RecreateSwapChain()
	{
		WaitForValidFramebufferSize();
		Context.GetDevice().waitIdle();
		Swapchain.Recreate(Context, Window);

		CreateCommandBuffers();
		//ImGuiBackend.OnSwapchainChanged() // @todo:

		bRecreateSwapChain = false;
	}

	void VulkanRenderer::WaitForValidFramebufferSize()
	{
		int width = 0;
		int height = 0;

		while (width == 0 || height == 0)
		{
			glfwGetFramebufferSize(Window, &width, &height);
			glfwWaitEvents();
		}
	}

	void VulkanRenderer::CreateScenePipeline()
	{
		// File directory relative to the working directory of the .exe
		// Set up to be that way inside the .exe's CMake
		const std::vector<uint32_t> vertCode = ReadSpirvFile("Shaders/DefaultShader.vert.spv");
		const std::vector<uint32_t> fragCode = ReadSpirvFile("Shaders/DefaultShader.frag.spv");

		vk::raii::ShaderModule vertShaderModule = CreateShaderModule(vertCode);
		vk::raii::ShaderModule fragShaderModule = CreateShaderModule(fragCode);

		vk::PipelineShaderStageCreateInfo vertStageInfo{};
		vertStageInfo.stage = vk::ShaderStageFlagBits::eVertex;
		vertStageInfo.module = *vertShaderModule;
		vertStageInfo.pName = "main";

		vk::PipelineShaderStageCreateInfo fragStageInfo{};
		fragStageInfo.stage = vk::ShaderStageFlagBits::eFragment;
		fragStageInfo.module = *fragShaderModule;
		fragStageInfo.pName = "main";

		vk::PipelineShaderStageCreateInfo shaderStages[] = { vertStageInfo, fragStageInfo };

		const auto bindingDescription = Vertex::GetBindingDescription();
		const auto attributeDescriptions = Vertex::GetAttributeDescriptions();

		vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
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
		rasterizer.cullMode = vk::CullModeFlagBits::eNone; // @todo: Re-enable Backface Culling vk::CullModeFlagBits::eBack;
		rasterizer.frontFace = vk::FrontFace::eClockwise;
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

		std::array<vk::DynamicState, 2> dynamicStates =
		{
			vk::DynamicState::eViewport,
			vk::DynamicState::eScissor
		};

		vk::PipelineDynamicStateCreateInfo dynamicState{};
		dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
		dynamicState.pDynamicStates = dynamicStates.data();

		vk::PipelineDepthStencilStateCreateInfo depthStencil{};
		depthStencil.depthTestEnable = VK_TRUE;
		depthStencil.depthWriteEnable = VK_TRUE;
		depthStencil.depthCompareOp = vk::CompareOp::eLess;
		depthStencil.depthBoundsTestEnable = VK_FALSE;
		depthStencil.stencilTestEnable = VK_FALSE;

		vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
		vk::DescriptorSetLayout setLayouts[] = { *SceneDescriptorSetLayout };
		pipelineLayoutInfo.setLayoutCount = 1;
		pipelineLayoutInfo.pSetLayouts = setLayouts;

		ScenePipelineLayout = vk::raii::PipelineLayout(Context.GetDevice(), pipelineLayoutInfo);

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
		pipelineInfo.layout = *ScenePipelineLayout;
		pipelineInfo.renderPass = *SceneViewport.GetRenderPass();
		pipelineInfo.subpass = 0;

		ScenePipeline = std::move(vk::raii::Pipeline(
			Context.GetDevice(),
			nullptr,
			pipelineInfo
		));
	}

	void VulkanRenderer::CreateGridPipeline()
	{
		const std::vector<uint32_t> vertCode = ReadSpirvFile("Shaders/Grid.vert.spv");
		const std::vector<uint32_t> fragCode = ReadSpirvFile("Shaders/Grid.frag.spv");

		vk::raii::ShaderModule vertShaderModule = CreateShaderModule(vertCode);
		vk::raii::ShaderModule fragShaderModule = CreateShaderModule(fragCode);

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
		colorBlendAttachment.blendEnable = VK_FALSE;
		colorBlendAttachment.colorWriteMask =
			vk::ColorComponentFlagBits::eR |
			vk::ColorComponentFlagBits::eG |
			vk::ColorComponentFlagBits::eB |
			vk::ColorComponentFlagBits::eA;

		vk::PipelineColorBlendStateCreateInfo colorBlending{};
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &colorBlendAttachment;

		std::array<vk::DynamicState, 2> dynamicStates =
		{
			vk::DynamicState::eViewport,
			vk::DynamicState::eScissor
		};

		vk::PipelineDynamicStateCreateInfo dynamicState{};
		dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
		dynamicState.pDynamicStates = dynamicStates.data();

		vk::DescriptorSetLayout setLayouts[] = { *SceneDescriptorSetLayout };

		// @todo: Is the depthStencil something we want for the grid?
		// Disabled it for now because it was hiding the rendered Quad
		//vk::PipelineDepthStencilStateCreateInfo depthStencil{};
		//depthStencil.depthTestEnable = VK_TRUE;
		//depthStencil.depthWriteEnable = VK_TRUE;
		//depthStencil.depthCompareOp = vk::CompareOp::eLess;
		//depthStencil.depthBoundsTestEnable = VK_FALSE;
		//depthStencil.stencilTestEnable = VK_FALSE;

		vk::PipelineDepthStencilStateCreateInfo depthStencil{};
		depthStencil.depthTestEnable = VK_FALSE;
		depthStencil.depthWriteEnable = VK_FALSE;
		depthStencil.depthCompareOp = vk::CompareOp::eAlways;
		depthStencil.depthBoundsTestEnable = VK_FALSE;
		depthStencil.stencilTestEnable = VK_FALSE;

		vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
		pipelineLayoutInfo.setLayoutCount = 1;
		pipelineLayoutInfo.pSetLayouts = setLayouts;

		GridPipelineLayout = vk::raii::PipelineLayout(Context.GetDevice(), pipelineLayoutInfo);

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
		pipelineInfo.layout = *GridPipelineLayout;
		pipelineInfo.renderPass = *SceneViewport.GetRenderPass();
		pipelineInfo.subpass = 0;

		GridPipeline = std::move(vk::raii::Pipeline(Context.GetDevice(), nullptr, pipelineInfo));
	}

	vk::raii::ShaderModule VulkanRenderer::CreateShaderModule(const std::vector<uint32_t>& spirv)
	{
		vk::ShaderModuleCreateInfo createInfo{};
		createInfo.codeSize = spirv.size() * sizeof(uint32_t);
		createInfo.pCode = spirv.data();

		return vk::raii::ShaderModule(Context.GetDevice(), createInfo);
	}

	std::vector<uint32_t> VulkanRenderer::ReadSpirvFile(const std::string& path)
	{
		std::ifstream file(path, std::ios::ate | std::ios::binary);
		if (!file.is_open())
		{
			throw std::runtime_error("Failed to open SPIR-V file: " + path);
		}

		const std::streamsize fileSize = file.tellg();
		if (fileSize <= 0 || (fileSize % sizeof(uint32_t)) != 0)
		{
			throw std::runtime_error("Invalid SPIR-V file size: " + path);
		}

		std::vector<uint32_t> buffer(static_cast<size_t>(fileSize) / sizeof(uint32_t));

		file.seekg(0);
		file.read(reinterpret_cast<char*>(buffer.data()), fileSize);

		if (!file)
		{
			throw std::runtime_error("Failed to read SPIR-V file: " + path);
		}

		return buffer;
	}

	void VulkanRenderer::CreateSceneDescriptors()
	{
		vk::DescriptorSetLayoutBinding uboLayoutBinding{};
		uboLayoutBinding.binding = 0;
		uboLayoutBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
		uboLayoutBinding.descriptorCount = 1;
		uboLayoutBinding.stageFlags = vk::ShaderStageFlagBits::eVertex | 
			                          vk::ShaderStageFlagBits::eFragment;
		uboLayoutBinding.pImmutableSamplers = nullptr;

		vk::DescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.bindingCount = 1;
		layoutInfo.pBindings = &uboLayoutBinding;

		SceneDescriptorSetLayout = vk::raii::DescriptorSetLayout(Context.GetDevice(), layoutInfo);

		vk::DescriptorPoolSize poolSize{};
		poolSize.type = vk::DescriptorType::eUniformBuffer;
		poolSize.descriptorCount = 1;

		vk::DescriptorPoolCreateInfo poolInfo{};
		poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
		poolInfo.poolSizeCount = 1;
		poolInfo.pPoolSizes = &poolSize;
		poolInfo.maxSets = 1;

		SceneDescriptorPool = vk::raii::DescriptorPool(Context.GetDevice(), poolInfo);

		vk::DescriptorSetAllocateInfo allocInfo{};
		allocInfo.descriptorPool = *SceneDescriptorPool;
		allocInfo.descriptorSetCount = 1;
		vk::DescriptorSetLayout layout = *SceneDescriptorSetLayout;
		allocInfo.pSetLayouts = &layout;

		SceneDescriptorSets = vk::raii::DescriptorSets(Context.GetDevice(), allocInfo);

		vk::DescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = *SceneUniformBuffer;
		bufferInfo.offset = 0;
		bufferInfo.range = sizeof(SceneUBO);

		vk::WriteDescriptorSet descriptorWrite{};
		descriptorWrite.dstSet = *SceneDescriptorSets.front();
		descriptorWrite.dstBinding = 0;
		descriptorWrite.dstArrayElement = 0;
		descriptorWrite.descriptorType = vk::DescriptorType::eUniformBuffer;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.pBufferInfo = &bufferInfo;

		Context.GetDevice().updateDescriptorSets(descriptorWrite, nullptr);
	}

	void VulkanRenderer::CreateSceneUniformBuffer()
	{
		vk::DeviceSize bufferSize = sizeof(SceneUBO);

		vk::BufferCreateInfo bufferInfo{};
		bufferInfo.size = bufferSize;
		bufferInfo.usage = vk::BufferUsageFlagBits::eUniformBuffer;
		bufferInfo.sharingMode = vk::SharingMode::eExclusive;

		SceneUniformBuffer = vk::raii::Buffer(Context.GetDevice(), bufferInfo);

		vk::MemoryRequirements memRequirements = SceneUniformBuffer.getMemoryRequirements();

		vk::MemoryAllocateInfo allocInfo{};
		allocInfo.allocationSize = memRequirements.size;
		allocInfo.memoryTypeIndex = FindMemoryType(
			*Context.GetPhysicalDevice(),
			memRequirements.memoryTypeBits,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
		);

		SceneUniformBufferMemory = vk::raii::DeviceMemory(Context.GetDevice(), allocInfo);
		SceneUniformBuffer.bindMemory(*SceneUniformBufferMemory, 0);
	}

	void VulkanRenderer::UpdateSceneUniforms()
	{
		static float t = 0.0f;
		t += 0.001f;

		const vk::Extent2D extent = SceneViewport.GetExtent();
		Camera.AspectRatio = extent.height > 0
			? static_cast<float>(extent.width) / static_cast<float>(extent.height)
			: 1.0f;
		
		SceneUBO ubo{};
		ubo.ViewProj = Camera.GetViewProjectionMatrix();
		ubo.InvViewProj = glm::inverse(ubo.ViewProj);
		ubo.Model = glm::rotate(glm::mat4(1.0f), t, glm::vec3(0.0f, 1.0f, 0.0f));
		ubo.ViewportSize = glm::vec2(static_cast<float>(extent.width), static_cast<float>(extent.height));
		ubo.CameraWorldPos = Camera.Position;

		void* mapped = SceneUniformBufferMemory.mapMemory(0, sizeof(SceneUBO));
		std::memcpy(mapped, &ubo, sizeof(SceneUBO));
		SceneUniformBufferMemory.unmapMemory();
	}

	void VulkanRenderer::CreateTestMeshData()
	{
		// Construct Cube

		TestVertices =
		{
			// Front
			{ {-0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 0.0f} },
			{ { 0.5f, -0.5f,  0.5f}, {0.0f, 1.0f, 0.0f} },
			{ { 0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f} },
			{ {-0.5f,  0.5f,  0.5f}, {1.0f, 1.0f, 0.0f} },

			// Back
			{ {-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 1.0f} },
			{ { 0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 1.0f} },
			{ { 0.5f,  0.5f, -0.5f}, {1.0f, 1.0f, 1.0f} },
			{ {-0.5f,  0.5f, -0.5f}, {0.2f, 0.2f, 0.2f} }
		};

		TestIndices =
		{
			// Front
			0, 1, 2,  2, 3, 0,

			// Back
			5, 4, 7,  7, 6, 5,

			// Left
			4, 0, 3,  3, 7, 4,

			// Right
			1, 5, 6,  6, 2, 1,

			// Top
			3, 2, 6,  6, 7, 3,

			// Bottom
			4, 5, 1,  1, 0, 4
		};
	}

	void VulkanRenderer::CreateTestMeshBuffers()
	{
		const vk::DeviceSize vertexBufferSize = sizeof(Vertex) * TestVertices.size();
		const vk::DeviceSize indexBufferSize = sizeof(uint32_t) * TestIndices.size();

		CreateBuffer(
			vertexBufferSize,
			vk::BufferUsageFlagBits::eVertexBuffer,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
			TestVertexBuffer,
			TestVertexBufferMemory
		);

		CreateBuffer(
			indexBufferSize,
			vk::BufferUsageFlagBits::eIndexBuffer,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
			TestIndexBuffer,
			TestIndexBufferMemory
		);

		{
			void* mapped = TestVertexBufferMemory.mapMemory(0, vertexBufferSize);
			std::memcpy(mapped, TestVertices.data(), static_cast<size_t>(vertexBufferSize));
			TestVertexBufferMemory.unmapMemory();
		}

		{
			void* mapped = TestIndexBufferMemory.mapMemory(0, indexBufferSize);
			std::memcpy(mapped, TestIndices.data(), static_cast<size_t>(indexBufferSize));
			TestIndexBufferMemory.unmapMemory();
		}
	}

	void VulkanRenderer::CreateBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties, vk::raii::Buffer& outBuffer, vk::raii::DeviceMemory& outMemory)
	{
		vk::BufferCreateInfo bufferInfo{};
		bufferInfo.size = size;
		bufferInfo.usage = usage;
		bufferInfo.sharingMode = vk::SharingMode::eExclusive;

		outBuffer = vk::raii::Buffer(Context.GetDevice(), bufferInfo);

		const vk::MemoryRequirements memRequirements = outBuffer.getMemoryRequirements();

		vk::MemoryAllocateInfo allocInfo{};
		allocInfo.allocationSize = memRequirements.size;
		allocInfo.memoryTypeIndex = FindMemoryType(
			*Context.GetPhysicalDevice(),
			memRequirements.memoryTypeBits,
			properties
		);

		outMemory = vk::raii::DeviceMemory(Context.GetDevice(), allocInfo);
		outBuffer.bindMemory(*outMemory, 0);
	}

	uint32_t VulkanRenderer::FindMemoryType(vk::PhysicalDevice physicalDevice, uint32_t typeFilter, vk::MemoryPropertyFlags properties)
	{
		const vk::PhysicalDeviceMemoryProperties memProperties = physicalDevice.getMemoryProperties();

		for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i)
		{
			if ((typeFilter & (1 << i)) &&
				(memProperties.memoryTypes[i].propertyFlags & properties) == properties)
			{
				return i;
			}
		}

		throw std::runtime_error("Failed to find suitable memory type.");
	}

	float VulkanRenderer::ComputeDeltaTime()
	{
		static double lastTime = glfwGetTime();
		const double currentTime = glfwGetTime();
		const float deltaTime = static_cast<float>(currentTime - lastTime);
		lastTime = currentTime;
		return deltaTime;
	}

	void VulkanRenderer::OnMouseWheelScrolled(double yOffset)
	{
		// Don't change anything if the mouse interacting with UI
		if (ImGui::GetIO().WantCaptureMouse && !bSceneViewportHovered)
		{
			return;
		}

		if (yOffset > 0.0)
		{
			CameraMoveSpeed *= CameraSpeedStep;
		}
		else if (yOffset < 0.0)
		{
			CameraMoveSpeed /= CameraSpeedStep;
		}

		CameraMoveSpeed = glm::clamp(CameraMoveSpeed, CameraSpeedMin, CameraSpeedMax);
	}

	void VulkanRenderer::SetSceneWindowHovered(bool hovered)
	{
		bSceneViewportHovered = hovered;
	}
}