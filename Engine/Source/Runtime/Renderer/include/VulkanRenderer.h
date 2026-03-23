#pragma once
#include "Renderer.h"
#include <vulkan/vulkan_raii.hpp>
#include "VulkanContext.h"
#include "VulkanSwapchain.h"
#include "OffscreenRenderTarget.h"
#include "VulkanViewportTarget.h"
#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>

namespace Nyx
{
	class VulkanImGuiBackend;

	struct SceneUBO
	{
		glm::mat4 ViewProj{ 1.0f };
		glm::mat4 Model{ 1.0f };
	};

	struct SceneCamera
	{
		float OrthoHalfHeight = 5.0f;
		float AspectRatio = 16.0f / 9.0f;

		glm::vec3 Position = glm::vec3(0.0f, 0.0f, 1.0f);
		float RotationRadians = 0.0f;

		glm::mat4 GetViewMatrix() const
		{
			glm::mat4 transform =
				glm::translate(glm::mat4(1.0f), Position) *
				glm::rotate(glm::mat4(1.0f), RotationRadians, glm::vec3(0.0f, 0.0f, 1.0f));

			return glm::inverse(transform);
		}

		glm::mat4 GetProjectionMatrix() const
		{
			const float halfWidth = OrthoHalfHeight * AspectRatio;

			return glm::ortho(
				-halfWidth, halfWidth,
				-OrthoHalfHeight, OrthoHalfHeight,
				-10.0f, 10.0f
			);
		}

		glm::mat4 GetViewProjectionMatrix() const
		{
			return GetProjectionMatrix() * GetViewMatrix();
		}
	};
}

namespace Nyx
{
	class VulkanRenderer : public IRenderer
	{
	public:
		VulkanRenderer();
		virtual ~VulkanRenderer() = default;

		virtual void Initialize(const char* applicationName, GLFWwindow* window);
		virtual void Shutdown();

		virtual void BeginFrame();
		virtual void EndFrame();

		virtual void OnFramebufferResized();

		virtual void DrawFrame(const std::function<void()>& buildUI);
		
		virtual void WaitIdle();
	public:
		VulkanContext& GetContext();
		VulkanSwapchain& GetSwapchain();

		virtual ImTextureID GetSceneTextureId() const;
		virtual Extent2D GetSceneViewportExtent() const;
		virtual void EnsureSceneViewportSize(uint32_t width, uint32_t height);

		virtual void SetSceneViewportSize(uint32_t width, uint32_t height);
		virtual bool WasSceneViewportRecreatedThisFrame() const;

		void TickCameraFromInput();
	private:
		void SetupVulkan(const char* applicationName, GLFWwindow* window);
		void SetupImGui();

		void CreateGraphicsPipeline();
		void CreateCommandPool();
		void CreateCommandBuffers();
		void CreateSyncObjects();

	private:
		void RecreateSwapChain();
		void WaitForValidFramebufferSize();

		void CreateScenePipeline();
		vk::raii::ShaderModule CreateShaderModule(const std::vector<uint32_t>& spirv);
		std::vector<uint32_t> ReadSpirvFile(const std::string& path);

		void CreateSceneDescriptors();
		void CreateSceneUniformBuffer();
		void UpdateSceneUniforms();

		uint32_t FindMemoryType(vk::PhysicalDevice physicalDevice, uint32_t typeFilter, vk::MemoryPropertyFlags properties);

	private:
		GLFWwindow* Window = nullptr;
		std::unique_ptr<VulkanImGuiBackend> ImGuiBackend;

		VulkanContext Context;
		VulkanSwapchain Swapchain;

		SceneCamera Camera;
		VulkanViewportTarget SceneViewport;
		uint32_t PendingSceneViewportWidth = 1280;
		uint32_t PendingSceneViewportHeight = 720;
		bool bSceneViewportResizePending = false;

		vk::raii::PipelineLayout ScenePipelineLayout{ nullptr };
		vk::raii::Pipeline ScenePipeline{ nullptr };

		vk::raii::DescriptorSetLayout SceneDescriptorSetLayout{ nullptr };
		vk::raii::DescriptorPool SceneDescriptorPool{ nullptr };
		vk::raii::DescriptorSets SceneDescriptorSets{ nullptr };

		vk::raii::Buffer SceneUniformBuffer{ nullptr };
		vk::raii::DeviceMemory SceneUniformBufferMemory{ nullptr };


		//vk::PhysicalDeviceFeatures DeviceFeatures;

		vk::raii::CommandPool CommandPool{ nullptr };
		std::vector<vk::raii::CommandBuffer> CommandBuffers;

		vk::raii::Semaphore ImageAvailableSemaphore{ nullptr };
		std::vector<vk::raii::Semaphore> RenderFinishedSemaphores;
		vk::raii::Fence InFlightFence{ nullptr };

		bool bRecreateSwapChain = false;
		bool bSceneViewportRecreatedThisFrame = false;
	};
}