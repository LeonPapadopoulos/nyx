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

#include "backends/imgui_impl_vulkan.h"

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

		// OffscreenRenderTarget
		{
			SceneTarget.Initialize(Context, 1280, 720, vk::Format::eR8G8B8A8Unorm);

			vk::SamplerCreateInfo samplerInfo{};
			samplerInfo.magFilter = vk::Filter::eLinear;
			samplerInfo.minFilter = vk::Filter::eLinear;
			samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
			samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
			samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
			samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
			samplerInfo.minLod = 0.0f;
			samplerInfo.maxLod = 0.0f;

			SceneSampler = vk::raii::Sampler(Context.GetDevice(), samplerInfo);

			// @todo: Is this cast really the way to go? ImGui says VkDescriptorSet == ImTextureID
			SceneTextureId = (ImTextureID)ImGui_ImplVulkan_AddTexture(
				*SceneSampler,
				SceneTarget.GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			);

			vk::AttachmentDescription colorAttachment{};
			colorAttachment.format = SceneTarget.GetFormat();
			colorAttachment.samples = vk::SampleCountFlagBits::e1;
			colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
			colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
			colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
			colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
			colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
			colorAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

			vk::AttachmentReference colorRef{};
			colorRef.attachment = 0;
			colorRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

			vk::SubpassDescription subpass{};
			subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
			subpass.colorAttachmentCount = 1;
			subpass.pColorAttachments = &colorRef;

			vk::RenderPassCreateInfo rpInfo{};
			rpInfo.attachmentCount = 1;
			rpInfo.pAttachments = &colorAttachment;
			rpInfo.subpassCount = 1;
			rpInfo.pSubpasses = &subpass;

			OffscreenRenderPass = vk::raii::RenderPass(Context.GetDevice(), rpInfo);

			vk::ImageView attachments[] = { SceneTarget.GetImageView() };

			vk::FramebufferCreateInfo fbInfo{};
			fbInfo.renderPass = *OffscreenRenderPass;
			fbInfo.attachmentCount = 1;
			fbInfo.pAttachments = attachments;
			fbInfo.width = SceneTarget.GetExtent().width;
			fbInfo.height = SceneTarget.GetExtent().height;
			fbInfo.layers = 1;

			OffscreenFramebuffer = vk::raii::Framebuffer(Context.GetDevice(), fbInfo);
		}
	}

	void VulkanRenderer::Shutdown()
	{
		if (*Context.GetDevice())
		{
			Context.GetDevice().waitIdle();
		}

		if (ImGuiBackend)
		{
			ImGuiBackend->Shutdown();
			ImGuiBackend.reset();
		}

		SceneTarget.Shutdown();

		Swapchain.Shutdown();

		// destroy command buffers / semaphores / fences / pools
		CommandBuffers.clear();
		RenderFinishedSemaphores.clear();
		ImageAvailableSemaphore = nullptr;
		InFlightFence = nullptr;
		CommandPool = nullptr;

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

		// OffscreenRenderTarget
		{
			vk::ClearValue offscreenClear{};
			offscreenClear.color = vk::ClearColorValue(std::array<float, 4>{ 0.2f, 0.05f, 0.35f, 1.0f });

			vk::RenderPassBeginInfo offscreenRpInfo{};
			offscreenRpInfo.renderPass = *OffscreenRenderPass;
			offscreenRpInfo.framebuffer = *OffscreenFramebuffer;
			offscreenRpInfo.renderArea.offset = vk::Offset2D{ 0, 0 };
			offscreenRpInfo.renderArea.extent = SceneTarget.GetExtent();
			offscreenRpInfo.clearValueCount = 1;
			offscreenRpInfo.pClearValues = &offscreenClear;

			cmd.beginRenderPass(offscreenRpInfo, vk::SubpassContents::eInline);
			cmd.endRenderPass();
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
}