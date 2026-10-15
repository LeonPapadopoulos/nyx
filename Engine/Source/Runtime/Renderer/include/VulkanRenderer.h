#pragma once
#include "Renderer.h"
#include <vulkan/vulkan_raii.hpp>
#include "VulkanContext.h"
#include "VulkanSwapchain.h"
#include "OffscreenRenderTarget.h"
#include "VulkanViewportTarget.h"
#include "VulkanPainter.h"
#include "Paths.h"
#include "ShaderPrograms.h"
#include "VulkanBlur.h"
#include "VulkanBindless.h"
#include "VulkanMaterials.h"
#include "VulkanEnvironment.h"
#include "VulkanGlass.h"
#include "Painter.h"
#include "WindowPainter.h"
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
	class VulkanImGuiBackend;

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
		Nyx::Engine::Entity SourceEntity{};

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
		const Nyx::Engine::Registry* Registry = nullptr;
		// New each time the world is set, also at the same address, and never the same for two
		// worlds; picks of another revision are dropped. 0 is none.
		uint64_t Revision = 0;

		std::vector<RenderObject> Objects;
		// Dense picking IDs: ID i is the entity at i - 1, 0 is nothing
		std::vector<Nyx::Engine::Entity> PickingIdToEntity;
		// Its debug lines, in DebugLineVertices
		uint32_t FirstDebugLineVertex = 0;
		uint32_t DebugLineVertexCount = 0;
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
		friend struct Nyx::Tests::SceneReplacementTestAccess;

	public:
		VulkanRenderer();
		virtual ~VulkanRenderer() = default;

		virtual void Initialize(const char* applicationName, GLFWwindow* window);
		virtual void Shutdown();

		virtual void BeginFrame();
		virtual void EndFrame();

		virtual void OnFramebufferResized();
		void SetVSync(bool bVSync) override;
		bool IsVSync() const override;
		const char* GetApiName() const override { return "Vulkan"; }

		virtual void DrawFrame(const std::function<void()>& buildUI);

		void SetSelectedEntity(std::optional<Nyx::Engine::Entity> entity) override;
		void SetSelectionOutlineMode(ESelectionOutlineMode mode) override;

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

		void SetWorld(const Nyx::Engine::Registry* world) override;
		void SetSceneViewWorld(uint64_t id, const Nyx::Engine::Registry* world) override;

		Nyx::Mesh* GetCubeMesh() override;
		Nyx::Mesh* GetSphereMesh() override;
		Nyx::Mesh* GetPlaneMesh() override;
		Nyx::Material* GetMaterial(const std::string& path) override;
		bool IsCompilingShaders() const override;
		const Nyx::Engine::ShaderLog& GetShaderLog() const override { return ShaderCompiles; }
		void SetShaderCompiledListener(ShaderCompiledListener listener) override { OnShaderCompiled = std::move(listener); }
		void FollowEditorShaders() override { Programs.FollowEditor(); }
		void ApplyCompiledShader(const Nyx::Engine::ShaderCompileRequest& request, const Nyx::Engine::ShaderCompileResult& result) override { Programs.Inject(request, result); }
		MaterialView ViewMaterial(const std::string& path) override;
		void SetMaterialValues(const std::string& path, const Nyx::Engine::MaterialAsset& asset) override;

	public:
		VulkanContext& GetContext();
		VulkanSwapchain& GetSwapchain();

		// The light of the sky, for tests
		Nyx::Engine::VulkanEnvironment& GetEnvironment() { return Environment; }
		Nyx::Engine::VulkanBindless& GetBindless() { return Bindless; }
		// The sky drawn behind the scene and lighting it: a cube of the bindless array, e.g. a
		// test's uniform one; its light is made again (waits for the GPU)
		void SetSky(uint32_t cubeSlot);
		uint32_t GetSky() const { return SkyboxTextureIndex; }

		// Draws 2D shapes recorded with a Painter into a VulkanPainterTarget
		Nyx::Engine::VulkanPainter& GetPainterPass() { return PainterPass; }

		uint64_t CreatePainterCanvas() override;
		void DestroyPainterCanvas(uint64_t id) override;
		void SubmitPainterCanvas(uint64_t id, const Nyx::Engine::Painter& painter, const Nyx::Engine::PainterDrawOptions& options) override;
		ImTextureID GetPainterCanvasTextureId(uint64_t id) const override;
		std::optional<Nyx::Engine::PainterTiles> GetPainterCanvasTiles(uint64_t id) const override;
		const Nyx::Engine::PainterWindowStats& GetWindowPainterStats() const override { return WindowPainterStats; }

		uint64_t CreateUIImage(const Nyx::Engine::RgbaImage& image) override;
		void DestroyUIImage(uint64_t id) override;
		ImTextureID GetUIImageTextureId(uint64_t id) const override;

		void SaveScreenshot(const std::filesystem::path& path, ImVec2 min, ImVec2 max) override;
		const std::string& GetScreenshotMessage() const override { return ScreenshotMessage; }

		ImTextureID GetSceneViewTextureId(uint64_t id) const override;
		// What a scene view rendered last, as RGBA; waits for the GPU. False for an unknown view. For tests.
		bool ReadSceneViewPixels(uint64_t id, Nyx::Engine::RgbaImage& outImage);
		Extent2D GetSceneViewExtent(uint64_t id) const override;


	private:
		void SetupVulkan(const char* applicationName, GLFWwindow* window);
		void SetupImGui();

		void CreateGraphicsPipeline();
		void CreateCommandPool();
		void CreateCommandBuffers();
		void CreateSyncObjects();

	private:
		void RecreateSwapChain();
		// False while the window is minimized
		bool HasValidFramebufferSize() const;

		// The world's state, once per frame: a new revision when it is set
		void BeginWorldRevision(const Nyx::Engine::Registry* registry);
		RenderWorld* FindRenderWorld(const Nyx::Engine::Registry* registry);
		const RenderWorld* FindRenderWorld(const Nyx::Engine::Registry* registry) const;
		// The world the view shows; null without one
		const RenderWorld* FindShownWorld(const SceneViewInstance& view) const;
		// The revision of the world the view shows; 0 without one
		uint64_t GetShownWorldRevision(const SceneViewInstance& view) const;
		// Whether the view shows the renderer's world, and something is selected in it
		bool ShowsSelection(const SceneViewInstance& view) const;
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
		void CreateGridPipeline();
		void CreatePickingPipeline();

		// The scene views' scene pass (HDR), for what draws the scene: materials, the sky
		vk::RenderPass GetSceneRenderPass() const;
		// Their picture pass (8-bit, after the tonemap), for what draws over the picture
		vk::RenderPass GetPictureRenderPass() const;
		// The view's HDR picture into its bindless slot, after the view's targets were made
		void RegisterHdrPicture(SceneViewInstance& view);
		void CreateTonemapPipeline();
		void DrawTonemap(SceneViewInstance& view, vk::raii::CommandBuffer& cmd);

		void LoadSkyboxCubemap();

		void CreatePickingResourcesForView(SceneViewInstance& view);
		void DestroyPickingResourcesForView(SceneViewInstance& view);
		void RecreatePickingResourcesForView(SceneViewInstance& view);

		struct OutlineCompositePushConstants
		{
			glm::vec4 Params0{ 0.0f }; // x=TexelSizeX, y=TexelSizeY, z=Thickness, w=Mode
			glm::vec4 VisibleOutlineColor{ 1.0f, 0.65f, 0.0f, 1.0f };
			glm::vec4 OccludedOutlineColor{ 0.2f, 0.8f, 1.0f, 1.0f };
			glm::vec4 HatchColor{ 0.2f, 0.8f, 1.0f, 0.9f };
			glm::vec4 HatchParams{ 14.0f, 2.0f, 8.0f, 6.0f }; // spacing, thickness, dashLen, dashGap
		};

		void CreateOutlineDescriptorSetLayout();
		void CreateOutlineDescriptorSetForView(SceneViewInstance& view);

		void CreateSelectionMaskResourcesForView(SceneViewInstance& view);
		void DestroySelectionMaskResourcesForView(SceneViewInstance& view);
		void RecreateSelectionMaskResourcesForView(SceneViewInstance& view);

		void CreateSelectionMaskPipelines();
		void DrawVisibleSelectionMaskPass(SceneViewInstance& view, const RenderWorld& world, vk::raii::CommandBuffer& cmd);
		void DrawFullSelectionMaskPass(SceneViewInstance& view, const RenderWorld& world, vk::raii::CommandBuffer& cmd);
		// What the view's selection masks are drawn from, as one number: the selection, the
		// world, the camera, the size, and every object's mesh and transform. Unset while the
		// view shows no selection (ShowsSelection()), when there is nothing to draw them for.
		std::optional<uint64_t> GetSelectionMaskFingerprint(const SceneViewInstance& view, const RenderWorld& world) const;

		void CreateOutlineCompositePipeline();
		void DrawSelectionOutline(SceneViewInstance& view, vk::raii::CommandBuffer& cmd);

		void CreateSkyboxPipeline();
		void DrawSkybox(SceneViewInstance& view, vk::raii::CommandBuffer& cmd);

		void DrawGrid(SceneViewInstance& view, vk::raii::CommandBuffer& cmd);

		vk::raii::ShaderModule CreateShaderModule(const std::vector<uint32_t>& spirv);

		// A shader's SPIR-V by file name (e.g. "Grid.vert.spv"), from Programs: the last good
		// compile, at first the build's
		std::vector<uint32_t> ReadSpirvFile(const std::string& fileName);

		void CreateSceneResourcesForView(SceneViewInstance& view);
		void UpdateSceneUniforms(SceneViewInstance& view);

		void CreateTestMeshData();

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

		// Live reload of the engine's shaders: turned on where the shader sources and
		// NyxShaderCompiler.exe are found
		void StartLiveShaderReload();
		// Once per frame, before recording: logs what compiled, and makes the pipelines of changed
		// shaders again
		void ApplyShaderUpdates();
		// One compile into the Shader Log (ShaderCompiles)
		void RecordCompile(const Nyx::Engine::ShaderProgramUpdate& update);

		// The cube of a view's sky, with its light, made at the first ask
		uint32_t GetSkySlot(ESceneViewSky sky);

		void ExtractSceneGlobalsFromEditorCamera(SceneViewInstance& view);
		void ExtractSceneGlobalsFromWorldCamera(SceneViewInstance& view, const Nyx::Engine::Registry& registry);
		void UpdateViewportSceneGlobals(SceneViewInstance& view, const Nyx::Engine::Registry& registry);

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
		float GetBlurMilliseconds() const override { return BlurMilliseconds; }
		std::vector<uint64_t> GetSceneViewIds() const override;

		SceneViewInstance* FindSceneView(uint64_t id);
		const SceneViewInstance* FindSceneView(uint64_t id) const;

		void EnsureSceneViewSize(SceneViewInstance& view, uint32_t width, uint32_t height);
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
		void DrawDebugLines(SceneViewInstance& view, const RenderWorld& world, vk::raii::CommandBuffer& cmd);

		void RequestPick(uint64_t sceneViewId, uint32_t pixelX, uint32_t pixelY);
		Nyx::IRenderer::PickResult ConsumeLastPickResult(uint64_t sceneViewId);

		void DrawPickingPass(SceneViewInstance& view, const RenderWorld& world, vk::raii::CommandBuffer& cmd);
		void ResolvePickRequest(SceneViewInstance& view, vk::raii::CommandBuffer& cmd);
		void ReadBackPickResults();

	private:
	private:
		GLFWwindow* Window = nullptr;
		std::unique_ptr<VulkanImGuiBackend> ImGuiBackend;

		VulkanContext Context;
		VulkanSwapchain Swapchain;

		// The engine's shaders by name; every pipeline is made from them. What live reload compiles
		// is kept in Saved/ShaderCache/Engine, so the next start begins from it.
		Nyx::Engine::ShaderPrograms Programs{ Nyx::Paths::GetCompiledShadersDir(), Nyx::Paths::GetShadersDir(), Nyx::Paths::GetShaderCacheDir() / "Engine" };

		Nyx::Engine::VulkanPainter PainterPass;

		// Blurs every scene view's picture after it renders
		Nyx::Engine::VulkanBlur BlurPass;
		void BlurSceneViews(vk::raii::CommandBuffer& cmd);
		void ReleaseSceneViewBlur(SceneViewInstance& view);

		// Points an ImGui texture at an image: a new texture the first time, the same texture
		// pointed at the new image when it changed (e.g. a blur target resized). An image is told
		// by its target's generation, not by its view: a new view may get the handle of the old
		// one, and the texture would go on showing the destroyed image. `shownGeneration` is the
		// generation the texture shows now.
		void PointImGuiTexture(VkDescriptorSet& texture, uint64_t& shownGeneration, vk::ImageView view, uint64_t generation);

		// What glass shows: ImGui's main window drawn up to the first glass window, blurred, each
		// frame there is glass, before the main window itself is drawn. Its ImGui texture keeps its
		// ID.
		Nyx::Engine::VulkanGlassBackdrop GlassBackdrop;
		VkDescriptorSet GlassImGuiTexture = VK_NULL_HANDLE;
		uint64_t GlassImGuiTextureGeneration = 0;
		void DrawGlassBackdrop(vk::raii::CommandBuffer& cmd);

		// What glass shows in OS windows of their own (Nyx::UI::SetPlainGlassBackdrop()): one
		// pixel in the theme's backdrop colour, a UI image made the first time it is asked for
		ImTextureID GetPlainGlassBackdrop();
		uint64_t PlainGlassImageId = 0;

		// GPU timestamps around the blurs: written in a frame, read once its fence is signalled.
		// Null where the graphics queue can't write timestamps.
		vk::raii::QueryPool BlurTimestamps{ nullptr };
		bool bBlurTimestampsWritten = false;
		float BlurMilliseconds = -1.0f;
		void CreateBlurTimestamps();
		void ReadBlurTimestamps();

		// An image for 2D shapes, shown with ImGui::Image() (CreatePainterCanvas())
		struct PainterCanvas
		{
			uint64_t Id = 0;
			Nyx::Engine::VulkanPainterTarget Target;

			// This frame's shapes, copied from SubmitPainterCanvas(); drawn once, then cleared
			Nyx::Engine::Painter Frame;
			Nyx::Engine::PainterDrawOptions Options;
			bool bSubmitted = false;

			// How ImGui shows the canvas: a descriptor set made by ImGui's backend. It is pointed
			// at the new image when the target is recreated (the target's generation changes),
			// so its ImTextureID never changes.
			vk::raii::Sampler Sampler{ nullptr };
			VkDescriptorSet ImGuiTexture = VK_NULL_HANDLE;
			uint64_t ImGuiTextureGeneration = 0;
		};

		// Draws the canvases submitted this frame, before ImGui renders
		void DrawPainterCanvases(vk::raii::CommandBuffer& cmd);
		void ReleasePainterCanvas(PainterCanvas& canvas);
		PainterCanvas* FindPainterCanvas(uint64_t id);
		const PainterCanvas* FindPainterCanvas(uint64_t id) const;

		// Pointers, so a canvas stays in place while others are added or removed
		std::vector<std::unique_ptr<PainterCanvas>> PainterCanvases;
		uint64_t NextPainterCanvasId = 1;

		// Painting inside ImGui windows (Nyx::UI::GetWindowPainter()), in two steps around
		// ImGui::Render():
		//  - GatherWindowPainters(), once the UI is built: ends the window painters;
		//  - UploadWindowSegments(), once ImGui has made the draw data: finds the runs of
		//    segments with nothing ImGui draws between them (Nyx::UI::FindWindowSegmentRuns()),
		//    copies each run into one painter (Painter::AppendSegment()) and adds it as one draw,
		//    then records binning them all on the GPU.
		// ImGui then calls back where each segment goes: DrawWindowSegment() draws a run at its
		// first segment, and nothing at the others.
		void GatherWindowPainters();
		void UploadWindowSegments(vk::CommandBuffer commandBuffer);
		// At the start of a frame, once the last one's GPU work is done: the GPU's binning
		// numbers, and the heatmap's heat, into the last frame's statistics
		void ReadWindowSegments();
		static void DrawWindowSegment(const Nyx::UI::WindowPainter& painter, uint32_t segment, const ImDrawCmd& command, void* renderer);
		void DrawWindowSegment(const Nyx::UI::WindowPainter& painter, uint32_t segment);

		// This frame's window painters, and their draws
		std::span<Nyx::UI::WindowPainter* const> WindowPainters;
		Nyx::Engine::VulkanPainterSegments WindowSegments;
		// What each segment's callback draws: the index of its run's draw in WindowSegments at the
		// run's first segment, NoWindowDraw at the others
		static constexpr uint32_t NoWindowDraw = UINT32_MAX;
		std::map<std::pair<const Nyx::UI::WindowPainter*, uint32_t>, uint32_t> WindowSegmentDraws;
		// The painters runs are copied into, one per draw of several segments. Kept apart, so
		// that no two have the same address in a frame: WindowSegments tells painters apart by it.
		std::vector<std::unique_ptr<Nyx::Engine::Painter>> RunPainters;
		// The OS window of each draw in WindowSegments, as an index into WindowPainterStats.OsWindows
		std::vector<uint32_t> WindowSegmentDrawOsWindows;
		// The last frame's statistics: its CPU times as the frame is built, its GPU times once its
		// fence has passed
		Nyx::Engine::PainterWindowStats WindowPainterStats;
		// This frame's CPU times taken before GatherWindowPainters() starts its statistics
		Nyx::Engine::PainterWindowStats::Times PendingFrameTimes;
		// When the last frame's DrawFrame() ended, for the time spent outside it
		std::chrono::steady_clock::time_point LastFrameEnd;

		// GPU timestamps around the frame's drawing, in pairs: the main window's UI pass, the
		// glass backdrop, the binning, the scene views' editor passes, the scene views, the whole
		// command buffer, and each window segment draw, in any OS window. Reset and written in a
		// frame, read once its fence has passed. Null where the graphics queue can't write
		// timestamps.
		static constexpr uint32_t MainWindowUIPair = 0;
		static constexpr uint32_t GlassBackdropPair = 1;
		static constexpr uint32_t BinningPair = 2;
		static constexpr uint32_t EditorPassesPair = 3;
		static constexpr uint32_t SceneViewsPair = 4;
		static constexpr uint32_t FramePair = 5;
		static constexpr uint32_t FirstSegmentPair = 6;
		static constexpr uint32_t MaxTimedSegmentDraws = 250;
		static constexpr uint32_t UITimestampCount = 2 * (FirstSegmentPair + MaxTimedSegmentDraws);
		struct TimedPair
		{
			uint32_t Pair = 0;
			// The OS window of a segment draw, an index into WindowPainterStats.OsWindows
			uint32_t OsWindow = 0;
		};
		vk::raii::QueryPool UITimestamps{ nullptr };
		// The pairs written this frame, and the next free one for a segment draw
		std::vector<TimedPair> UITimedPairs;
		uint32_t NextSegmentPair = FirstSegmentPair;
		void CreateUITimestamps();
		// Writes a pair's start before what it times, or its end after it, which keeps the pair to
		// read, with its OS window for a segment draw. Nothing without UITimestamps.
		void WriteUITimestamp(vk::CommandBuffer commandBuffer, uint32_t pair, bool bEnd, uint32_t osWindow = UINT32_MAX);
		// Adds the last frame's GPU times to WindowPainterStats
		void ReadUITimestamps();

		// An image for the UI (CreateUIImage()), shown through a descriptor set made by ImGui's backend
		struct UIImage
		{
			uint64_t Id = 0;
			vk::raii::Image Image{ nullptr };
			vk::raii::DeviceMemory Memory{ nullptr };
			vk::raii::ImageView View{ nullptr };
			vk::raii::Sampler Sampler{ nullptr };
			VkDescriptorSet ImGuiTexture = VK_NULL_HANDLE;
		};
		std::vector<std::unique_ptr<UIImage>> UIImages;
		// Destroyed ones, released once the GPU is done with the frame that may still show them
		std::vector<std::unique_ptr<UIImage>> UIImagesToRelease;
		uint64_t NextUIImageId = 1;
		void ReleaseUIImage(UIImage& image);

		// A screenshot asked for this frame (SaveScreenshot()): the rect is copied out of the
		// swapchain image after ImGui has rendered into it, and written to the file once the
		// frame's fence is signalled.
		struct ScreenshotRequest
		{
			std::filesystem::path Path;
			// In the swapchain image's pixels
			vk::Rect2D Region;
			vk::raii::Buffer Buffer{ nullptr };
			vk::raii::DeviceMemory Memory{ nullptr };
			// The swapchain's format holds blue first (B8G8R8A8)
			bool bBlueFirst = false;
			bool bRecorded = false;
		};
		std::optional<ScreenshotRequest> Screenshot;
		std::string ScreenshotMessage;
		void RecordScreenshot(vk::raii::CommandBuffer& cmd, uint32_t imageIndex);
		void FinishScreenshot();

		std::vector<std::unique_ptr<Nyx::Mesh>> LoadedMeshes;
		std::vector<std::unique_ptr<Nyx::Texture>> LoadedTextures;

		std::vector<SceneViewInstance> SceneViews;
		// Views let go of while a frame was recorded, destroyed at the next frame's start
		std::vector<uint64_t> SceneViewsToDestroy;
		// Between waiting for the last frame and the end of DrawFrame(): the frame's commands are recorded
		bool bRecordingFrame = false;
		uint64_t NextSceneViewId = 1;

		// Set 0 of every scene pipeline: every texture, the fixed samplers, the material buffer
		Nyx::Engine::VulkanBindless Bindless;

		vk::raii::DescriptorSetLayout SceneDescriptorSetLayout{ nullptr };
		// Shared by the scene, sky, grid, debug line, picking and selection mask pipelines
		vk::raii::PipelineLayout ScenePipelineLayout{ nullptr };

		// The meshes' materials, from their .nyxmat files
		Nyx::Engine::VulkanMaterials Materials;
		// Every shader's last compile (GetShaderLog())
		Nyx::Engine::ShaderLog ShaderCompiles;
		ShaderCompiledListener OnShaderCompiled;
		// Values set for materials (SetMaterialValues()), written once the UI is built: the frame's
		// draws are recorded by then, but run after
		std::map<std::string, Nyx::Engine::MaterialAsset> PendingMaterialValues;
		// The sky's light, for physically based materials
		Nyx::Engine::VulkanEnvironment Environment;

		// The renderer's world (SetWorld()), owned by the editor / scene document
		const Nyx::Engine::Registry* World = nullptr;
		// Every world a scene view shows, the renderer's world among them; owned here so that
		// their addresses stay while worlds come and go
		std::vector<std::unique_ptr<RenderWorld>> RenderWorlds;
		uint64_t LastWorldRevision = 0;

		float SceneTime = 0.0f;

		// Picking
		vk::raii::Pipeline PickingPipeline{ nullptr };

		// Selection
		std::optional<Nyx::Engine::Entity> SelectedEntity;
		ESelectionOutlineMode SelectionOutlineMode = ESelectionOutlineMode::VisibleOccludeHatched;

		vk::raii::DescriptorSetLayout OutlineDescriptorSetLayout{ nullptr };
		vk::raii::Sampler OutlineCompositeSampler{ nullptr };

		vk::raii::Pipeline SelectionMaskPipelineVisible{ nullptr };
		vk::raii::Pipeline SelectionMaskPipelineFull{ nullptr };

		vk::raii::PipelineLayout OutlineCompositePipelineLayout{ nullptr };
		vk::raii::Pipeline OutlineCompositePipeline{ nullptr };

		// Grid
		vk::raii::Pipeline GridPipeline{ nullptr };


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
		// Set when the device is lost; nothing is drawn after that (IsDeviceLost())
		bool bDeviceLost = false;

		// Debugging
		static constexpr uint32_t MaxDebugLineVertices = 65536;

		std::vector<DebugLineVertex> DebugLineVertices;

		vk::raii::Buffer DebugLineVertexBuffer{ nullptr };
		vk::raii::DeviceMemory DebugLineVertexBufferMemory{ nullptr };

		vk::raii::Pipeline DebugLinePipeline{ nullptr };
	};
}
