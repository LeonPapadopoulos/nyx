#include "NyxPCH.h"
#include "Assertions.h"
#include "VulkanRenderer.h"
#include "ImGuiVulkanUtil.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <ranges>
#include <string>

static std::vector<char const*> s_ValidationLayers =
{
	"VK_LAYER_KHRONOS_validation"
};

#ifdef NDEBUG
static bool s_bEnableValidationLayers = false;
#else
static bool s_bEnableValidationLayers = true;
#endif

static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity, vk::DebugUtilsMessageTypeFlagsEXT type, const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData, void*)
{
	if (severity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eError || severity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning)
	{
		std::cerr << "validation layer: type " << to_string(type) << " msg: " << pCallbackData->pMessage << std::endl;
	}

	return vk::False;
}


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
		ASSERT(GetMinImageCount() >= 2 && "Failed to fulfill ImGui Vulkan requirements.");
		ASSERT(GetSwapChainImageCount() >= GetMinImageCount() && "Failed to fulfill ImGui Vulkan requirements.");
		SetupImGui();
	}

	void VulkanRenderer::Shutdown()
	{
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
		CreateInstance(applicationName);
		SetupDebugMessenger();
		CreateSurface(window);
		PickPhysicalDevice();
		CreateLogicalDevice();
		CreateSwapChain();
		CreateImageViews();
		CreateGraphicsPipeline();

		// @todo: check order
		CreateCommandPool();
		CreateRenderPass();
		CreateFramebuffers();
		CreateCommandBuffers();
		CreateSyncObjects();
	}

	void VulkanRenderer::SetupImGui()
	{
		ImGui = std::make_unique<ImGuiVulkanUtil>(Window, *this);
		ImGui->Initialize(
			static_cast<float>(GetSwapChainExtent().width),
			static_cast<float>(GetSwapChainExtent().height));
	}

	void VulkanRenderer::DrawFrame(const std::function<void()>& buildUI)
	{
		// 1) Wait until previous submitted frame is done
		Device.waitForFences({ *InFlightFence }, true, UINT64_MAX);

		// 2) Acquire next swapchain image
		const vk::ResultValue<uint32_t> acquireResult =
			SwapChain.acquireNextImage(UINT64_MAX, *ImageAvailableSemaphore);

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
		Device.resetFences({ *InFlightFence });

		// 3) Record command buffer for this image
		vk::raii::CommandBuffer& cmd = CommandBuffers[imageIndex];
		cmd.reset();

		vk::CommandBufferBeginInfo beginInfo{};
		cmd.begin(beginInfo);

		vk::ClearValue clearValue{};
		clearValue.color = vk::ClearColorValue(std::array<float, 4>{ 0.1f, 0.1f, 0.1f, 1.0f });

		vk::RenderPassBeginInfo renderPassInfo{};
		renderPassInfo.renderPass = *RenderPass;
		renderPassInfo.framebuffer = *Framebuffers[imageIndex];
		renderPassInfo.renderArea.offset = vk::Offset2D{ 0, 0 };
		renderPassInfo.renderArea.extent = SwapChainExtent;
		renderPassInfo.clearValueCount = 1;
		renderPassInfo.pClearValues = &clearValue;

		cmd.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

		// This is where ImGui records into the active frame command buffer.
		{
			ImGui->BeginFrame();
			buildUI();
			ImGui->DrawFrame(cmd);
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

		GraphicsQueue.submit(submitInfo, *InFlightFence);

		// 5) Present
		vk::PresentInfoKHR presentInfo{};
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &*renderFinishedSemaphore;
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &*SwapChain;
		presentInfo.pImageIndices = &imageIndex;

		const vk::Result presentResult = GraphicsQueue.presentKHR(presentInfo);
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
		if (*Device)
		{
			Device.waitIdle();
		}
	}

	vk::raii::Device& VulkanRenderer::GetDevice()
	{
		return Device;
	}

	vk::raii::PhysicalDevice& VulkanRenderer::GetPhysicalDevice()
	{
		return PhysicalDevice;
	}

	vk::raii::Queue& VulkanRenderer::GetGraphicsQueue()
	{
		return GraphicsQueue;
	}

	const vk::Extent2D& VulkanRenderer::GetSwapChainExtent()
	{
		return SwapChainExtent;
	}

	void VulkanRenderer::CreateInstance(const std::string& applicationName)
	{
		vk::ApplicationInfo appInfo(
			applicationName.c_str(),
			VK_MAKE_VERSION(1, 0, 0),
			"Nyx Engine",
			VK_MAKE_VERSION(1, 0, 0),
			vk::ApiVersion14);

		// Validation Layers
		std::vector<char const*> requiredLayers;
		{
			// Get the required layers
			if (s_bEnableValidationLayers)
			{
				requiredLayers.assign(s_ValidationLayers.begin(), s_ValidationLayers.end());
			}

			// Check if the required layers are supported by the Vulkan implementation.
			auto layerProperties = Context.enumerateInstanceLayerProperties();
			auto unsupportedLayerIt = std::ranges::find_if(requiredLayers,
				[&layerProperties](auto const& requiredLayer) {
					return std::ranges::none_of(layerProperties,
						[requiredLayer](auto const& layerProperty) { return strcmp(layerProperty.layerName, requiredLayer) == 0; });
				});
			if (unsupportedLayerIt != requiredLayers.end())
			{
				throw std::runtime_error("Required layer not supported: " + std::string(*unsupportedLayerIt));
			}
		}

		// Get the required GLFW extensions.
		auto requiredExtensions = GetRequiredInstanceExtensions();
		{
			// Check if the required extensions are supported by the Vulkan implementation.
			auto extensionProperties = Context.enumerateInstanceExtensionProperties();
			auto unsupportedPropertyIt =
				std::ranges::find_if(requiredExtensions,
					[&extensionProperties](auto const& requiredExtension) {
						return std::ranges::none_of(extensionProperties,
							[requiredExtension](auto const& extensionProperty) { return strcmp(extensionProperty.extensionName, requiredExtension) == 0; });
					});
			if (unsupportedPropertyIt != requiredExtensions.end())
			{
				throw std::runtime_error("Required extension not supported: " + std::string(*unsupportedPropertyIt));
			}
		}

		vk::InstanceCreateInfo createInfo{};
		createInfo.pApplicationInfo = &appInfo;
		createInfo.enabledLayerCount = static_cast<uint32_t>(requiredLayers.size());
		createInfo.ppEnabledLayerNames = requiredLayers.data();
		createInfo.enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size());
		createInfo.ppEnabledExtensionNames = requiredExtensions.data();

		Instance = vk::raii::Instance(Context, createInfo);
		// @todo: Error handling
	}

	void VulkanRenderer::SetupDebugMessenger()
	{
		if (!s_bEnableValidationLayers)
			return;

		vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
			vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
			vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);
		vk::DebugUtilsMessageTypeFlagsEXT     messageTypeFlags(
			vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);
		vk::DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfoEXT{};
		debugUtilsMessengerCreateInfoEXT.messageSeverity = severityFlags;
		debugUtilsMessengerCreateInfoEXT.messageType = messageTypeFlags;
		debugUtilsMessengerCreateInfoEXT.pfnUserCallback = &debugCallback;
		DebugMessenger = Instance.createDebugUtilsMessengerEXT(debugUtilsMessengerCreateInfoEXT);
	}

	void VulkanRenderer::CreateSurface(GLFWwindow* window)
	{
		VkSurfaceKHR _surface;
		if (glfwCreateWindowSurface(*Instance, window, nullptr, &_surface) != 0)
		{
			throw std::runtime_error("failed to create window surface!");
		}
		Surface = vk::raii::SurfaceKHR(Instance, _surface);
	}

	void VulkanRenderer::PickPhysicalDevice()
	{
		std::vector<vk::raii::PhysicalDevice> physicalDevices = Instance.enumeratePhysicalDevices();
		auto const devIter = std::ranges::find_if(physicalDevices, [&](auto const& physicalDevice) { return IsDeviceSuitable(physicalDevice); });
		if (devIter == physicalDevices.end())
		{
			throw std::runtime_error("failed to find a suitable GPU!");
		}
		PhysicalDevice = *devIter;
	}

	void VulkanRenderer::CreateLogicalDevice()
	{
		std::vector<vk::QueueFamilyProperties> queueFamilyProperties = PhysicalDevice.getQueueFamilyProperties();

		// get the first index into queueFamilyProperties which supports both graphics and present
		uint32_t graphicsQueueFamilyIndex = ~0;
		for (uint32_t qfpIndex = 0; qfpIndex < queueFamilyProperties.size(); qfpIndex++)
		{
			if ((queueFamilyProperties[qfpIndex].queueFlags & vk::QueueFlagBits::eGraphics) &&
				PhysicalDevice.getSurfaceSupportKHR(qfpIndex, *Surface))
			{
				// found a queue family that supports both graphics and present
				graphicsQueueFamilyIndex = qfpIndex;
				break;
			}
		}
		if (graphicsQueueFamilyIndex == ~0)
		{
			throw std::runtime_error("Could not find a queue for graphics and present -> terminating");
		}

		// Create a chain of feature structures
		vk::PhysicalDeviceFeatures2 physicalDeviceFeatures2{};
		vk::PhysicalDeviceVulkan13Features physicalDeviceVulkan13Features{};
		physicalDeviceVulkan13Features.dynamicRendering = true;
		vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT physicalDeviceExtendedDynamicStateFeaturesEXT{};
		physicalDeviceExtendedDynamicStateFeaturesEXT.extendedDynamicState = true;
		vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features, vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT> featureChain = {
			physicalDeviceFeatures2,                        // vk::PhysicalDeviceFeatures2 (empty for now)
			physicalDeviceVulkan13Features,					// Enable dynamic rendering from Vulkan 1.3
			physicalDeviceExtendedDynamicStateFeaturesEXT   // Enable extended dynamic state from the extension
		};

		// create a Device
		float queuePriority = 0.5f;
		vk::DeviceQueueCreateInfo deviceQueueCreateInfo{};
		deviceQueueCreateInfo.queueFamilyIndex = graphicsQueueFamilyIndex;
		deviceQueueCreateInfo.queueCount = 1;
		deviceQueueCreateInfo.pQueuePriorities = &queuePriority;

		vk::DeviceCreateInfo deviceCreateInfo{};
		deviceCreateInfo.pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>();
		deviceCreateInfo.queueCreateInfoCount = 1;
		deviceCreateInfo.pQueueCreateInfos = &deviceQueueCreateInfo;
		deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(RequiredDeviceExtension.size());
		deviceCreateInfo.ppEnabledExtensionNames = RequiredDeviceExtension.data();

		Device = vk::raii::Device(PhysicalDevice, deviceCreateInfo);
		GraphicsQueue = vk::raii::Queue(Device, graphicsQueueFamilyIndex, 0 /*queueInThatFamily*/);
		GraphicsQueueFamily = graphicsQueueFamilyIndex;
	}

	void VulkanRenderer::CreateSwapChain()
	{
		vk::SurfaceCapabilitiesKHR surfaceCapabilities = PhysicalDevice.getSurfaceCapabilitiesKHR(*Surface);
		SwapChainExtent = ChooseSwapExtent(surfaceCapabilities);
		MinImageCount = ChooseSwapMinImageCount(surfaceCapabilities);

		std::vector<vk::SurfaceFormatKHR> availableFormats = PhysicalDevice.getSurfaceFormatsKHR(*Surface);
		SwapChainSurfaceFormat = ChooseSwapSurfaceFormat(availableFormats);

		std::vector<vk::PresentModeKHR> availablePresentModes = PhysicalDevice.getSurfacePresentModesKHR(*Surface);
		vk::PresentModeKHR presentMode = ChooseSwapPresentMode(availablePresentModes);

		vk::SwapchainCreateInfoKHR swapChainCreateInfo{};
		swapChainCreateInfo.surface = *Surface;
		swapChainCreateInfo.minImageCount = MinImageCount;
		swapChainCreateInfo.imageFormat = SwapChainSurfaceFormat.format;
		swapChainCreateInfo.imageColorSpace = SwapChainSurfaceFormat.colorSpace;
		swapChainCreateInfo.imageExtent = SwapChainExtent;
		swapChainCreateInfo.imageArrayLayers = 1;
		swapChainCreateInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
		swapChainCreateInfo.imageSharingMode = vk::SharingMode::eExclusive;
		swapChainCreateInfo.preTransform = surfaceCapabilities.currentTransform;
		swapChainCreateInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
		swapChainCreateInfo.presentMode = presentMode;
		swapChainCreateInfo.clipped = true;

		SwapChain = vk::raii::SwapchainKHR(Device, swapChainCreateInfo);
		SwapChainImages = SwapChain.getImages();
	}

	void VulkanRenderer::CreateImageViews()
	{
		assert(SwapChainImageViews.empty());

		vk::ImageViewCreateInfo imageViewCreateInfo{};
		imageViewCreateInfo.viewType = vk::ImageViewType::e2D;
		imageViewCreateInfo.format = SwapChainSurfaceFormat.format;
		imageViewCreateInfo.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };

		for (auto& image : SwapChainImages)
		{
			imageViewCreateInfo.image = image;
			SwapChainImageViews.emplace_back(Device, imageViewCreateInfo);
		}
	}

	void VulkanRenderer::CreateGraphicsPipeline()
	{
		// @todo:
	}

	void VulkanRenderer::CreateCommandPool()
	{
		vk::CommandPoolCreateInfo poolInfo{};
		poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
		poolInfo.queueFamilyIndex = GraphicsQueueFamily;

		CommandPool = vk::raii::CommandPool(Device, poolInfo);
	}

	void VulkanRenderer::CreateRenderPass()
	{
		vk::AttachmentDescription colorAttachment{};
		colorAttachment.format = SwapChainSurfaceFormat.format;
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

		RenderPass = vk::raii::RenderPass(Device, renderPassInfo);
	}

	void VulkanRenderer::CreateFramebuffers()
	{
		Framebuffers.clear();
		Framebuffers.reserve(SwapChainImageViews.size());

		for (const vk::raii::ImageView& imageView : SwapChainImageViews)
		{
			vk::ImageView attachments[] = { *imageView };

			vk::FramebufferCreateInfo framebufferInfo{};
			framebufferInfo.renderPass = *RenderPass;
			framebufferInfo.attachmentCount = 1;
			framebufferInfo.pAttachments = attachments;
			framebufferInfo.width = SwapChainExtent.width;
			framebufferInfo.height = SwapChainExtent.height;
			framebufferInfo.layers = 1;

			Framebuffers.emplace_back(Device, framebufferInfo);
		}
	}

	void VulkanRenderer::CreateCommandBuffers()
	{
		vk::CommandBufferAllocateInfo allocInfo{};
		allocInfo.commandPool = *CommandPool;
		allocInfo.level = vk::CommandBufferLevel::ePrimary;
		allocInfo.commandBufferCount = static_cast<uint32_t>(SwapChainImages.size());

		CommandBuffers = vk::raii::CommandBuffers(Device, allocInfo);
	}

	void VulkanRenderer::CreateSyncObjects()
	{
		vk::SemaphoreCreateInfo semaphoreInfo{};
		ImageAvailableSemaphore = vk::raii::Semaphore(Device, semaphoreInfo);

		RenderFinishedSemaphores.clear();
		RenderFinishedSemaphores.reserve(SwapChainImages.size());
		for (size_t i = 0; i < SwapChainImages.size(); ++i)
		{
			RenderFinishedSemaphores.emplace_back(Device, semaphoreInfo);
		}

		vk::FenceCreateInfo fenceInfo{};
		fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled;
		InFlightFence = vk::raii::Fence(Device, fenceInfo);
	}

	void VulkanRenderer::CleanupSwapChain()
	{
		CommandBuffers.clear();
		Framebuffers.clear();
		RenderPass = nullptr;
		SwapChainImageViews.clear();
		SwapChain = nullptr;
		SwapChainImages.clear();
	}

	void VulkanRenderer::RecreateSwapChain()
	{
		int width = 0;
		int height = 0;

		while (width == 0 || height == 0)
		{
			glfwGetFramebufferSize(Window, &width, &height);
			glfwWaitEvents();
		}

		Device.waitIdle();

		CleanupSwapChain();

		CreateSwapChain();
		CreateImageViews();
		CreateRenderPass();
		CreateFramebuffers();
		CreateCommandBuffers();

		bRecreateSwapChain = false;
	}

	uint32_t VulkanRenderer::ChooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const& surfaceCapabilities)
	{
		auto minImageCount = std::max(3u, surfaceCapabilities.minImageCount);
		if ((0 < surfaceCapabilities.maxImageCount) && (surfaceCapabilities.maxImageCount < minImageCount))
		{
			minImageCount = surfaceCapabilities.maxImageCount;
		}
		return minImageCount;
	}

	vk::SurfaceFormatKHR VulkanRenderer::ChooseSwapSurfaceFormat(std::vector<vk::SurfaceFormatKHR> const& availableFormats)
	{
		assert(!availableFormats.empty());

		// Preferred: UNORM format with standard SRGB nonlinear presentation colorspace.
		// This avoids the washed-out ImGui look you can get with SRGB swapchain formats.
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

	vk::PresentModeKHR VulkanRenderer::ChooseSwapPresentMode(std::vector<vk::PresentModeKHR> const& availablePresentModes)
	{
		assert(std::ranges::any_of(availablePresentModes, [](auto presentMode) { return presentMode == vk::PresentModeKHR::eFifo; }));
		return std::ranges::any_of(availablePresentModes,
			[](const vk::PresentModeKHR value) { return vk::PresentModeKHR::eMailbox == value; }) ?
			vk::PresentModeKHR::eMailbox :
			vk::PresentModeKHR::eFifo;
	}

	vk::Extent2D VulkanRenderer::ChooseSwapExtent(vk::SurfaceCapabilitiesKHR const& capabilities)
	{
		if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
		{
			return capabilities.currentExtent;
		}
		int width, height;
		glfwGetFramebufferSize(Window, &width, &height);

		return {
			std::clamp<uint32_t>(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
			std::clamp<uint32_t>(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height) };
	}

	std::vector<const char*> VulkanRenderer::GetRequiredInstanceExtensions()
	{
		uint32_t glfwExtensionCount = 0;
		auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

		std::vector extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
		if (s_bEnableValidationLayers)
		{
			extensions.push_back(vk::EXTDebugUtilsExtensionName);
		}

		return extensions;
	}

	bool VulkanRenderer::IsDeviceSuitable(const vk::raii::PhysicalDevice& physicalDevice)
	{
		// Check if the physicalDevice supports the Vulkan 1.3 API version
		bool supportsVulkan1_3 = physicalDevice.getProperties().apiVersion >= vk::ApiVersion13;

		// Check if any of the queue families support graphics operations
		auto queueFamilies = physicalDevice.getQueueFamilyProperties();
		bool supportsGraphics = std::ranges::any_of(queueFamilies, [](auto const& qfp) { return !!(qfp.queueFlags & vk::QueueFlagBits::eGraphics); });

		// Check if all required physicalDevice extensions are available
		auto availableDeviceExtensions = physicalDevice.enumerateDeviceExtensionProperties();
		bool supportsAllRequiredExtensions =
			std::ranges::all_of(RequiredDeviceExtension,
				[&availableDeviceExtensions](auto const& requiredDeviceExtension)
				{
					return std::ranges::any_of(availableDeviceExtensions,
						[requiredDeviceExtension](auto const& availableDeviceExtension)
						{ return strcmp(availableDeviceExtension.extensionName, requiredDeviceExtension) == 0; });
				});

		// Check if the physicalDevice supports the required features (dynamic rendering and extended dynamic state)
		auto features =
			physicalDevice
			.template getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features, vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
		bool supportsRequiredFeatures = features.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
			features.template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState;

		// Return true if the physicalDevice meets all the criteria
		return supportsVulkan1_3 && supportsGraphics && supportsAllRequiredExtensions && supportsRequiredFeatures;
	}
}