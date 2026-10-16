#pragma once

#include "EditorRenderer.h"
#include "Entity.h"

#include <vulkan/vulkan_raii.hpp>

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace Nyx::Tests
{
	struct SceneReplacementTestAccess;
}

namespace Nyx
{
	class Registry;
	class ShaderPrograms;
	class VulkanContext;
	struct RenderWorld;
	struct SceneViewInstance;

	enum class ESelectionOutlineMode : uint8_t
	{
		VisibleOnly = 0,
		FullSilhouette = 1,
		VisibleOccludeHatched = 2
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

	// A scene view's part of the editor overlays: its picking and selection mask targets, and its
	// pick in flight
	struct SceneViewOverlays
	{
		// -------------------------------------------------
		// Picking
		// -------------------------------------------------
		vk::raii::Image PickingImage{ nullptr };
		vk::raii::DeviceMemory PickingImageMemory{ nullptr };
		vk::raii::ImageView PickingImageView{ nullptr };

		vk::raii::Image PickingDepthImage{ nullptr };
		vk::raii::DeviceMemory PickingDepthImageMemory{ nullptr };
		vk::raii::ImageView PickingDepthImageView{ nullptr };

		vk::raii::RenderPass PickingRenderPass{ nullptr };
		vk::raii::Framebuffer PickingFramebuffer{ nullptr };

		vk::raii::Buffer PickingReadbackBuffer{ nullptr };
		vk::raii::DeviceMemory PickingReadbackBufferMemory{ nullptr };

		bool bPickRequestPending = false;
		bool bPickReadbackPending = false;
		bool bPickResultReady = false;

		uint32_t PendingPickX = 0;
		uint32_t PendingPickY = 0;

		std::optional<Nyx::Entity> LastPickedEntity;

		uint64_t RenderedWorldRevision = 0;
		uint64_t PickWorldRevision = 0;

		void ResetPicking()
		{
			bPickRequestPending = false;
			bPickReadbackPending = false;
			bPickResultReady = false;
			PendingPickX = 0;
			PendingPickY = 0;
			LastPickedEntity.reset();
			RenderedWorldRevision = 0;
			PickWorldRevision = 0;
		}

		// -------------------------------------------------
		// Selection
		// -------------------------------------------------

		vk::raii::Image VisibleSelectionMaskImage{ nullptr };
		vk::raii::DeviceMemory VisibleSelectionMaskImageMemory{ nullptr };
		vk::raii::ImageView VisibleSelectionMaskImageView{ nullptr };

		vk::raii::Image FullSelectionMaskImage{ nullptr };
		vk::raii::DeviceMemory FullSelectionMaskImageMemory{ nullptr };
		vk::raii::ImageView FullSelectionMaskImageView{ nullptr };

		vk::raii::Image SelectionMaskDepthImage{ nullptr };
		vk::raii::DeviceMemory SelectionMaskDepthImageMemory{ nullptr };
		vk::raii::ImageView SelectionMaskDepthImageView{ nullptr };

		vk::raii::RenderPass SelectionMaskRenderPass{ nullptr };
		vk::raii::Framebuffer VisibleSelectionMaskFramebuffer{ nullptr };
		vk::raii::Framebuffer FullSelectionMaskFramebuffer{ nullptr };

		vk::raii::DescriptorPool OutlineDescriptorPool{ nullptr };
		std::optional<vk::raii::DescriptorSets> OutlineDescriptorSets;

		// What the masks were last drawn from (VulkanEditorOverlays::GetSelectionMaskFingerprint()):
		// they are drawn again only when it changes. Unset while nothing is selected, and when the
		// masks are made anew, so that they are drawn before the outline reads them.
		std::optional<uint64_t> SelectionMaskFingerprint;
	};

	// The editor's overlays of the scene views: the grid, debug lines, picking, and the selection
	// outline drawn from the selection masks. The renderer calls them for the views whose
	// bShowEditorOverlays is on; every view has their resources.
	class VulkanEditorOverlays
	{
		friend struct Nyx::Tests::SceneReplacementTestAccess;

	public:
		// Binds the scene pipelines' two sets, the global one and the view's, for a draw into the view
		using BindSceneSetsFunction = std::function<void(SceneViewInstance& view, vk::raii::CommandBuffer& cmd)>;

		// Both must outlive the overlays
		VulkanEditorOverlays(VulkanContext& context, ShaderPrograms& programs);

		// Before the first view is made: what every view's outline set is made with, and the debug
		// lines' buffer. The overlays draw with the scene pipelines' layout (the global set, the
		// view's set, ObjectConstants), which must outlive them.
		void Initialize(vk::PipelineLayout scenePipelineLayout, BindSceneSetsFunction bindSceneSets);
		// Once the first view is made: the pipelines, for its passes, which every view's are like
		void CreatePipelines(const SceneViewInstance& firstView);
		// The pipelines of the shaders `uses()` says changed ("Grid": Grid.vert or Grid.frag), made
		// again; the GPU must be done with the old ones
		void RebuildPipelines(const std::function<bool(const char* shader)>& uses, const SceneViewInstance& firstView);

		// The renderer's world, which the selection is in; discards the selection
		void SetWorld(const Nyx::Registry* world);
		void SetSelectedEntity(std::optional<Nyx::Entity> entity);

		// A view's picking and selection mask targets, at its size: when it is made, after it
		// changed size, and when it goes
		void CreateViewResources(SceneViewInstance& view);
		void RecreateViewResources(SceneViewInstance& view);
		void DestroyViewResources(SceneViewInstance& view);

		// Before the views are drawn: picking for a click, and the selection masks, which the
		// outline is drawn from
		void DrawPasses(SceneViewInstance& view, const RenderWorld& world, vk::raii::CommandBuffer& cmd);
		// Over the view's picture: the grid, and with a world its debug lines and the selection
		// outline
		void Draw(SceneViewInstance& view, const RenderWorld* world, vk::raii::CommandBuffer& cmd);

		// Every world's debug lines, once per frame: reset, then added world by world, then uploaded
		void ResetDebugLines();
		// The world's objects' axes and bounds, and the world's axes
		void AddDebugLines(RenderWorld& world);
		void UploadDebugLines();

		// A click at a pixel of the view, which shows `worldRevision` of its world; picked from in
		// the next frame
		void RequestPick(SceneViewInstance& view, uint64_t worldRevision, uint32_t pixelX, uint32_t pixelY);
		// The entity at the click, once it was read back; dropped when the view's world changed
		// since (`worldRevision` is the one it shows now)
		IEditorRenderer::PickResult ConsumeLastPickResult(SceneViewInstance& view, uint64_t worldRevision);
		// After the last frame's fence: what its pick copy read, decoded against the world the view
		// shows (null: none)
		void ReadBackPick(SceneViewInstance& view, const RenderWorld* world);

	private:
		void CreateGridPipeline(const SceneViewInstance& firstView);
		void CreatePickingPipeline(const SceneViewInstance& firstView);
		void DrawGrid(SceneViewInstance& view, vk::raii::CommandBuffer& cmd);

		// Whether the view shows the renderer's world, and something is selected in it
		bool ShowsSelection(const SceneViewInstance& view) const;

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

		void CreateSelectionMaskPipelines(const SceneViewInstance& firstView);
		void DrawVisibleSelectionMaskPass(SceneViewInstance& view, const RenderWorld& world, vk::raii::CommandBuffer& cmd);
		void DrawFullSelectionMaskPass(SceneViewInstance& view, const RenderWorld& world, vk::raii::CommandBuffer& cmd);
		// What the view's selection masks are drawn from, as one number: the selection, the
		// world, the camera, the size, and every object's mesh and transform. Unset while the
		// view shows no selection (ShowsSelection()), when there is nothing to draw them for.
		std::optional<uint64_t> GetSelectionMaskFingerprint(const SceneViewInstance& view, const RenderWorld& world) const;

		void CreateOutlineCompositePipeline(const SceneViewInstance& firstView);
		void DrawSelectionOutline(SceneViewInstance& view, vk::raii::CommandBuffer& cmd);

		void CreateDebugLineResources();
		void CreateDebugLinePipeline(const SceneViewInstance& firstView);

		void AddDebugLine(const glm::vec3& a, const glm::vec3& b, const glm::vec3& color);
		void AddDebugAxes(const glm::mat4& worldTransform, float axisLength);
		void AddDebugAABB(const glm::vec3& min, const glm::vec3& max, const glm::vec3& color);
		void AddDebugOrientedBox(
			const glm::mat4& worldTransform,
			const glm::vec3& localMin,
			const glm::vec3& localMax,
			const glm::vec3& color);

		void DrawDebugLines(SceneViewInstance& view, const RenderWorld& world, vk::raii::CommandBuffer& cmd);

		void DrawPickingPass(SceneViewInstance& view, const RenderWorld& world, vk::raii::CommandBuffer& cmd);
		void ResolvePickRequest(SceneViewInstance& view, const RenderWorld& world, vk::raii::CommandBuffer& cmd);

		void CreateImage(
			uint32_t width,
			uint32_t height,
			vk::Format format,
			vk::ImageUsageFlags usage,
			vk::ImageAspectFlags aspectFlags,
			vk::raii::Image& outImage,
			vk::raii::DeviceMemory& outMemory,
			vk::raii::ImageView& outImageView);

		VulkanContext& Context;
		ShaderPrograms& Programs;
		vk::PipelineLayout ScenePipelineLayout;
		BindSceneSetsFunction BindSceneSets;

		// Picking
		vk::raii::Pipeline PickingPipeline{ nullptr };

		// Selection, in the renderer's world
		const Nyx::Registry* World = nullptr;
		std::optional<Nyx::Entity> SelectedEntity;
		ESelectionOutlineMode SelectionOutlineMode = ESelectionOutlineMode::VisibleOccludeHatched;

		vk::raii::DescriptorSetLayout OutlineDescriptorSetLayout{ nullptr };
		vk::raii::Sampler OutlineCompositeSampler{ nullptr };

		vk::raii::Pipeline SelectionMaskPipelineVisible{ nullptr };
		vk::raii::Pipeline SelectionMaskPipelineFull{ nullptr };

		vk::raii::PipelineLayout OutlineCompositePipelineLayout{ nullptr };
		vk::raii::Pipeline OutlineCompositePipeline{ nullptr };

		// Grid
		vk::raii::Pipeline GridPipeline{ nullptr };

		// Debugging
		static constexpr uint32_t MaxDebugLineVertices = 65536;

		std::vector<DebugLineVertex> DebugLineVertices;

		vk::raii::Buffer DebugLineVertexBuffer{ nullptr };
		vk::raii::DeviceMemory DebugLineVertexBufferMemory{ nullptr };

		vk::raii::Pipeline DebugLinePipeline{ nullptr };
	};
}
