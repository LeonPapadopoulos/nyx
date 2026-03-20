#include "NyxPCH.h"
#include "VulkanContext.h"
#include <ranges>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

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
	void VulkanContext::Initialize(const char* applicationName, GLFWwindow* window)
	{
		CreateInstance(applicationName);
		SetupDebugMessenger();
		CreateSurface(window);
		PickPhysicalDevice();
		CreateLogicalDevice();
	}

	void VulkanContext::Shutdown()
	{
		// raii resources cleanup automatically
	}

	vk::raii::Instance& VulkanContext::GetInstance()
	{
		return Instance;
	}

	vk::raii::PhysicalDevice& VulkanContext::GetPhysicalDevice()
	{
		return PhysicalDevice;
	}

	vk::raii::Device& VulkanContext::GetDevice()
	{
		return Device;
	}

	vk::raii::SurfaceKHR& VulkanContext::GetSurface()
	{
		return Surface;
	}

	vk::raii::Queue& VulkanContext::GetGraphicsQueue()
	{
		return GraphicsQueue;
	}

	uint32_t VulkanContext::GetGraphicsQueueFamily() const
	{
		return GraphicsQueueFamily;
	}

	void VulkanContext::CreateInstance(const char* applicationName)
	{
		vk::ApplicationInfo appInfo(
			applicationName,
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

	void VulkanContext::SetupDebugMessenger()
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

	void VulkanContext::CreateSurface(GLFWwindow* window)
	{
		VkSurfaceKHR _surface;
		if (glfwCreateWindowSurface(*Instance, window, nullptr, &_surface) != 0)
		{
			throw std::runtime_error("failed to create window surface!");
		}
		Surface = vk::raii::SurfaceKHR(Instance, _surface);
	}

	void VulkanContext::PickPhysicalDevice()
	{
		std::vector<vk::raii::PhysicalDevice> physicalDevices = Instance.enumeratePhysicalDevices();
		auto const devIter = std::ranges::find_if(physicalDevices, [&](auto const& physicalDevice) { return IsDeviceSuitable(physicalDevice); });
		if (devIter == physicalDevices.end())
		{
			throw std::runtime_error("failed to find a suitable GPU!");
		}
		PhysicalDevice = *devIter;
	}

	void VulkanContext::CreateLogicalDevice()
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

	std::vector<const char*> VulkanContext::GetRequiredInstanceExtensions()
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

	bool VulkanContext::IsDeviceSuitable(const vk::raii::PhysicalDevice& physicalDevice)
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