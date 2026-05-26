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
#include "EditorCamera.h"
#include "SceneViewTypes.h"
#include "SceneView.h"

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
		Nyx::Engine::Entity SourceEntity{};

		Nyx::Mesh* MeshAsset = nullptr;
		Material* MaterialAsset = nullptr;
		glm::mat4 WorldTransform{ 1.0f };

		// Will be filled with mesh-derived bounds
		glm::vec3 LocalBoundsMin{ -0.5f, -0.5f, -0.5f };
		glm::vec3 LocalBoundsMax{ 0.5f,  0.5f,  0.5f };

		uint32_t PickingId = 0;
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

	struct DebugLineVertex
	{
		glm::vec3 Position{ 0.0f };
		glm::vec3 Color{ 1.0f };

		static vk::VertexInputBindingDescription GetBindingDescription()
		{
			vk::VertexInputBindingDescription binding{};
			binding.binding = 0;
			binding.stride = sizeof(DebugLineVertex);
			binding.inputRate = vk::VertexInputRate::eVertex;
			return binding;
		}

		static std::array<vk::VertexInputAttributeDescription, 2> GetAttributeDescriptions()
		{
			std::array<vk::VertexInputAttributeDescription, 2> attributes{};

			attributes[0].binding = 0;
			attributes[0].location = 0;
			attributes[0].format = vk::Format::eR32G32B32Sfloat;
			attributes[0].offset = offsetof(DebugLineVertex, Position);

			attributes[1].binding = 0;
			attributes[1].location = 1;
			attributes[1].format = vk::Format::eR32G32B32Sfloat;
			attributes[1].offset = offsetof(DebugLineVertex, Color);

			return attributes;
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
		virtual void OnMouseWheelScrolled(double yOffset);
		
		void SetSelectedEntity(std::optional<Nyx::Engine::Entity> entity) override;

		virtual void WaitIdle();

		virtual void SetSceneViewCameraMode(uint64_t id, EViewportCameraMode mode);
		virtual void SetSceneViewEditorCameraTransform(uint64_t id, const glm::vec3& pos, const glm::vec3& rot);
	
		void SetWorld(const Nyx::Engine::Registry* world) override;

		Nyx::Mesh* GetCubeMesh() override;
		Nyx::Material* GetTexturedMaterial() override;
		Nyx::Material* GetReflectiveMaterial() override;
		Nyx::Material* GetUntexturedMaterial() override;
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
		void ExtractRenderObjects(const Nyx::Engine::Registry& registry);
		void DrawRenderObjects(SceneViewInstance& view, vk::raii::CommandBuffer& cmd);

		void CreateSceneDescriptorSetLayout();
		void CreateScenePipeline();
		void CreateGridPipeline();
		void CreatePickingPipeline();

		vk::RenderPass GetSceneRenderPass() const;

		void LoadSkyboxCubemap();
		void CreateSkyboxSharedResources();
		void CreateSkyboxResourcesForView(SceneViewInstance& view);

		void CreatePickingResourcesForView(SceneViewInstance& view);
		void DestroyPickingResourcesForView(SceneViewInstance& view);
		void RecreatePickingResourcesForView(SceneViewInstance& view);

		void CreateSelectionMaskResourcesForView(SceneViewInstance& view);
		void DestroySelectionMaskResourcesForView(SceneViewInstance& view);
		void RecreateSelectionMaskResourcesForView(SceneViewInstance& view);

		void CreateOutlineDescriptorSetLayout();
		void CreateOutlineDescriptorSetForView(SceneViewInstance& view);

		struct SelectionMaskPushConstants
		{
			glm::mat4 Model{ 1.0f };
			float SelectedValue = 0.0f;
		};

		void CreateSelectionMaskPipeline();
		void DrawSelectionMaskPass(SceneViewInstance& view, vk::raii::CommandBuffer& cmd);

		struct OutlineCompositePushConstants
		{
			glm::vec2 TexelSize{ 0.0f };
			float Thickness = 1.0f;
			float Padding = 0.0f;
			glm::vec4 OutlineColor{ 1.0f, 0.6f, 0.1f, 1.0f };
		};

		void CreateOutlineCompositePipeline();
		void DrawSelectionOutline(SceneViewInstance& view, vk::raii::CommandBuffer& cmd);


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

		void CreateImage(
			uint32_t width,
			uint32_t height,
			vk::Format format,
			vk::ImageUsageFlags usage,
			vk::ImageAspectFlags aspectFlags,
			vk::raii::Image& outImage,
			vk::raii::DeviceMemory& outMemory,
			vk::raii::ImageView& outImageView);

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

		void CreateDebugLineResources();
		void CreateDebugLinePipeline();

		void ResetDebugLines();
		void AddDebugLine(const glm::vec3& a, const glm::vec3& b, const glm::vec3& color);
		void AddDebugAxes(const glm::mat4& worldTransform, float axisLength);
		void AddDebugAABB(const glm::vec3& min, const glm::vec3& max, const glm::vec3& color);
		void AddDebugOrientedBox(
			const glm::mat4& worldTransform,
			const glm::vec3& localMin,
			const glm::vec3& localMax,
			const glm::vec3& color);

		void UploadDebugLines();
		void DrawDebugLines(SceneViewInstance& view, vk::raii::CommandBuffer& cmd);
		void BuildDebugDrawData();

		void RequestPick(uint64_t sceneViewId, uint32_t pixelX, uint32_t pixelY);
		Nyx::IRenderer::PickResult ConsumeLastPickResult(uint64_t sceneViewId);

		void DrawPickingPass(SceneViewInstance& view, vk::raii::CommandBuffer& cmd);
		void ResolvePickRequest(SceneViewInstance& view, vk::raii::CommandBuffer& cmd);
		void ReadBackPickResults();

	private:
		struct PickingPushConstants
		{
			glm::mat4 Model{ 1.0f };
			uint32_t EncodedEntityId = 0;
		};

	private:

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

		// World is owned by the editor / scene document.
		const Nyx::Engine::Registry* World = nullptr;

		std::vector<RenderObject> RenderObjects;
		float SceneTime = 0.0f;

		// Picking
		vk::raii::PipelineLayout PickingPipelineLayout{ nullptr };
		vk::raii::Pipeline PickingPipeline{ nullptr };

		std::vector<Nyx::Engine::Entity> PickingIdToEntity;

		// Selection
		vk::raii::PipelineLayout SelectionMaskPipelineLayout{ nullptr };
		vk::raii::Pipeline SelectionMaskPipeline{ nullptr };

		std::optional<Nyx::Engine::Entity> SelectedEntity;

		vk::raii::DescriptorSetLayout OutlineDescriptorSetLayout{ nullptr };
		vk::raii::Sampler OutlineCompositeSampler{ nullptr };

		vk::raii::PipelineLayout OutlineCompositePipelineLayout{ nullptr };
		vk::raii::Pipeline OutlineCompositePipeline{ nullptr };

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

		// Debugging
		static constexpr uint32_t MaxDebugLineVertices = 65536;

		std::vector<DebugLineVertex> DebugLineVertices;

		vk::raii::Buffer DebugLineVertexBuffer{ nullptr };
		vk::raii::DeviceMemory DebugLineVertexBufferMemory{ nullptr };

		vk::raii::PipelineLayout DebugLinePipelineLayout{ nullptr };
		vk::raii::Pipeline DebugLinePipeline{ nullptr };
	};
}