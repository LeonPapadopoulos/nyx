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

		SceneViewport.Initialize(Context, 1280, 720, vk::Format::eR8G8B8A8Unorm);
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
				Context.GetDevice().waitIdle();
				SceneViewport.Recreate(Context, PendingSceneViewportWidth, PendingSceneViewportHeight, vk::Format::eR8G8B8A8Unorm);
				bSceneViewportResizePending = false;
			}

			vk::ClearValue clear{};
			clear.color = vk::ClearColorValue(std::array<float, 4>{ 0.2f, 0.05f, 0.35f, 1.0f });

			SceneViewport.BeginRenderPass(cmd, clear);
			// draw scene later
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