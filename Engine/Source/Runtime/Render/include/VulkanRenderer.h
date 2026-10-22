#pragma once
#include "EditorRenderer.h"
#include <vulkan/vulkan_raii.hpp>
#include "VulkanContext.h"
#include "VulkanSwapchain.h"
#include "OffscreenRenderTarget.h"
#include "VulkanViewportTarget.h"
#include "Paths.h"
#include "ShaderPrograms.h"
#include "VulkanBlur.h"
#include "VulkanBindless.h"
#include "VulkanMaterials.h"
#include "VulkanEnvironment.h"
#include "VulkanUIBackend.h"
#include "VulkanDebugLines.h"
#include "VulkanEditorOverlays.h"
#include "Mesh.h"
#include "Texture.h"
#include "CubemapTexture.h"
#include "Material.h"
#include "GltfImporter.h"
#include <imgui.h>
#include <chrono>
#include <map>
#include <memory>
#include <span>
#include <unordered_map>

#include "Entity.h"
#include "TransformComponent.h"
#include "MeshRendererComponent.h"
#include "CameraComponent.h"
#include "DirectionalLightComponent.h"
#include "EditorCamera.h"
#include "SceneViewTypes.h"
#include "SceneView.h"
#include "Extent2D.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Nyx::Tests
{
	struct SceneReplacementTestAccess;
}

namespace Nyx
{
	struct SceneUBO
	{
		glm::mat4 ViewProj;
		glm::mat4 InvViewProj;

		glm::vec2 ViewportSize;
		// The sky's turn about the up axis: its cosine and sine
		glm::vec2 SkyRotation{ 1.0f, 0.0f };

		glm::vec4 CameraWorldPos;   // xyz used
		glm::vec4 LightDirectionWS; // xyz used, normalized
		glm::vec4 LightColor;       // rgb = light color, a = ambient strength

		// The view's rotation without its position, for the sky
		glm::mat4 SkyboxViewProj{ 1.0f };
		// The sky's cube map in the bindless texture array
		uint32_t EnvironmentTexture = 0;
		// The view's scene in HDR, in the bindless texture array, for the tonemap
		uint32_t SceneTexture = 0;
		// What the scene's light is multiplied by before tonemapping
		float ExposureScale = 1.0f;
		// ESceneViewDebug
		uint32_t DebugView = 0;

		// The environment's light (VulkanEnvironment): the specular cube and its mips, the BRDF
		// table, the irradiance's nine coefficients
		uint32_t SpecularTexture = 0;
		uint32_t SpecularLevels = 0;
		uint32_t BrdfLookup = 0;
		uint32_t Padding2 = 0;
		glm::vec4 Irradiance[9] = {};
	};

	// The view's set (set 1) as nyx/view.slang reads it: std140 and scalar layout agree on it
	static_assert(sizeof(SceneUBO) == 432, "SceneUBO must match ViewConstants in nyx/view.slang");

	// One draw's push constants, the same for every scene pipeline: ObjectConstants in
	// nyx/bindless.slang
	struct ObjectConstants
	{
		glm::mat4 Model{ 1.0f };
		// Where the material's values start in the material buffer
		uint32_t MaterialOffset = 0;
		// The picking pass writes it
		uint32_t EntityId = 0;
		uint32_t Padding[2] = {};
		// Set per object by code; the selection mask passes keep the mask's value in x
		glm::vec4 CustomData{ 0.0f };
	};

	static_assert(sizeof(ObjectConstants) == 96, "ObjectConstants must match nyx/bindless.slang");

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
		Nyx::Entity SourceEntity{};

		Nyx::Mesh* MeshAsset = nullptr;
		Material* MaterialAsset = nullptr;
		glm::mat4 WorldTransform{ 1.0f };

		// Will be filled with mesh-derived bounds
		glm::vec3 LocalBoundsMin{ -0.5f, -0.5f, -0.5f };
		glm::vec3 LocalBoundsMax{ 0.5f, 0.5f, 0.5f };

		uint32_t PickingId = 0;
	};

	// A world that scene views show (IRenderer::SetWorld(), SetSceneViewWorld()), as this frame
	// draws it
	struct RenderWorld
	{
		const Nyx::Registry* Registry = nullptr;
		// New each time the world is set, also at the same address, and never the same for two
		// worlds; picks of another revision are dropped. 0 is none.
		uint64_t Revision = 0;

		std::vector<RenderObject> Objects;
		// Dense picking IDs: ID i is the entity at i - 1, 0 is nothing
		std::vector<Nyx::Entity> PickingIdToEntity;
		// This frame's debug lines of the world: the editor overlays' (its objects' bounds and axes)
		// and its debug drawing's (IRenderer::SetWorldDebugDraw())
		DebugLineRange OverlayLines;
		DebugLineRange DebugDrawLines;
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
				glm::vec3(0.0f, 1.0f, 0.0f));
		}

		glm::mat4 GetProjectionMatrix() const
		{
			glm::mat4 proj = glm::perspective(
				glm::radians(60.0f),
				AspectRatio,
				0.1f,
				100.0f);

			// Vulkan clip-space convention with GLM
			proj[1][1] *= -1.0f;
			return proj;
		}

		glm::mat4 GetViewProjectionMatrix() const
		{
			return GetProjectionMatrix() * GetViewMatrix();
		}
	};
}

namespace Nyx
{
	class VulkanRenderer : public IEditorRenderer
	{
		friend struct Nyx::Tests::SceneReplacementTestAccess;

	public:
		VulkanRenderer();
		virtual ~VulkanRenderer() = default;

		virtual void Initialize(const char* applicationName, GLFWwindow* window);
		virtual void Shutdown();

		virtual void OnFramebufferResized();
		void SetVSync(bool bVSync) override;
		bool IsVSync() const override;
		const char* GetApiName() const override { return "Vulkan"; }

		virtual void DrawFrame(const std::function<void()>& buildUI);

		void SetSelectedEntity(std::optional<Nyx::Entity> entity) override;

		virtual void WaitIdle();
		bool IsDeviceLost() const override;

		virtual void SetSceneViewCameraMode(uint64_t id, EViewportCameraMode mode);
		virtual void SetSceneViewEditorCameraTransform(uint64_t id, const glm::vec3& pos, const glm::vec3& rot);
		bool GetSceneViewEditorCameraTransform(uint64_t id, glm::vec3& outPos, glm::vec3& outRot) const override;
		void SetSceneViewShowEditorOverlays(uint64_t id, bool bShow) override;
		void SetSceneViewExposure(uint64_t id, float stops) override;
		void SetSceneViewSky(uint64_t id, ESceneViewSky sky, float rotationRadians) override;
		void SetSceneViewDebug(uint64_t id, ESceneViewDebug view) override;

		bool GetSceneViewCameraData(uint64_t sceneViewId, Nyx::SceneViewCameraData& outData) const override;

		void SetWorld(const Nyx::Registry* world) override;
		void SetSceneViewWorld(uint64_t id, const Nyx::Registry* world) override;
		void SetWorldDebugDraw(const Nyx::Registry* world, const Nyx::DebugDraw* drawing) override;
		void SetSceneViewShowDebugDrawing(uint64_t id, bool bShow) override;

		Nyx::Mesh* GetCubeMesh() override;
		Nyx::Mesh* GetSphereMesh() override;
		Nyx::Mesh* GetPlaneMesh() override;
		Nyx::Material* GetMaterial(const std::string& path) override;
		bool IsCompilingShaders() const override;
		const Nyx::ShaderLog& GetShaderLog() const override { return ShaderCompiles; }
		void SetShaderCompiledListener(ShaderCompiledListener listener) override { OnShaderCompiled = std::move(listener); }
		void FollowEditorShaders() override { Programs.FollowEditor(); }
		void ApplyCompiledShader(const Nyx::ShaderCompileRequest& request, const Nyx::ShaderCompileResult& result) override { Programs.Inject(request, result); }
		MaterialView ViewMaterial(const std::string& path) override;
		void SetMaterialValues(const std::string& path, const Nyx::MaterialAsset& asset) override;

	public:
		VulkanContext& GetContext();
		VulkanSwapchain& GetSwapchain();

		// The light of the sky, for tests
		Nyx::VulkanEnvironment& GetEnvironment() { return Environment; }
		Nyx::VulkanBindless& GetBindless() { return Bindless; }
		// The sky drawn behind the scene and lighting it: a cube of the bindless array, e.g. a
		// test's uniform one; its light is made again (waits for the GPU)
		void SetSky(uint32_t cubeSlot);
		uint32_t GetSky() const { return SkyboxTextureIndex; }

		// Draws 2D shapes recorded with a Painter into a VulkanPainterTarget
		Nyx::VulkanPainter& GetPainterPass() { return UI.GetPainterPass(); }

		// The UI's, from the UI backend
		uint64_t CreatePainterCanvas() override { return UI.CreatePainterCanvas(); }
		void DestroyPainterCanvas(uint64_t id) override { UI.DestroyPainterCanvas(id); }
		void SubmitPainterCanvas(uint64_t id, const Nyx::UI::Painter& painter, const Nyx::UI::PainterDrawOptions& options) override { UI.SubmitPainterCanvas(id, painter, options); }
		ImTextureID GetPainterCanvasTextureId(uint64_t id) const override { return UI.GetPainterCanvasTextureId(id); }
		std::optional<Nyx::UI::PainterTiles> GetPainterCanvasTiles(uint64_t id) const override { return UI.GetPainterCanvasTiles(id); }
		const Nyx::UI::PainterWindowStats& GetWindowPainterStats() const override { return UI.GetWindowPainterStats(); }

		uint64_t CreateUIImage(const Nyx::RgbaImage& image) override { return UI.CreateUIImage(image); }
		void DestroyUIImage(uint64_t id) override { UI.DestroyUIImage(id); }
		ImTextureID GetUIImageTextureId(uint64_t id) const override { return UI.GetUIImageTextureId(id); }

		void SaveScreenshot(const std::filesystem::path& path, ImVec2 min, ImVec2 max) override { UI.SaveScreenshot(path, min, max); }
		const std::string& GetScreenshotMessage() const override { return UI.GetScreenshotMessage(); }

		ImTextureID GetSceneViewTextureId(uint64_t id) const override;
		// What a scene view rendered last, as RGBA; waits for the GPU. False for an unknown view. For tests.
		bool ReadSceneViewPixels(uint64_t id, Nyx::RgbaImage& outImage);
		Extent2D GetSceneViewExtent(uint64_t id) const override;


	private:
		void SetupVulkan(const char* applicationName, GLFWwindow* window);

		void CreateGraphicsPipeline();
		void CreateCommandPool();
		void CreateCommandBuffers();
		void CreateSyncObjects();

	private:
		void RecreateSwapChain();
		// False while the window is minimized
		bool HasValidFramebufferSize() const;

		// The world's state, once per frame: a new revision when it is set
		void BeginWorldRevision(const Nyx::Registry* registry);
		RenderWorld* FindRenderWorld(const Nyx::Registry* registry);
		const RenderWorld* FindRenderWorld(const Nyx::Registry* registry) const;
		// The world the view shows; null without one
		const RenderWorld* FindShownWorld(const SceneViewInstance& view) const;
		// The revision of the world the view shows; 0 without one
		uint64_t GetShownWorldRevision(const SceneViewInstance& view) const;
		// Every shown world's objects and debug lines; forgets worlds no view shows any more
		void ExtractRenderWorlds();
		void ExtractRenderObjects(RenderWorld& world);
		void DrawRenderObjects(SceneViewInstance& view, const RenderWorld& world, vk::raii::CommandBuffer& cmd);

		// Set 1, the view's: its SceneUBO
		void CreateSceneDescriptorSetLayout();
		// The layout of every scene pipeline: the global set (Bindless), the view's set, and
		// ObjectConstants
		void CreateScenePipelineLayout();
		// Binds both sets for the scene pipelines' draws into the view
		void BindSceneSets(SceneViewInstance& view, vk::raii::CommandBuffer& cmd);

		// The scene views' scene pass (HDR), for what draws the scene: materials, the sky
		vk::RenderPass GetSceneRenderPass() const;
		// Their picture pass (8-bit, after the tonemap), for what draws over the picture
		vk::RenderPass GetPictureRenderPass() const;
		// The view's HDR picture into its bindless slot, after the view's targets were made
		void RegisterHdrPicture(SceneViewInstance& view);
		void CreateTonemapPipeline();
		void DrawTonemap(SceneViewInstance& view, vk::raii::CommandBuffer& cmd);

		void LoadSkyboxCubemap();

		void CreateSkyboxPipeline();
		void DrawSkybox(SceneViewInstance& view, vk::raii::CommandBuffer& cmd);

		// A shader's SPIR-V by file name (e.g. "Grid.vert.spv"), from Programs: the last good
		// compile, at first the build's
		std::vector<uint32_t> ReadSpirvFile(const std::string& fileName);

		void CreateSceneResourcesForView(SceneViewInstance& view);
		void UpdateSceneUniforms(SceneViewInstance& view);

		void CreateTestMeshData();

		// Live reload of the engine's shaders: turned on where the shader sources and
		// NyxShaderCompiler.exe are found
		void StartLiveShaderReload();
		// Once per frame, before recording: logs what compiled, and makes the pipelines of changed
		// shaders again
		void ApplyShaderUpdates();
		// One compile into the Shader Log (ShaderCompiles)
		void RecordCompile(const Nyx::ShaderProgramUpdate& update);

		// The cube of a view's sky, with its light, made at the first ask
		uint32_t GetSkySlot(ESceneViewSky sky);

		void ExtractSceneGlobalsFromEditorCamera(SceneViewInstance& view);
		void ExtractSceneGlobalsFromWorldCamera(SceneViewInstance& view, const Nyx::Registry& registry);
		void UpdateViewportSceneGlobals(SceneViewInstance& view, const Nyx::Registry& registry);

		// While a frame is recorded (e.g. as the UI is built), a view made shows from its first
		// drawn frame on (WasSceneViewRecreatedThisFrame() says so), and a view let go of is
		// destroyed at the next frame's start: the frame's commands may draw it already, and
		// destroying what they use while they are recorded would break them.
		uint64_t CreateSceneView();
		void DestroySceneView(uint64_t id);
		void DestroySceneViewNow(uint64_t id);

		void SetSceneViewSize(uint64_t id, uint32_t width, uint32_t height) override;

		bool WasSceneViewRecreatedThisFrame(uint64_t id) const override;

		ImTextureID GetSceneViewBlurTextureId(uint64_t id) const override;
		Extent2D GetSceneViewBlurExtent(uint64_t id) const override;
		float GetBlurMilliseconds() const override { return UI.GetBlurMilliseconds(); }
		std::vector<uint64_t> GetSceneViewIds() const override;

		SceneViewInstance* FindSceneView(uint64_t id);
		const SceneViewInstance* FindSceneView(uint64_t id) const;

		void EnsureSceneViewSize(SceneViewInstance& view, uint32_t width, uint32_t height);
		void RenderSceneView(SceneViewInstance& view, vk::raii::CommandBuffer& cmd);

		void RequestPick(uint64_t sceneViewId, uint32_t pixelX, uint32_t pixelY);
		Nyx::IEditorRenderer::PickResult ConsumeLastPickResult(uint64_t sceneViewId);

		// After the last frame's fence: every view's pick that frame copied
		void ReadBackPickResults();

	private:
	private:
		GLFWwindow* Window = nullptr;

		VulkanContext Context;
		VulkanSwapchain Swapchain;

		// The engine's shaders by name; every pipeline is made from them. What live reload compiles
		// is kept in Saved/ShaderCache/Engine, so the next start begins from it.
		Nyx::ShaderPrograms Programs{ Nyx::Paths::GetCompiledShadersDir(), Nyx::Paths::GetShadersDir(), Nyx::Paths::GetShaderCacheDir() / "Engine" };

		// ImGui, the painter, glass and blur, UI images, screenshots, and the frame's GPU times
		VulkanUIBackend UI{ Context, Swapchain, Programs };
		// Every view's debug lines, the worlds' debug drawing and the overlays' alike
		VulkanDebugLines DebugLines{ Context, Programs };
		// The grid, debug lines, picking and the selection outline, of the views that show them
		VulkanEditorOverlays Overlays{ Context, Programs, DebugLines };

		// When the last frame's DrawFrame() ended, for the time spent outside it
		std::chrono::steady_clock::time_point LastFrameEnd;

		std::vector<std::unique_ptr<Nyx::Mesh>> LoadedMeshes;
		std::vector<std::unique_ptr<Nyx::Texture>> LoadedTextures;

		std::vector<SceneViewInstance> SceneViews;
		// Views let go of while a frame was recorded, destroyed at the next frame's start
		std::vector<uint64_t> SceneViewsToDestroy;
		// Between waiting for the last frame and the end of DrawFrame(): the frame's commands are recorded
		bool bRecordingFrame = false;
		uint64_t NextSceneViewId = 1;

		// Set 0 of every scene pipeline: every texture, the fixed samplers, the material buffer
		Nyx::VulkanBindless Bindless;

		vk::raii::DescriptorSetLayout SceneDescriptorSetLayout{ nullptr };
		// Shared by the scene, sky, grid, debug line, picking and selection mask pipelines
		vk::raii::PipelineLayout ScenePipelineLayout{ nullptr };

		// The meshes' materials, from their .nyxmat files
		Nyx::VulkanMaterials Materials;
		// Every shader's last compile (GetShaderLog())
		Nyx::ShaderLog ShaderCompiles;
		ShaderCompiledListener OnShaderCompiled;
		// Values set for materials (SetMaterialValues()), written once the UI is built: the frame's
		// draws are recorded by then, but run after
		std::map<std::string, Nyx::MaterialAsset> PendingMaterialValues;
		// The sky's light, for physically based materials
		Nyx::VulkanEnvironment Environment;

		// The renderer's world (SetWorld()), owned by the editor / scene document
		const Nyx::Registry* World = nullptr;
		// Every world a scene view shows, the renderer's world among them; owned here so that
		// their addresses stay while worlds come and go
		std::vector<std::unique_ptr<RenderWorld>> RenderWorlds;
		uint64_t LastWorldRevision = 0;
		// The worlds' debug drawing (SetWorldDebugDraw()), by world; kept apart from RenderWorlds,
		// whose entries come and go with the views
		std::unordered_map<const Nyx::Registry*, const Nyx::DebugDraw*> WorldDebugDraws;

		float SceneTime = 0.0f;

		// HDR to the picture
		vk::raii::Pipeline TonemapPipeline{ nullptr };

		// Skybox
		CubemapTexture SkyboxCubemap{ "Skybox01" };
		uint32_t SkyboxTextureIndex = 0;
		// The studio's and black's skies, for views that show them (SetSceneViewSky()); 0 until then
		uint32_t StudioSkySlot = 0;
		uint32_t BlackSkySlot = 0;

		vk::raii::Pipeline SkyboxPipeline{ nullptr };

		// Mesh
		Mesh CubeMesh = Mesh("Cube");
		Mesh SphereMesh = Mesh("Sphere");
		Mesh PlaneMesh = Mesh("Plane");

		std::vector<Vertex> MeshVertices;
		std::vector<uint32_t> MeshIndices;

		// Texture

		//vk::PhysicalDeviceFeatures DeviceFeatures;

		vk::raii::CommandPool CommandPool{ nullptr };
		std::vector<vk::raii::CommandBuffer> CommandBuffers;

		vk::raii::Semaphore ImageAvailableSemaphore{ nullptr };
		std::vector<vk::raii::Semaphore> RenderFinishedSemaphores;
		// Signalled when all of a frame's GPU work is done, including ImGui's OS windows.
		vk::raii::Fence InFlightFence{ nullptr };

		bool bRecreateSwapChain = false;

		// DrawFrame() without its handling of a lost device
		void DrawFrameOnDevice(const std::function<void()>& buildUI);

		// The frame's start, and the times of its steps before the UI, for the painter's statistics
		struct FrameClock
		{
			std::chrono::steady_clock::time_point Start;
			Nyx::UI::PainterWindowStats::Times Times;
		};
		FrameClock StartFrameClock() const;

		// DrawFrameOnDevice()'s steps, in their order
		// Until the last frame's GPU work is done; then the views let go of during it are destroyed
		void WaitForLastFrame(FrameClock& clock);
		// The swapchain image to draw into; none when the swapchain had to be made again
		std::optional<uint32_t> AcquireImage(FrameClock& clock);
		vk::raii::CommandBuffer& BeginCommands(uint32_t imageIndex);
		// Every shown world's state, the scene views' editor passes, the views, and their blur
		void RenderScene(vk::raii::CommandBuffer& cmd, FrameClock& clock);
		// Per view: a size asked for made, its uniforms, and the editor's own passes: picking, and the
		// selection masks
		void DrawEditorPasses(vk::raii::CommandBuffer& cmd);
		// Values set for materials while the UI was built (SetMaterialValues())
		void ApplyPendingMaterialValues();
		// The frame's command buffer, ImGui's OS windows, and the fence that covers them all
		void Submit(vk::raii::CommandBuffer& cmd, uint32_t imageIndex);
		void Present(uint32_t imageIndex, const FrameClock& clock);
		// Set when the device is lost; nothing is drawn after that (IsDeviceLost())
		bool bDeviceLost = false;
	};
}
