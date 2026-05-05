#pragma once
#include "Renderer.h"
#include <vulkan/vulkan_raii.hpp>
#include "VulkanContext.h"
#include "VulkanSwapchain.h"
#include "OffscreenRenderTarget.h"
#include "VulkanViewportTarget.h"
#include "Mesh.h"
#include "Texture.h"
#include "CubemapTexture.h"
#include "Material.h"
#include "GltfImporter.h"
#include <imgui.h>
#include <memory>

#include "Entity.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Nyx
{
	class VulkanImGuiBackend;

	struct SceneUBO
	{
		glm::mat4 ViewProj;
		glm::mat4 InvViewProj;

		glm::vec2 ViewportSize;
		glm::vec2 Padding0;

		glm::vec4 CameraWorldPos;    // xyz used
		glm::vec4 LightDirectionWS;  // xyz used, normalized
		glm::vec4 LightColor;        // rgb = light color, a = ambient strength
	};

	struct ObjectPushConstants
	{
		glm::mat4 Model{ 1.0f };

		// x = base reflectivity
		// y = use texture? (1.0 = yes, 0.0 = no)
		// z,w = reserved
		glm::vec4 Params{ 0.0f, 1.0f, 0.0f, 0.0f };

		// rgb = per-object tint
		// a   = unused for now
		glm::vec4 Tint{ 1.0f, 1.0f, 1.0f, 1.0f };
	};

	struct Transform
	{
		glm::vec3 Position{ 0.0f, 0.0f, 0.0f };
		glm::vec3 RotationRadians{ 0.0f, 0.0f, 0.0f };
		glm::vec3 Scale{ 1.0f, 1.0f, 1.0f };

		glm::mat4 ToMatrix() const
		{
			glm::mat4 m = glm::translate(glm::mat4(1.0f), Position);
			m = glm::rotate(m, RotationRadians.x, glm::vec3(1.0f, 0.0f, 0.0f));
			m = glm::rotate(m, RotationRadians.y, glm::vec3(0.0f, 1.0f, 0.0f));
			m = glm::rotate(m, RotationRadians.z, glm::vec3(0.0f, 0.0f, 1.0f));
			m = glm::scale(m, Scale);
			return m;
		}
	};

	struct RenderObject
	{
		Nyx::Mesh* MeshAsset = nullptr;
		Material* MaterialAsset = nullptr;
		glm::mat4 WorldTransform{ 1.0f };
	};

	struct SkyboxUBO
	{
		glm::mat4 ViewProj = glm::mat4(1.0f);
	};

	struct SceneCamera
	{
		float AspectRatio = 16.0f / 9.0f;

		glm::vec3 Position = glm::vec3(0.0f, 3.0f, 6.0f);

		float YawRadians = -glm::half_pi<float>(); // looking along -Z
		float PitchRadians = glm::radians(-10.0f);

		glm::vec3 GetForwardVector() const
		{
			glm::vec3 forward;
			forward.x = cos(YawRadians) * cos(PitchRadians);
			forward.y = sin(PitchRadians);
			forward.z = sin(YawRadians) * cos(PitchRadians);
			return glm::normalize(forward);
		}

		glm::vec3 GetRightVector() const
		{
			return glm::normalize(glm::cross(GetForwardVector(), glm::vec3(0.0f, 1.0f, 0.0f)));
		}

		glm::vec3 GetUpVector() const
		{
			return glm::normalize(glm::cross(GetRightVector(), GetForwardVector()));
		}

		glm::mat4 GetViewMatrix() const
		{
			return glm::lookAt(
				Position,
				Position + GetForwardVector(),
				glm::vec3(0.0f, 1.0f, 0.0f)
			);
		}

		glm::mat4 GetProjectionMatrix() const
		{
			glm::mat4 proj = glm::perspective(
				glm::radians(60.0f),
				AspectRatio,
				0.1f,
				100.0f
			);

			// Vulkan clip-space convention with GLM
			proj[1][1] *= -1.0f;
			return proj;
		}

		glm::mat4 GetViewProjectionMatrix() const
		{
			return GetProjectionMatrix() * GetViewMatrix();
		}
	};

	struct ShaderHotReloadState
	{
		std::filesystem::path VertSourcePath;
		std::filesystem::path FragSourcePath;
		std::filesystem::path VertSpvPath;
		std::filesystem::path FragSpvPath;

		std::filesystem::file_time_type LastVertWriteTime{};
		std::filesystem::file_time_type LastFragWriteTime{};

		bool bReloadPending = false;
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
		virtual void OnMouseWheelScrolled(double yOffset);
		virtual void SetSceneWindowHovered(bool hovered);
		
		virtual void WaitIdle();
	public:
		VulkanContext& GetContext();
		VulkanSwapchain& GetSwapchain();

		virtual ImTextureID GetSceneTextureId() const;
		virtual Extent2D GetSceneViewportExtent() const;
		virtual void EnsureSceneViewportSize(uint32_t width, uint32_t height);

		virtual void SetSceneViewportSize(uint32_t width, uint32_t height);
		virtual bool WasSceneViewportRecreatedThisFrame() const;

		void TickCameraFromInput(float deltaTime);
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

		void CreateMaterials();
		void CreateRenderObjects();
		void UpdateRenderObjects(float deltaTime);
		void ExtractRenderObjects(const Nyx::Engine::Registry& registry);
		void DrawRenderObjects(vk::raii::CommandBuffer& cmd);

		void CreateScenePipeline();
		void CreateGridPipeline();

		void CreateSkyboxResources();
		void CreateSkyboxUniformBuffer();
		void CreateSkyboxDescriptorSetLayout();
		void CreateSkyboxDescriptorPool();
		void AllocateSkyboxDescriptorSets();
		void UpdateSkyboxDescriptorSet();
		void UpdateSkyboxUniforms(float deltaTime);
		void CreateSkyboxPipeline();
		void DrawSkybox(vk::raii::CommandBuffer& cmd);

		vk::raii::ShaderModule CreateShaderModule(const std::vector<uint32_t>& spirv);
		std::vector<uint32_t> ReadSpirvFile(const std::string& path);

		void CreateSceneDescriptors();
		void UpdateSceneDescriptorSets();
		void CreateSceneUniformBuffer();
		void UpdateSceneUniforms(float deltaTime);

		void CreateTestTextureData();
		void CreateTestMeshData();
		void CreateTestMeshBuffers();
		
		void CreateBuffer(
			vk::DeviceSize size,
			vk::BufferUsageFlags usage,
			vk::MemoryPropertyFlags properties,
			vk::raii::Buffer& outBuffer,
			vk::raii::DeviceMemory& outMemory);

		uint32_t FindMemoryType(vk::PhysicalDevice physicalDevice, uint32_t typeFilter, vk::MemoryPropertyFlags properties);

		float ComputeDeltaTime();

		bool RecompileGridShaders();
		void PollGridShaderHotReload();

		void LoadTestGltfScene();

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

		std::vector<std::unique_ptr<Nyx::Mesh>> LoadedMeshes;
		std::vector<std::unique_ptr<Nyx::Texture>> LoadedTextures;

		// Scene
		vk::raii::PipelineLayout ScenePipelineLayout{ nullptr };
		vk::raii::Pipeline ScenePipeline{ nullptr };

		vk::raii::DescriptorSetLayout SceneDescriptorSetLayout{ nullptr };
		vk::raii::DescriptorPool SceneDescriptorPool{ nullptr };
		vk::raii::DescriptorSets SceneDescriptorSets{ nullptr };

		vk::raii::Buffer SceneUniformBuffer{ nullptr };
		vk::raii::DeviceMemory SceneUniformBufferMemory{ nullptr };

		Material TexturedMaterial;
		Material ReflectiveMaterial;
		Material UntexturedMaterial;

		// @todo: Move this world out of the renderer, into the game/editor-layer
		Nyx::Engine::Registry World;

		std::vector<RenderObject> RenderObjects;
		float SceneTime = 0.0f;

		// Grid
		vk::raii::PipelineLayout GridPipelineLayout{ nullptr };
		vk::raii::Pipeline GridPipeline{ nullptr };

		ShaderHotReloadState GridShaderHotReload;

		// Skybox
		CubemapTexture SkyboxCubemap{ "Skybox01" };

		vk::raii::Buffer SkyboxUniformBuffer{ nullptr };
		vk::raii::DeviceMemory SkyboxUniformBufferMemory{ nullptr };

		vk::raii::DescriptorSetLayout SkyboxDescriptorSetLayout{ nullptr };
		vk::raii::DescriptorPool SkyboxDescriptorPool{ nullptr };
		vk::raii::DescriptorSets SkyboxDescriptorSets{ nullptr };

		vk::raii::PipelineLayout SkyboxPipelineLayout{ nullptr };
		vk::raii::Pipeline SkyboxPipeline{ nullptr };

		// Mesh
		Mesh CubeMesh = Mesh("Cube");

		std::vector<Vertex> MeshVertices;
		std::vector<uint32_t> MeshIndices;

		vk::raii::Buffer MeshVertexBuffer{ nullptr };
		vk::raii::DeviceMemory MeshVertexBufferMemory{ nullptr };

		vk::raii::Buffer MeshIndexBuffer{ nullptr };
		vk::raii::DeviceMemory MeshIndexBufferMemory{ nullptr };

		// Texture
		Texture TestTexture{ "checker" };

		// Input
		bool bMouseLookActive = false;
		bool bFirstMouseLookSample = true;
		double LastMouseX = 0.0;
		double LastMouseY = 0.0;

		// Camera
		float CameraMoveSpeed = 5.0f;
		float CameraMouseSensitivity = 0.003f; // radians per pixel
		float CameraSpeedMin = 0.25f;
		float CameraSpeedMax = 100.0f;
		float CameraSpeedStep = 1.2f;

		bool bSceneViewportHovered = false;

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