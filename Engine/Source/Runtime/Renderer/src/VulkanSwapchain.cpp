#include "NyxPCH.h"
#include "VulkanSwapchain.h"
#include "Assertions.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace Nyx
{
	void VulkanSwapchain::Initialize(VulkanContext& context, GLFWwindow* window)
	{
		CreateSwapchain(context, window);
		CreateImageViews(context);
		CreateRenderPass(context);
		CreateFramebuffers(context);
	}

	void VulkanSwapchain::Shutdown()
	{
		Cleanup();
	}


	void VulkanSwapchain::Recreate(VulkanContext& context, GLFWwindow* window)
	{
		Cleanup();

		CreateSwapchain(context, window);
		CreateImageViews(context);
		CreateRenderPass(context);
		CreateFramebuffers(context);
	}

	void VulkanSwapchain::Cleanup()
	{
		Framebuffers.clear();
		RenderPass = nullptr;
		SwapchainImageViews.clear();
		SwapchainImages.clear();
		Swapchain = nullptr;
	}

	vk::raii::SwapchainKHR& VulkanSwapchain::GetSwapchain()
	{
		return Swapchain;
	}

	const std::vector<vk::Image>& VulkanSwapchain::GetImages() const
	{
		return SwapchainImages;
	}

	const std::vector<vk::raii::ImageView>& VulkanSwapchain::GetImageViews() const
	{
		return SwapchainImageViews;
	}

	const std::vector<vk::raii::Framebuffer>& VulkanSwapchain::GetFramebuffers() const
	{
		return Framebuffers;
	}

	vk::raii::RenderPass& VulkanSwapchain::GetRenderPass()
	{
		return RenderPass;
	}

	const vk::Extent2D& VulkanSwapchain::GetExtent() const
	{
		return Extent;
	}

	const vk::SurfaceFormatKHR& VulkanSwapchain::GetSurfaceFormat() const
	{
		return SurfaceFormat;
	}

	uint32_t VulkanSwapchain::GetMinImageCount() const
	{
		return MinImageCount;
	}

	uint32_t VulkanSwapchain::GetImageCount() const
	{
		return static_cast<uint32_t>(SwapchainImages.size());
	}

	void VulkanSwapchain::CreateSwapchain(VulkanContext& context, GLFWwindow* window)
	{
		vk::raii::SurfaceKHR& Surface = context.GetSurface();

		vk::SurfaceCapabilitiesKHR surfaceCapabilities = context.GetPhysicalDevice().getSurfaceCapabilitiesKHR(Surface);
		Extent = ChooseSwapExtent(window, surfaceCapabilities);
		MinImageCount = ChooseSwapMinImageCount(surfaceCapabilities);

		std::vector<vk::SurfaceFormatKHR> availableFormats = context.GetPhysicalDevice().getSurfaceFormatsKHR(Surface);
		SurfaceFormat = ChooseSwapSurfaceFormat(availableFormats);

		std::vector<vk::PresentModeKHR> availablePresentModes = context.GetPhysicalDevice().getSurfacePresentModesKHR(Surface);
		vk::PresentModeKHR presentMode = ChooseSwapPresentMode(availablePresentModes);

		vk::SwapchainCreateInfoKHR swapchainCreateInfo{};
		swapchainCreateInfo.surface = *context.GetSurface();
		swapchainCreateInfo.minImageCount = MinImageCount;
		swapchainCreateInfo.imageFormat = SurfaceFormat.format;
		swapchainCreateInfo.imageColorSpace = SurfaceFormat.colorSpace;
		swapchainCreateInfo.imageExtent = Extent;
		swapchainCreateInfo.imageArrayLayers = 1;
		swapchainCreateInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
		swapchainCreateInfo.imageSharingMode = vk::SharingMode::eExclusive;
		swapchainCreateInfo.preTransform = surfaceCapabilities.currentTransform;
		swapchainCreateInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
		swapchainCreateInfo.presentMode = presentMode;
		swapchainCreateInfo.clipped = true;

		Swapchain = vk::raii::SwapchainKHR(context.GetDevice(), swapchainCreateInfo);
		SwapchainImages = Swapchain.getImages();
	}


	void VulkanSwapchain::CreateImageViews(VulkanContext& context)
	{
		ASSERT(SwapchainImageViews.empty());

		vk::ImageViewCreateInfo imageViewCreateInfo{};
		imageViewCreateInfo.viewType = vk::ImageViewType::e2D;
		imageViewCreateInfo.format = SurfaceFormat.format;
		imageViewCreateInfo.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };

		for (auto& image : SwapchainImages)
		{
			imageViewCreateInfo.image = image;
			SwapchainImageViews.emplace_back(context.GetDevice(), imageViewCreateInfo);
		}
	}


	void VulkanSwapchain::CreateRenderPass(VulkanContext& context)
	{
		vk::AttachmentDescription colorAttachment{};
		colorAttachment.format = SurfaceFormat.format;
		colorAttachment.samples = vk::SampleCountFlagBits::e1;
		colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
		colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
		colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
		colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
		colorAttachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;

		vk::AttachmentReference colorRef{};
		colorRef.attachment = 0;
		colorRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

		vk::SubpassDescription subpass{};
		subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorRef;

		vk::SubpassDependency dependency{};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

		vk::RenderPassCreateInfo renderPassInfo{};
		renderPassInfo.attachmentCount = 1;
		renderPassInfo.pAttachments = &colorAttachment;
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;
		renderPassInfo.dependencyCount = 1;
		renderPassInfo.pDependencies = &dependency;

		RenderPass = vk::raii::RenderPass(context.GetDevice(), renderPassInfo);
	}

	void VulkanSwapchain::CreateFramebuffers(VulkanContext& context)
	{
		Framebuffers.clear();
		Framebuffers.reserve(SwapchainImageViews.size());

		for (const vk::raii::ImageView& imageView : SwapchainImageViews)
		{
			vk::ImageView attachments[] = { *imageView };

			vk::FramebufferCreateInfo framebufferInfo{};
			framebufferInfo.renderPass = *RenderPass;
			framebufferInfo.attachmentCount = 1;
			framebufferInfo.pAttachments = attachments;
			framebufferInfo.width = Extent.width;
			framebufferInfo.height = Extent.height;
			framebufferInfo.layers = 1;

			Framebuffers.emplace_back(context.GetDevice(), framebufferInfo);
		}
	}


	uint32_t VulkanSwapchain::ChooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const& surfaceCapabilities)
	{
		auto minImageCount = std::max(3u, surfaceCapabilities.minImageCount);
		if ((0 < surfaceCapabilities.maxImageCount) && (surfaceCapabilities.maxImageCount < minImageCount))
		{
			minImageCount = surfaceCapabilities.maxImageCount;
		}
		return minImageCount;
	}

	vk::SurfaceFormatKHR VulkanSwapchain::ChooseSwapSurfaceFormat(std::vector<vk::SurfaceFormatKHR> const& availableFormats)
	{
		ASSERT(!availableFormats.empty());

		// Preferred: UNORM format with standard SRGB nonlinear presentation colorspace.
		// This avoids the washed-out ImGuiBackend look you can get with SRGB swapchain formats.
		if (const auto it = std::ranges::find_if(
			availableFormats,
			[](const vk::SurfaceFormatKHR& format)
			{
				return format.format == vk::Format::eB8G8R8A8Unorm &&
					format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
			});
			it != availableFormats.end())
		{
			return *it;
		}

		return availableFormats[0];
	}

	vk::PresentModeKHR VulkanSwapchain::ChooseSwapPresentMode(std::vector<vk::PresentModeKHR> const& availablePresentModes)
	{
		ASSERT(std::ranges::any_of(availablePresentModes, [](auto presentMode) { return presentMode == vk::PresentModeKHR::eFifo; }));
		return std::ranges::any_of(availablePresentModes,
			[](const vk::PresentModeKHR value) { return vk::PresentModeKHR::eMailbox == value; }) ?
			vk::PresentModeKHR::eMailbox :
			vk::PresentModeKHR::eFifo;
	}

	vk::Extent2D VulkanSwapchain::ChooseSwapExtent(GLFWwindow* window, const vk::SurfaceCapabilitiesKHR& capabilities)
	{
		if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
		{
			return capabilities.currentExtent;
		}
		int width, height;
		glfwGetFramebufferSize(window, &width, &height);

		return {
			std::clamp<uint32_t>(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
			std::clamp<uint32_t>(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height) };
	}
}