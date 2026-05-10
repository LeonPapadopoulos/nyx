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
#include "TransformComponent.h"
#include "MeshRendererComponent.h"
#include "CameraComponent.h"
#include "DirectionalLightComponent.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Nyx
{
	class VulkanImGuiBackend;

	enum class EViewportCameraMode : uint8_t
	{
		EditorFreeCamera,
		ScenePrimaryCamera
	};

	struct EditorCamera
	{
		float AspectRatio = 16.0f / 9.0f;

		glm::vec3 Position = glm::vec3(0.0f, 2.0f, 6.0f);
		glm::vec3 RotationRadians = glm::vec3(0.0f);

		glm::mat4 GetViewMatrix() const
		{
			glm::mat4 transform =
				glm::translate(glm::mat4(1.0f), Position) *
				glm::rotate(glm::mat4(1.0f), RotationRadians.y, glm::vec3(0.0f, 1.0f, 0.0f)) *
				glm::rotate(glm::mat4(1.0f), RotationRadians.x, glm::vec3(1.0f, 0.0f, 0.0f)) *
				glm::rotate(glm::mat4(1.0f), RotationRadians.z, glm::vec3(0.0f, 0.0f, 1.0f));

			return glm::inverse(transform);
		}

		glm::mat4 GetProjectionMatrix() const
		{
			glm::mat4 proj = glm::perspective(
				glm::radians(60.0f),
				AspectRatio,
				0.1f,
				1000.0f
			);

			proj[1][1] *= -1.0f;
			return proj;
		}

		glm::mat4 GetViewProjectionMatrix() const
		{
			return GetProjectionMatrix() * GetViewMatrix();
		}
	};

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

	struct ExtractedSceneGlobals
	{
		glm::mat4 View{ 1.0f };
		glm::mat4 Projection{ 1.0f };
		glm::mat4 ViewProjection{ 1.0f };

		glm::vec3 CameraWorldPos{ 0.0f, 0.0f, 3.0f };
		glm::vec3 LightDirectionWS{ glm::normalize(glm::vec3(0.4f, 1.0f, 0.2f)) };
		glm::vec3 LightColor{ 1.0f, 0.98f, 0.95f };
		float Ambient = 0.18f;

		bool bHasCamera = false;
		bool bHasDirectionalLight = false;
	};

	struct SceneViewInstance
	{
		uint64_t Id = 0;

		VulkanViewportTarget RenderTarget;

		EViewportCameraMode CameraMode = EViewportCameraMode::EditorFreeCamera;
		EditorCamera EditorCam;
		ExtractedSceneGlobals SceneGlobals;

		vk::raii::Buffer SceneUniformBuffer{ nullptr };
		vk::raii::DeviceMemory SceneUniformBufferMemory{ nullptr };
		vk::raii::DescriptorPool SceneDescriptorPool{ nullptr };
		vk::raii::DescriptorSets SceneDescriptorSets{ nullptr };

		vk::raii::Buffer SkyboxUniformBuffer{ nullptr };
		vk::raii::DeviceMemory SkyboxUniformBufferMemory{ nullptr };
		vk::raii::DescriptorPool SkyboxDescriptorPool{ nullptr };
		vk::raii::DescriptorSets SkyboxDescriptorSets{ nullptr };

		bool bHovered = false;
		bool bFocused = false;

		bool bResizePending = false;
		uint32_t PendingWidth = 1;
		uint32_t PendingHeight = 1;

		bool bRecreatedThisFrame = false;
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
		
		virtual void WaitIdle();
	public:
		VulkanContext& GetContext();
		VulkanSwapchain& GetSwapchain();

		ImTextureID GetSceneViewTextureId(uint64_t id) const override;
		Extent2D GetSceneViewExtent(uint64_t id) const;

		SceneViewInstance* FindEditorInputTargetView();
		void TickActiveEditorSceneViewFromInput(float deltaTime);
		void TickEditorCameraFromInput(SceneViewInstance& view, float deltaTime);
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
		void UpdateRenderObjects(Nyx::Engine::Registry& registry, float deltaTime);
		void ExtractRenderObjects(const Nyx::Engine::Registry& registry);
		void DrawRenderObjects(SceneViewInstance& view, vk::raii::CommandBuffer& cmd);

		void CreateSceneDescriptorSetLayout();
		void CreateScenePipeline();
		void CreateGridPipeline();

		vk::RenderPass GetSceneRenderPass() const;

		void LoadSkyboxCubemap();
		void CreateSkyboxSharedResources();
		void CreateSkyboxResourcesForView(SceneViewInstance& view);

		void CreateSkyboxUniformBuffer(SceneViewInstance& view);
		void CreateSkyboxDescriptorSetLayout();
		void UpdateSkyboxUniforms(SceneViewInstance& view);
		void CreateSkyboxPipeline();
		void DrawSkybox(SceneViewInstance& view, vk::raii::CommandBuffer& cmd);

		void DrawGrid(SceneViewInstance& view, vk::raii::CommandBuffer& cmd);

		vk::raii::ShaderModule CreateShaderModule(const std::vector<uint32_t>& spirv);
		std::vector<uint32_t> ReadSpirvFile(const std::string& path);

		void CreateSceneResourcesForView(SceneViewInstance& view);
		void UpdateSceneUniforms(SceneViewInstance& view);

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

		void ExtractSceneGlobalsFromEditorCamera(SceneViewInstance& view);
		void ExtractSceneGlobalsFromWorldCamera(SceneViewInstance& view, const Nyx::Engine::Registry& registry);
		void UpdateViewportSceneGlobals(SceneViewInstance& view, const Nyx::Engine::Registry& registry);

		uint64_t CreateSceneView();
		void DestroySceneView(uint64_t id);

		void SetSceneViewHovered(uint64_t id, bool bHovered) override;
		void SetSceneViewFocused(uint64_t id, bool bFocused) override;
		void SetSceneViewSize(uint64_t id, uint32_t width, uint32_t height) override;

		bool WasSceneViewRecreatedThisFrame(uint64_t id) const override;

		SceneViewInstance* FindSceneView(uint64_t id);
		const SceneViewInstance* FindSceneView(uint64_t id) const;

		void EnsureSceneViewSize(SceneViewInstance& view, uint32_t width, uint32_t height);
		void UpdateSceneView(SceneViewInstance& view, const Nyx::Engine::Registry& world, float deltaTime);
		void RenderSceneView(SceneViewInstance& view, vk::raii::CommandBuffer& cmd);

		void SpawnTestEntities();

	private:
		GLFWwindow* Window = nullptr;
		std::unique_ptr<VulkanImGuiBackend> ImGuiBackend;

		VulkanContext Context;
		VulkanSwapchain Swapchain;

		std::vector<std::unique_ptr<Nyx::Mesh>> LoadedMeshes;
		std::vector<std::unique_ptr<Nyx::Texture>> LoadedTextures;
		
		std::vector<SceneViewInstance> SceneViews;
		uint64_t NextSceneViewId = 1;

		vk::raii::DescriptorSetLayout SceneDescriptorSetLayout{ nullptr };
		vk::raii::PipelineLayout ScenePipelineLayout{ nullptr };
		vk::raii::Pipeline ScenePipeline{ nullptr };

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


		vk::raii::DescriptorSetLayout SkyboxDescriptorSetLayout{ nullptr };
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
		uint64_t MouseLookLockedSceneViewId = 0;

		float CameraMoveSpeed = 5.0f;
		float CameraMouseSensitivity = 0.003f; // radians per pixel
		float CameraSpeedMin = 0.25f;
		float CameraSpeedMax = 100.0f;
		float CameraSpeedStep = 1.2f;

		bool bHaveLastMousePosition = false;

		//vk::PhysicalDeviceFeatures DeviceFeatures;

		vk::raii::CommandPool CommandPool{ nullptr };
		std::vector<vk::raii::CommandBuffer> CommandBuffers;

		vk::raii::Semaphore ImageAvailableSemaphore{ nullptr };
		std::vector<vk::raii::Semaphore> RenderFinishedSemaphores;
		vk::raii::Fence InFlightFence{ nullptr };

		bool bRecreateSwapChain = false;
	};
}