#include "NyxPCH.h"
#include "ImGuiVulkanUtil.h"
#include "VulkanUtil.h"
#include "Log.h"
#include "ImGuiTheme.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

namespace Nyx
{
	namespace Renderer
	{
		ImGuiVulkanUtil::ImGuiVulkanUtil(GLFWwindow* window, VulkanUtil* vulkan)
			: Window(window)
			, Vulkan(vulkan)
		{
		}

		ImGuiVulkanUtil::~ImGuiVulkanUtil()
		{
			Shutdown();
		}

		void ImGuiVulkanUtil::Initialize(float width, float height)
		{
			if (bInitialized)
			{
				LOG_WARNING("ImGuiVulkanUtil is already initialized!");
				return;
			}

			// Initialize ImGui context
			IMGUI_CHECKVERSION();
			ImGui::CreateContext();

			// Configure ImGui
			ImGuiIO& io = ImGui::GetIO();
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
			io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
			io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

			// Set display size
			io.DisplaySize = ImVec2(width, height);
			io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

			// Set up style
			VulkanStyle = ImGui::GetStyle();
			VulkanStyle.Colors[ImGuiCol_TitleBg] = ImVec4(1.0f, 0.0f, 0.0f, 0.6f);
			VulkanStyle.Colors[ImGuiCol_TitleBgActive] = ImVec4(1.0f, 0.0f, 0.0f, 0.8f);
			VulkanStyle.Colors[ImGuiCol_MenuBarBg] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
			VulkanStyle.Colors[ImGuiCol_Header] = ImVec4(1.0f, 0.0f, 0.0f, 0.4);
			VulkanStyle.Colors[ImGuiCol_CheckMark] = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);

			// Apply default style
			SetStyle(4); // @todo: Expose to the user via Editor UI

			// Style
			ImGuiStyle& style = ImGui::GetStyle();
			style.WindowPadding = ImVec2(10.0f, 10.0f);
			style.FramePadding = ImVec2(8.0f, 6.0f);
			style.ItemSpacing = ImVec2(6.0f, 6.0f);
			style.ChildRounding = 6.0f;
			style.PopupRounding = 6.0f;
			style.FrameRounding = 6.0f;
			style.WindowTitleAlign = ImVec2(0.5f, 0.5f);

			// When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
			if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
			{
				style.WindowRounding = 0.0f;
				style.Colors[ImGuiCol_WindowBg].w = 1.0f;
			}

			CreateDescriptorPool();

			ImGui_ImplGlfw_InitForVulkan(Window, true);

			ImGui_ImplVulkan_InitInfo init_info{};
			init_info.Instance = *Vulkan->GetInstance();
			init_info.PhysicalDevice = *Vulkan->GetPhysicalDevice();
			init_info.Device = *Vulkan->GetDevice();
			init_info.QueueFamily = Vulkan->GetGraphicsQueueFamily();
			init_info.Queue = *Vulkan->GetGraphicsQueue();
			init_info.DescriptorPool = *DescriptorPool;
			init_info.MinImageCount = Vulkan->GetMinImageCount();
			init_info.ImageCount = Vulkan->GetSwapChainImageCount();
			init_info.PipelineCache = VK_NULL_HANDLE;

			init_info.UseDynamicRendering = false;
			init_info.PipelineInfoMain.RenderPass = *Vulkan->GetRenderPass();
			init_info.PipelineInfoMain.Subpass = 0;
			init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
			ImGui_ImplVulkan_Init(&init_info);

			bInitialized = true;
		}

		void ImGuiVulkanUtil::Shutdown()
		{
			if (!bInitialized)
			{
				LOG_WARNING("Attempted to shutdown an uninitialized ImGuiVulkanUtil!");
				return;
			}

			// Wait for device to finish operations before destroying resources
			if (Vulkan)
			{
				// 'waitIdle' only considered acceptable inside destructors
				Vulkan->GetDevice().waitIdle();
			}

			// vk resources are automatically cleaned up by their destructors

			// ImGui context is destroyed separately
			ImGui_ImplVulkan_Shutdown();
			ImGui_ImplGlfw_Shutdown();
			ImGui::DestroyContext();

			DescriptorPool = nullptr;
		}

		void ImGuiVulkanUtil::SetStyle(uint32_t index)
		{
			ImGuiStyle& style = ImGui::GetStyle();

			switch (index)
			{
			case 0:
				style = VulkanStyle;
				break;
			case 1:
				ImGui::StyleColorsClassic();
				break;
			case 2:
				ImGui::StyleColorsDark();
				break;
			case 3:
				ImGui::StyleColorsLight();
				break;
			case 4:
				SetNyxTheme();
				break;
			default:
				LOG_ERROR("Failed to set ImGuiStyle - No style associated with given index: {0}", index);
				break;
			}
		}

		void ImGuiVulkanUtil::BeginFrame()
		{
			ImGui_ImplVulkan_NewFrame();
			ImGui_ImplGlfw_NewFrame();

			ImGui::NewFrame();

			//// Create your UI elements here
			//// For example:
			//ImGui::Begin("Vulkan ImGui Demo");
			//ImGui::Text("Hello, Vulkan!");
			//if (ImGui::Button("Click me!")) {
			//	// Handle button click
			//}
			//ImGui::End();
		}

		void ImGuiVulkanUtil::DrawFrame(vk::raii::CommandBuffer & commandBuffer)
		{
			if (!bInitialized)
			{
				return;
			}

			ImGui::Render();
			ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), *commandBuffer);

			ImGuiIO& io = ImGui::GetIO();
			// Update and Render additional Platform Windows
			if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
			{
				ImGui::UpdatePlatformWindows();
				ImGui::RenderPlatformWindowsDefault();
			}
		}

		void ImGuiVulkanUtil::HandleKey(int key, int scancode, int action, int mods)
		{
			ImGuiIO& io = ImGui::GetIO();

			// This example uses GLFW key codes and actions, but you can adapt this
			// to work with any windowing library's input system

			// Map the platform-specific key action to ImGui's key state
			// In GLFW: GLFW_PRESS = 1, GLFW_RELEASE = 0
			const int KEY_PRESSED = 1;  // Generic key pressed value
			const int KEY_RELEASED = 0; // Generic key released value

			// @todo: LP: Is this casting safe? used to be io.KeyDown[key] = true/false
			if (action == KEY_PRESSED)
				io.AddKeyEvent(static_cast<ImGuiKey>(key), true);
			if (action == KEY_RELEASED)
				io.AddKeyEvent(static_cast<ImGuiKey>(key), false);

			//// Update modifier keys
			//// These key codes are GLFW-specific, but you would use your windowing library's
			//// equivalent key codes for other libraries
			//const int KEY_LEFT_CTRL = 341;   // GLFW_KEY_LEFT_CONTROL
			//const int KEY_RIGHT_CTRL = 345;  // GLFW_KEY_RIGHT_CONTROL
			//const int KEY_LEFT_SHIFT = 340;  // GLFW_KEY_LEFT_SHIFT
			//const int KEY_RIGHT_SHIFT = 344; // GLFW_KEY_RIGHT_SHIFT
			//const int KEY_LEFT_ALT = 342;    // GLFW_KEY_LEFT_ALT
			//const int KEY_RIGHT_ALT = 346;   // GLFW_KEY_RIGHT_ALT
			//const int KEY_LEFT_SUPER = 343;  // GLFW_KEY_LEFT_SUPER
			//const int KEY_RIGHT_SUPER = 347; // GLFW_KEY_RIGHT_SUPER

			// @todo: LP: was changing io.KeyDown[KEY_XXX_XXX] to ImGuiIs::KeyDown() correct?
			io.KeyCtrl = ImGui::IsKeyDown(ImGuiKey::ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey::ImGuiKey_RightCtrl);
			io.KeyShift = ImGui::IsKeyDown(ImGuiKey::ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey::ImGuiKey_RightShift);
			io.KeyAlt = ImGui::IsKeyDown(ImGuiKey::ImGuiKey_LeftAlt) || ImGui::IsKeyDown(ImGuiKey::ImGuiKey_RightAlt);
			io.KeySuper = ImGui::IsKeyDown(ImGuiKey::ImGuiKey_LeftSuper) || ImGui::IsKeyDown(ImGuiKey::ImGuiKey_RightSuper);
		}

		bool ImGuiVulkanUtil::GetWantKeyCapture()
		{
			return ImGui::GetIO().WantCaptureKeyboard;
		}

		void ImGuiVulkanUtil::CharPressed(uint32_t key)
		{
			ImGuiIO& io = ImGui::GetIO();
			io.AddInputCharacter(key);
		}

		void ImGuiVulkanUtil::CreateDescriptorPool()
		{
			vk::DescriptorPoolSize poolSizes[] =
			{
				{ vk::DescriptorType::eSampler, 1000 },
				{ vk::DescriptorType::eCombinedImageSampler, 1000 },
				{ vk::DescriptorType::eSampledImage, 1000 },
				{ vk::DescriptorType::eStorageImage, 1000 },
				{ vk::DescriptorType::eUniformTexelBuffer, 1000 },
				{ vk::DescriptorType::eStorageTexelBuffer, 1000 },
				{ vk::DescriptorType::eUniformBuffer, 1000 },
				{ vk::DescriptorType::eStorageBuffer, 1000 },
				{ vk::DescriptorType::eUniformBufferDynamic, 1000 },
				{ vk::DescriptorType::eStorageBufferDynamic, 1000 },
				{ vk::DescriptorType::eInputAttachment, 1000 }
			};

			vk::DescriptorPoolCreateInfo poolInfo{};
			poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
			poolInfo.maxSets = 1000 * static_cast<uint32_t>(std::size(poolSizes));
			poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
			poolInfo.pPoolSizes = poolSizes;

			DescriptorPool = Vulkan->GetDevice().createDescriptorPool(poolInfo);
		}
	}

} // Engine