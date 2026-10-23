#include "NyxPCH.h"
#include "Assertions.h"
#include "VulkanRenderer.h"
#include "VulkanContext.h"
#include "VulkanSwapchain.h"
#include "VulkanUtil.h"
#include "Log.h"
#include "GltfImporter.h"
#include "TransformComponent.h"
#include "CameraProjection.h"
#include "PreviousTransformComponent.h"
#include "FixedStepClock.h"
#include "MeshRendererComponent.h"
#include "Paths.h"
#include "Profiler.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ranges>
#include <set>
#include <string>
#include <utility>
#include <fstream>
#include <limits>
#include <optional>


#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/packing.hpp>

#include <glm/gtx/norm.hpp>

#include <stb_image.h>

namespace
{
	struct ProjectedPoint
	{
		glm::vec2 PixelPos{ 0.0f };
		float Depth = 1.0f;
		bool bValid = false;
	};

	ProjectedPoint ProjectWorldToViewportPixel(
		const glm::vec3& worldPos,
		const glm::mat4& viewProj,
		const vk::Extent2D& extent)
	{
		ProjectedPoint result{};

		const glm::vec4 clip = viewProj * glm::vec4(worldPos, 1.0f);

		if (std::abs(clip.w) < 1e-6f)
		{
			return result;
		}

		const glm::vec3 ndc = glm::vec3(clip) / clip.w;

		// Vulkan-style clip depth
		if (ndc.z < 0.0f || ndc.z > 1.0f)
		{
			return result;
		}

		result.PixelPos.x = (ndc.x * 0.5f + 0.5f) * static_cast<float>(extent.width);
		result.PixelPos.y = (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(extent.height);
		result.Depth = ndc.z;
		result.bValid = true;
		return result;
	}

	void ComputeLocalBounds(
		const std::vector<Nyx::Vertex>& vertices,
		glm::vec3& outMin,
		glm::vec3& outMax)
	{
		ASSERT(!vertices.empty());

		outMin = glm::vec3(std::numeric_limits<float>::max());
		outMax = glm::vec3(std::numeric_limits<float>::lowest());

		for (const Nyx::Vertex& vertex : vertices)
		{
			outMin = glm::min(outMin, vertex.Position);
			outMax = glm::max(outMax, vertex.Position);
		}
	}

	void TransformAABB(
		const glm::vec3& localMin,
		const glm::vec3& localMax,
		const glm::mat4& worldTransform,
		glm::vec3& outWorldMin,
		glm::vec3& outWorldMax)
	{
		const glm::vec3 corners[8] = {
			{ localMin.x, localMin.y, localMin.z },
			{ localMax.x, localMin.y, localMin.z },
			{ localMin.x, localMax.y, localMin.z },
			{ localMax.x, localMax.y, localMin.z },
			{ localMin.x, localMin.y, localMax.z },
			{ localMax.x, localMin.y, localMax.z },
			{ localMin.x, localMax.y, localMax.z },
			{ localMax.x, localMax.y, localMax.z }
		};

		outWorldMin = glm::vec3(std::numeric_limits<float>::max());
		outWorldMax = glm::vec3(std::numeric_limits<float>::lowest());

		for (const glm::vec3& corner : corners)
		{
			const glm::vec3 worldCorner = glm::vec3(worldTransform * glm::vec4(corner, 1.0f));
			outWorldMin = glm::min(outWorldMin, worldCorner);
			outWorldMax = glm::max(outWorldMax, worldCorner);
		}
	}

	bool IntersectRayAABB(
		const glm::vec3& rayOrigin,
		const glm::vec3& rayDirection,
		const glm::vec3& boundsMin,
		const glm::vec3& boundsMax,
		float& outT)
	{
		float tMin = 0.0f;
		float tMax = std::numeric_limits<float>::max();

		for (int axis = 0; axis < 3; ++axis)
		{
			const float origin = rayOrigin[axis];
			const float direction = rayDirection[axis];
			const float minPlane = boundsMin[axis];
			const float maxPlane = boundsMax[axis];

			if (std::abs(direction) < 1e-6f)
			{
				if (origin < minPlane || origin > maxPlane)
				{
					return false;
				}

				continue;
			}

			const float invDir = 1.0f / direction;
			float t1 = (minPlane - origin) * invDir;
			float t2 = (maxPlane - origin) * invDir;

			if (t1 > t2)
			{
				std::swap(t1, t2);
			}

			tMin = std::max(tMin, t1);
			tMax = std::min(tMax, t2);

			if (tMin > tMax)
			{
				return false;
			}
		}

		outT = tMin;
		return true;
	}
}

static glm::mat4 BuildRotationMatrix(const Nyx::TransformComponent& transform)
{
	return glm::toMat4(glm::normalize(transform.Rotation));
}

static glm::vec3 GetForwardVector(const Nyx::TransformComponent& transform)
{
	const glm::mat4 rotation = BuildRotationMatrix(transform);
	return glm::normalize(glm::vec3(rotation * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
}

static glm::vec3 GetRightVector(const Nyx::TransformComponent& transform)
{
	const glm::mat4 rotation = BuildRotationMatrix(transform);
	return glm::normalize(glm::vec3(rotation * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)));
}

namespace
{
	glm::vec3 GetForwardVectorWS(const Nyx::TransformComponent& transform)
	{
		const glm::mat4 world = transform.ToMatrix();

		// Local forward = -Z
		const glm::vec3 forward = glm::normalize(glm::vec3(world * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
		return forward;
	}
}

namespace
{
	// Milliseconds since `start`, for the frame's step times
	float MillisecondsSince(std::chrono::steady_clock::time_point start)
	{
		return std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
	}

	bool LoadImageDataRGBA(const std::filesystem::path& path, Nyx::ImageData& outImageData)
	{
		outImageData = {};

		int width = 0;
		int height = 0;
		int channels = 0;

		stbi_uc* pixels = stbi_load(path.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
		if (!pixels)
		{
			return false;
		}

		const size_t imageSize = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;

		outImageData.Width = width;
		outImageData.Height = height;
		outImageData.Channels = 4;
		outImageData.Pixels.resize(imageSize);
		std::memcpy(outImageData.Pixels.data(), pixels, imageSize);

		stbi_image_free(pixels);
		return true;
	}
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
		ASSERT(Swapchain.GetMinImageCount() >= 2 && "Failed to fulfill VulkanImGuiBackend requirements.");
		ASSERT(Swapchain.GetImageCount() >= Swapchain.GetMinImageCount() && "Failed to fulfill VulkanImGuiBackend requirements.");
		UI.InitializeImGui(window);

		// Before any pipeline is made
		Context.LoadPipelineCache(Nyx::Paths::GetShaderCacheDir() / "Pipelines.bin");
		StartLiveShaderReload();

		// ------------------------------------------------------------------------------------------------------
		// CPU / GPU resources needed by rendering
		// ------------------------------------------------------------------------------------------------------
		// The global set: every texture, the fixed samplers, the material buffer
		Bindless.Initialize(Context);

		CreateTestMeshData();

		// Shared layouts / shared GPU assets that per-view creation depends on
		CreateSceneDescriptorSetLayout();
		CreateScenePipelineLayout();
		DebugLines.Initialize(*ScenePipelineLayout, [this](SceneViewInstance& view, vk::raii::CommandBuffer& cmd)
			{
				BindSceneSets(view, cmd);
			});
		Overlays.Initialize(*ScenePipelineLayout, [this](SceneViewInstance& view, vk::raii::CommandBuffer& cmd)
			{
				BindSceneSets(view, cmd);
			},
			PlaneMesh);

		LoadSkyboxCubemap();
		// Its light: the specular cube, the irradiance, the BRDF table
		Environment.Initialize(Context, Bindless, Programs);
		Environment.Build(SkyboxTextureIndex);

		// First scene view: creates per-view resources
		CreateSceneView();

		// Pipelines that depend on view-owned render passes / framebuffers
		DebugLines.CreatePipeline(SceneViews.front());
		Overlays.CreatePipelines(SceneViews.front());

		// Shared pipelines
		CreateSkyboxPipeline();
		CreateTonemapPipeline();

		// 2D shapes, the scene views' blur for glows and glass, and the frame's GPU times
		UI.InitializePasses();

		// The meshes' materials, loaded from their files when first asked for
		Materials.Initialize(Context, Bindless, Programs, Nyx::Paths::GetAssetSearchDirs(), *ScenePipelineLayout, [this]
			{
				return GetSceneRenderPass();
			},
			[this]
			{
				return GetPickingRenderPass();
			});
	}

	void VulkanRenderer::Shutdown()
	{
		Context.WaitIdle();
		Context.SavePipelineCache(Nyx::Paths::GetShaderCacheDir() / "Pipelines.bin");

		for (SceneViewInstance& view : SceneViews)
		{
			view.SceneDescriptorSets = vk::raii::DescriptorSets{ nullptr };
			view.SceneDescriptorPool = nullptr;
			view.SceneUniformBuffer = nullptr;
			view.SceneUniformBufferMemory = nullptr;

			UI.ReleaseSceneViewBlur(view);
			view.RenderTarget.Shutdown(Context);
		}
		SceneViews.clear();
		UI.Shutdown();

		Swapchain.Shutdown();

		// destroy command buffers / semaphores / fences / pools
		CommandBuffers.clear();
		RenderFinishedSemaphores.clear();
		ImageAvailableSemaphore = nullptr;
		InFlightFence = nullptr;
		CommandPool = nullptr;

		Materials.Shutdown();
		Environment.Shutdown();
		Bindless.Shutdown();
		Context.Shutdown();
	}

	void VulkanRenderer::OnFramebufferResized()
	{
		bRecreateSwapChain = true;
	}

	void VulkanRenderer::SetVSync(bool bVSync)
	{
		if (bVSync == Swapchain.IsVSync())
		{
			return;
		}
		// The present mode is the swapchain's: one made from now on has it, and one that exists
		// already is made again after this frame.
		Swapchain.SetVSync(bVSync);
		if (*Swapchain.GetSwapchain())
		{
			bRecreateSwapChain = true;
		}
	}

	bool VulkanRenderer::IsVSync() const
	{
		return Swapchain.IsVSync();
	}

	void VulkanRenderer::SetupVulkan(const char* applicationName, GLFWwindow* window)
	{
		Context.Initialize(applicationName, window);
		Swapchain.Initialize(Context, window);

		CreateGraphicsPipeline();

		// @todo: check order
		CreateCommandPool();
		CreateCommandBuffers();
		CreateSyncObjects();
	}

	void VulkanRenderer::DrawFrame(const std::function<void()>& buildUI)
	{
		if (bDeviceLost)
		{
			return;
		}

		try
		{
			DrawFrameOnDevice(buildUI);
		}
		catch (const vk::DeviceLostError& error)
		{
			// The driver reset the GPU, e.g. after a fault (Windows logs nvlddmkm event 153 and a
			// TDR). The device and everything made on it are gone, and the renderer can't make
			// them anew, so it draws nothing more and the window closes (IsDeviceLost()).
			bDeviceLost = true;
			LOG_ERROR("The GPU device was lost while drawing a frame ({}). Nothing more can be drawn; the window closes.", error.what());
		}
		LastFrameEnd = std::chrono::steady_clock::now();
	}

	void VulkanRenderer::DrawFrameOnDevice(const std::function<void()>& buildUI)
	{
		NYX_PROFILE_FUNCTION();
		FrameClock clock = StartFrameClock();
		WaitForLastFrame(clock);

		// From here to the end, the frame's commands are recorded
		bRecordingFrame = true;
		struct RecordingEnds
		{
			bool& bRecording;
			~RecordingEnds()
			{
				bRecording = false;
			}
		} recordingEnds{ bRecordingFrame };

		{
			NYX_PROFILE_SCOPE("Read back last frame");
			UI.ReadLastFrame();
			ReadBackPickResults();
		}

		const std::optional<uint32_t> imageIndex = AcquireImage(clock);
		if (!imageIndex)
		{
			return;
		}

		// Shaders saved since the last frame, compiled again; their pipelines are made again
		{
			NYX_PROFILE_SCOPE("Shader updates");
			ApplyShaderUpdates();
		}

		vk::raii::CommandBuffer& cmd = BeginCommands(*imageIndex);
		RenderScene(cmd, clock);
		// No frame is in flight, and this one's draws run after it is submitted: values set while
		// building the UI show in it
		UI.BuildUI(cmd, [&]()
			{
				buildUI();
				ApplyPendingMaterialValues();
			}, clock.Times);
		UI.RecordUI(cmd, *imageIndex);
		UI.EndCommands(*cmd);
		cmd.end();

		Submit(cmd, *imageIndex);
		Present(*imageIndex, clock);
	}

	VulkanRenderer::FrameClock VulkanRenderer::StartFrameClock() const
	{
		// The frame's steps are timed for the painter's statistics, the frame itself at its end.
		FrameClock clock;
		clock.Start = std::chrono::steady_clock::now();
		// Since the last frame's DrawFrame() ended: the events, the layers' updates and the frame
		// rate cap
		clock.Times.Outside = LastFrameEnd == std::chrono::steady_clock::time_point{} ? 0.0f
			: std::chrono::duration<float, std::milli>(clock.Start - LastFrameEnd).count();
		return clock;
	}

	void VulkanRenderer::WaitForLastFrame(FrameClock& clock)
	{
		NYX_PROFILE_FUNCTION();
		const vk::Result fenceResult = Context.GetDevice().waitForFences({ *InFlightFence }, true, UINT64_MAX);
		(void)fenceResult;
		clock.Times.WaitForGpu = MillisecondsSince(clock.Start);

		// Views let go of during the last frame, which the GPU is done with now
		for (const uint64_t id : std::exchange(SceneViewsToDestroy, {}))
		{
			DestroySceneViewNow(id);
		}
	}

	std::optional<uint32_t> VulkanRenderer::AcquireImage(FrameClock& clock)
	{
		NYX_PROFILE_FUNCTION();
		const std::chrono::steady_clock::time_point acquireStart = std::chrono::steady_clock::now();
		const vk::ResultValue<uint32_t> acquireResult =
			Swapchain.GetSwapchain().acquireNextImage(UINT64_MAX, *ImageAvailableSemaphore);
		clock.Times.Acquire = MillisecondsSince(acquireStart);

		if (acquireResult.result == vk::Result::eErrorOutOfDateKHR)
		{
			RecreateSwapChain();
			return std::nullopt;
		}

		if (acquireResult.result != vk::Result::eSuccess &&
			acquireResult.result != vk::Result::eSuboptimalKHR)
		{
			throw std::runtime_error("Failed to acquire swapchain image.");
		}

		// Only reset once we know we will submit work
		Context.GetDevice().resetFences({ *InFlightFence });
		return acquireResult.value;
	}

	vk::raii::CommandBuffer& VulkanRenderer::BeginCommands(uint32_t imageIndex)
	{
		vk::raii::CommandBuffer& cmd = CommandBuffers[imageIndex];
		cmd.reset();

		vk::CommandBufferBeginInfo beginInfo{};
		cmd.begin(beginInfo);
		UI.BeginCommands(*cmd);
		return cmd;
	}

	void VulkanRenderer::RenderScene(vk::raii::CommandBuffer& cmd, FrameClock& clock)
	{
		NYX_PROFILE_FUNCTION();
		const std::chrono::steady_clock::time_point sceneStart = std::chrono::steady_clock::now();

		// Each shown world's state, once per frame, for all views that show it
		SceneTime = std::chrono::duration<float>(sceneStart - StartTime).count();
		ExtractRenderWorlds();

		// Every scene view into its own offscreen target: first the editor's own passes of all
		// views, then the views, so that each is timed in one go
		{
			NYX_PROFILE_SCOPE("Editor passes");
			UI.WriteUITimestamp(*cmd, VulkanUIBackend::EditorPassesPair, false);
			DrawEditorPasses(cmd);
			UI.WriteUITimestamp(*cmd, VulkanUIBackend::EditorPassesPair, true);
		}

		{
			NYX_PROFILE_SCOPE("Scene views");
			UI.WriteUITimestamp(*cmd, VulkanUIBackend::SceneViewsPair, false);
			for (SceneViewInstance& view : SceneViews)
			{
				RenderSceneView(view, cmd);
			}
			UI.WriteUITimestamp(*cmd, VulkanUIBackend::SceneViewsPair, true);
		}

		// Each picture blurred small, before the UI is built, so that the UI shows this frame's blur
		UI.BlurSceneViews(cmd, SceneViews);
		clock.Times.Scene = MillisecondsSince(sceneStart);
	}

	void VulkanRenderer::DrawEditorPasses(vk::raii::CommandBuffer& cmd)
	{
		for (SceneViewInstance& view : SceneViews)
		{
			if (view.bResizePending)
			{
				view.RenderTarget.Recreate(
					Context,
					std::max(1u, view.PendingWidth),
					std::max(1u, view.PendingHeight),
					vk::Format::eR8G8B8A8Unorm);

				RegisterHdrPicture(view);
				Overlays.RecreateViewResources(view);

				view.bResizePending = false;
				view.bRecreatedThisFrame = true;
			}
			else
			{
				view.bRecreatedThisFrame = false;
			}

			if (const RenderWorld* world = FindShownWorld(view))
			{
				UpdateViewportSceneGlobals(view, *world->Registry);
				UpdateSceneUniforms(view);

				// Editor-only passes: picking, and the masks the selection outline is drawn from
				if (view.bShowEditorOverlays)
				{
					Overlays.DrawPasses(view, *world, cmd);
				}
			}
		}
	}

	void VulkanRenderer::ApplyPendingMaterialValues()
	{
		for (const auto& [path, asset] : PendingMaterialValues)
		{
			Materials.SetValues(path, asset);
		}
		PendingMaterialValues.clear();
	}

	void VulkanRenderer::Submit(vk::raii::CommandBuffer& cmd, uint32_t imageIndex)
	{
		NYX_PROFILE_FUNCTION();
		const vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;

		vk::SubmitInfo submitInfo{};
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &*ImageAvailableSemaphore;
		submitInfo.pWaitDstStageMask = &waitStage;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &*cmd;
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &*RenderFinishedSemaphores[imageIndex];

		Context.GetGraphicsQueue().submit(submitInfo);

		// ImGui windows outside the main window render in command buffers ImGui submits itself.
		// After this frame's submission, the images they show (scene views, painter canvases) are
		// painted first, in queue order; before it, they would show last frame's or none at all.
		UI.RenderPlatformWindows();

		// The frame's fence goes in last, with an empty submission: a fence signalled by a queue
		// submission waits for everything submitted before it on the queue. So it covers this
		// frame's command buffer and the OS windows' ones, which also read painter canvases and
		// scene views. The next frame waits for it before it rewrites any of them.
		Context.GetGraphicsQueue().submit(vk::ArrayProxy<const vk::SubmitInfo>{}, *InFlightFence);
		Bindless.EndFrame();
	}

	void VulkanRenderer::Present(uint32_t imageIndex, const FrameClock& clock)
	{
		NYX_PROFILE_FUNCTION();
		vk::PresentInfoKHR presentInfo{};
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &*RenderFinishedSemaphores[imageIndex];
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &*Swapchain.GetSwapchain();
		presentInfo.pImageIndices = &imageIndex;

		UI.BeforePresent();
		const vk::Result presentResult = Context.GetGraphicsQueue().presentKHR(presentInfo);
		UI.AfterPresent(clock.Times.Outside + MillisecondsSince(clock.Start));
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

	void VulkanRenderer::SetSelectedEntity(std::optional<Nyx::Entity> entity)
	{
		Overlays.SetSelectedEntity(entity);
	}

	void VulkanRenderer::WaitIdle()
	{
		Context.WaitIdle();
	}

	bool VulkanRenderer::IsDeviceLost() const
	{
		return bDeviceLost;
	}

	void VulkanRenderer::SetSceneViewCameraMode(uint64_t id, EViewportCameraMode mode)
	{
		if (SceneViewInstance* view = FindSceneView(id))
		{
			view->CameraMode = mode;
		}
	}

	void VulkanRenderer::SetSceneViewShowEditorOverlays(uint64_t id, bool bShow)
	{
		if (SceneViewInstance* view = FindSceneView(id))
		{
			view->bShowEditorOverlays = bShow;
			if (!bShow)
			{
				view->Overlays.ResetPicking();
			}
		}
	}

	void VulkanRenderer::SetSceneViewExposure(uint64_t id, float stops)
	{
		if (SceneViewInstance* view = FindSceneView(id))
		{
			view->ExposureStops = stops;
		}
	}

	void VulkanRenderer::SetSceneViewSky(uint64_t id, ESceneViewSky sky, float rotationRadians)
	{
		if (SceneViewInstance* view = FindSceneView(id))
		{
			GetSkySlot(sky);
			view->Sky = sky;
			view->SkyRotation = rotationRadians;
		}
	}

	void VulkanRenderer::SetSceneViewDebug(uint64_t id, ESceneViewDebug debugView)
	{
		if (SceneViewInstance* view = FindSceneView(id))
		{
			view->Debug = debugView;
		}
	}

	void VulkanRenderer::SetSceneViewEditorCameraTransform(uint64_t id, const glm::vec3& pos, const glm::vec3& rot)
	{
		SceneViewInstance* view = FindSceneView(id);
		if (!view)
		{
			return;
		}

		view->EditorCam.Position = pos;
		view->EditorCam.RotationRadians = rot;
	}

	bool VulkanRenderer::GetSceneViewEditorCameraTransform(uint64_t id, glm::vec3& outPos, glm::vec3& outRot) const
	{
		const SceneViewInstance* view = FindSceneView(id);
		if (!view)
		{
			return false;
		}

		outPos = view->EditorCam.Position;
		outRot = view->EditorCam.RotationRadians;
		return true;
	}

	bool VulkanRenderer::GetSceneViewCameraData(uint64_t sceneViewId, Nyx::SceneViewCameraData& outData) const
	{
		const SceneViewInstance* view = FindSceneView(sceneViewId);
		if (!view)
		{
			return false;
		}

		const vk::Extent2D extent = view->RenderTarget.GetExtent();

		outData.View = view->SceneGlobals.View;
		outData.Projection = view->SceneGlobals.Projection;
		outData.ViewProjection = view->SceneGlobals.ViewProjection;
		outData.InverseViewProjection = glm::inverse(view->SceneGlobals.ViewProjection);
		outData.CameraWorldPos = view->SceneGlobals.CameraWorldPos;
		outData.Extent = Extent2D{ extent.width, extent.height };

		return true;
	}

	void VulkanRenderer::SetWorld(const Nyx::Registry* world)
	{
		World = world;
		BeginWorldRevision(world);
		Overlays.SetWorld(world);

		// The views of the world before and of this one; those of worlds of their own are left be
		for (SceneViewInstance& view : SceneViews)
		{
			if (!view.OwnWorld || view.OwnWorld == world)
			{
				view.Overlays.ResetPicking();
			}
		}
	}

	void VulkanRenderer::SetSceneViewWorld(uint64_t id, const Nyx::Registry* world)
	{
		if (SceneViewInstance* view = FindSceneView(id))
		{
			view->OwnWorld = world;
			BeginWorldRevision(world);
			view->Overlays.ResetPicking();
		}
	}

	void VulkanRenderer::BeginWorldRevision(const Nyx::Registry* registry)
	{
		if (!registry)
		{
			return;
		}

		RenderWorld* world = FindRenderWorld(registry);
		if (!world)
		{
			world = RenderWorlds.emplace_back(std::make_unique<RenderWorld>()).get();
			world->Registry = registry;
		}
		world->Revision = ++LastWorldRevision;
		// Its contents may be new: nothing of the old ones is drawn or picked from
		world->Objects.clear();
		world->Decals.clear();
		world->PickingIdToEntity.clear();
		world->OverlayLines = {};
		world->DebugDrawLines = {};
	}

	void VulkanRenderer::SetWorldDebugDraw(const Nyx::Registry* world, const Nyx::DebugDraw* drawing)
	{
		if (!world)
		{
			return;
		}
		if (drawing)
		{
			WorldDebugDraws[world] = drawing;
		}
		else
		{
			WorldDebugDraws.erase(world);
		}
	}

	void VulkanRenderer::SetSceneViewShowDebugDrawing(uint64_t id, bool bShow)
	{
		if (SceneViewInstance* view = FindSceneView(id))
		{
			view->bShowDebugDrawing = bShow;
		}
	}

	void VulkanRenderer::SetWorldInterpolation(const Nyx::Registry* world, const Nyx::FixedStepClock* clock)
	{
		if (!world)
		{
			return;
		}
		if (clock)
		{
			WorldInterpolationClocks[world] = clock;
		}
		else
		{
			WorldInterpolationClocks.erase(world);
		}
	}

	RenderWorld* VulkanRenderer::FindRenderWorld(const Nyx::Registry* registry)
	{
		return const_cast<RenderWorld*>(std::as_const(*this).FindRenderWorld(registry));
	}

	const RenderWorld* VulkanRenderer::FindRenderWorld(const Nyx::Registry* registry) const
	{
		if (!registry)
		{
			return nullptr;
		}
		for (const std::unique_ptr<RenderWorld>& world : RenderWorlds)
		{
			if (world->Registry == registry)
			{
				return world.get();
			}
		}
		return nullptr;
	}

	const RenderWorld* VulkanRenderer::FindShownWorld(const SceneViewInstance& view) const
	{
		return FindRenderWorld(view.OwnWorld ? view.OwnWorld : World);
	}

	uint64_t VulkanRenderer::GetShownWorldRevision(const SceneViewInstance& view) const
	{
		const RenderWorld* world = FindShownWorld(view);
		return world ? world->Revision : 0;
	}

	Mesh* VulkanRenderer::GetCubeMesh()
	{
		return &CubeMesh;
	}

	Mesh* VulkanRenderer::GetSphereMesh()
	{
		return &SphereMesh;
	}

	Mesh* VulkanRenderer::GetCapsuleMesh()
	{
		return &CapsuleMesh;
	}

	Mesh* VulkanRenderer::GetPlaneMesh()
	{
		return &PlaneMesh;
	}

	void VulkanRenderer::SetSky(uint32_t cubeSlot)
	{
		if (SkyboxTextureIndex != cubeSlot && SkyboxTextureIndex != StudioSkySlot && SkyboxTextureIndex != BlackSkySlot)
		{
			Environment.Forget(SkyboxTextureIndex);
		}
		SkyboxTextureIndex = cubeSlot;
		Environment.Build(cubeSlot);
	}

	uint32_t VulkanRenderer::GetSkySlot(ESceneViewSky sky)
	{
		// A cube of half-float colours, each texel's from its direction (Vulkan's cube faces: +X,
		// -X, +Y, -Y, +Z, -Z)
		const auto makeSky = [this](uint32_t size, const std::function<glm::vec3(glm::vec3)>& light)
		{
			std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 6 * 4 * sizeof(uint16_t));
			uint16_t* texels = reinterpret_cast<uint16_t*>(pixels.data());
			for (uint32_t face = 0; face < 6; ++face)
			{
				for (uint32_t y = 0; y < size; ++y)
				{
					for (uint32_t x = 0; x < size; ++x)
					{
						const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
						const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
						const std::array<glm::vec3, 6> directions = { glm::vec3(1.0f, -v, -u), glm::vec3(-1.0f, -v, u), glm::vec3(u, 1.0f, v),
							glm::vec3(u, -1.0f, -v), glm::vec3(u, -v, 1.0f), glm::vec3(-u, -v, -1.0f) };
						const glm::vec3 color = light(glm::normalize(directions[face]));
						uint16_t* texel = texels + ((static_cast<size_t>(face) * size + y) * size + x) * 4;
						for (int channel = 0; channel < 4; ++channel)
						{
							texel[channel] = glm::packHalf1x16(channel < 3 ? color[channel] : 1.0f);
						}
					}
				}
			}
			const uint32_t slot = Bindless.CreateCubeTexture(size, pixels, vk::Format::eR16G16B16A16Sfloat);
			Environment.Build(slot);
			return slot;
		};

		switch (sky)
		{
		case ESceneViewSky::Studio:
			if (StudioSkySlot == 0)
			{
				// A grey room, lighter above than below, with a large soft key light above to the
				// front right, a fill light from the left and a rim light from high behind, out of
				// the way of what the camera usually sees behind the shape
				StudioSkySlot = makeSky(64, [](glm::vec3 d)
					{
						const auto softbox = [&d](glm::vec3 towards, float spread, float strength)
						{
							return std::pow(std::max(glm::dot(d, glm::normalize(towards)), 0.0f), spread) * strength;
						};
						const float room = glm::mix(0.08f, 0.45f, glm::smoothstep(-0.6f, 0.8f, d.y));
						const float lights = softbox(glm::vec3(0.6f, 0.7f, 0.5f), 24.0f, 6.0f) + softbox(glm::vec3(-0.9f, 0.3f, 0.4f), 12.0f, 1.2f) + softbox(glm::vec3(-0.4f, 1.0f, -0.5f), 32.0f, 2.0f);
						return glm::vec3(room + lights);
					});
			}
			return StudioSkySlot;
		case ESceneViewSky::Black:
			if (BlackSkySlot == 0)
			{
				BlackSkySlot = makeSky(4, [](glm::vec3)
					{
						return glm::vec3(0.0f);
					});
			}
			return BlackSkySlot;
		default:
			return SkyboxTextureIndex;
		}
	}

	Material* VulkanRenderer::GetMaterial(const std::string& path)
	{
		return Materials.Get(path);
	}

	bool VulkanRenderer::IsCompilingShaders() const
	{
		return Programs.IsBusy() || Materials.IsWaiting();
	}

	MaterialView VulkanRenderer::ViewMaterial(const std::string& path)
	{
		const Nyx::VulkanMaterials::View view = Materials.Inspect(path);
		return MaterialView{ .Asset = view.Asset, .Inherited = view.Inherited, .Shader = view.Shader, .Layout = view.Layout, .Defaults = view.Defaults, .FileRevision = view.FileRevision };
	}

	void VulkanRenderer::SetMaterialValues(const std::string& path, const Nyx::MaterialAsset& asset)
	{
		PendingMaterialValues.insert_or_assign(path, asset);
	}

	VulkanContext& VulkanRenderer::GetContext()
	{
		return Context;
	}

	VulkanSwapchain& VulkanRenderer::GetSwapchain()
	{
		return Swapchain;
	}

	void VulkanRenderer::CreateGraphicsPipeline()
	{
		// @todo:
	}

	void VulkanRenderer::CreateCommandPool()
	{
		vk::CommandPoolCreateInfo poolInfo{};
		poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
		poolInfo.queueFamilyIndex = Context.GetGraphicsQueueFamily();

		CommandPool = vk::raii::CommandPool(Context.GetDevice(), poolInfo);
	}

	void VulkanRenderer::CreateCommandBuffers()
	{
		CommandBuffers.clear();

		vk::CommandBufferAllocateInfo allocInfo{};
		allocInfo.commandPool = *CommandPool;
		allocInfo.level = vk::CommandBufferLevel::ePrimary;
		allocInfo.commandBufferCount = Swapchain.GetImageCount();

		CommandBuffers = vk::raii::CommandBuffers(Context.GetDevice(), allocInfo);
	}

	void VulkanRenderer::CreateSyncObjects()
	{
		vk::SemaphoreCreateInfo semaphoreInfo{};
		ImageAvailableSemaphore = vk::raii::Semaphore(Context.GetDevice(), semaphoreInfo);

		RenderFinishedSemaphores.clear();
		RenderFinishedSemaphores.reserve(Swapchain.GetImageCount());
		for (size_t i = 0; i < Swapchain.GetImageCount(); ++i)
		{
			RenderFinishedSemaphores.emplace_back(Context.GetDevice(), semaphoreInfo);
		}

		vk::FenceCreateInfo fenceInfo{};
		fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled;
		InFlightFence = vk::raii::Fence(Context.GetDevice(), fenceInfo);
	}

	void VulkanRenderer::RecreateSwapChain()
	{
		// A minimized window has no size to create a swapchain for. Instead of waiting here until
		// it is restored (which would stop the whole frame loop, e.g. the editor link), keep the
		// request: the window doesn't draw while minimized, and the first frame after restoring
		// comes back here.
		if (!HasValidFramebufferSize())
		{
			bRecreateSwapChain = true;
			return;
		}

		Context.GetDevice().waitIdle();
		Swapchain.Recreate(Context, Window);

		CreateCommandBuffers();
		//UI.OnSwapchainChanged() // @todo:

		bRecreateSwapChain = false;
	}

	bool VulkanRenderer::HasValidFramebufferSize() const
	{
		int width = 0;
		int height = 0;
		glfwGetFramebufferSize(Window, &width, &height);
		return width > 0 && height > 0;
	}

	void VulkanRenderer::ExtractRenderWorlds()
	{
		// Before reading any: the registry of a world no view shows may be gone
		std::erase_if(RenderWorlds, [this](const std::unique_ptr<RenderWorld>& world)
			{
				if (world->Registry == World)
				{
					return false;
				}
				return std::none_of(SceneViews.begin(), SceneViews.end(), [&world](const SceneViewInstance& view)
					{
						return view.OwnWorld == world->Registry;
					});
			});

		DebugLines.Reset();
		for (const std::unique_ptr<RenderWorld>& world : RenderWorlds)
		{
			// Used to update Render Objects here (e.g. Transform)
			const std::optional<float> interpolation = GetWorldInterpolation(world->Registry);
			ExtractRenderObjects(*world, interpolation);
			ExtractSurfaceDecals(*world->Registry, interpolation, world->Decals);
			// Picked as the objects are: by their entity, with the ids after the objects'
			for (RenderDecal& decal : world->Decals)
			{
				world->PickingIdToEntity.push_back(decal.SourceEntity);
				decal.PickingId = static_cast<uint32_t>(world->PickingIdToEntity.size());
			}
			Overlays.AddDebugLines(*world);

			const auto drawing = WorldDebugDraws.find(world->Registry);
			world->DebugDrawLines = drawing != WorldDebugDraws.end() ? DebugLines.Add(*drawing->second) : DebugLineRange{};
		}
		DebugLines.Upload();
	}

	std::optional<float> VulkanRenderer::GetWorldInterpolation(const Nyx::Registry* registry) const
	{
		const auto clock = WorldInterpolationClocks.find(registry);
		if (clock == WorldInterpolationClocks.end())
		{
			return std::nullopt;
		}
		return static_cast<float>(clock->second->GetDrawFactor());
	}

	void VulkanRenderer::ExtractRenderObjects(RenderWorld& world, std::optional<float> interpolation)
	{
		world.Objects.clear();
		world.PickingIdToEntity.clear();

		const Nyx::Registry& registry = *world.Registry;

		const_cast<Nyx::Registry&>(registry).Each<Nyx::MeshRendererComponent>(
			[&world, &registry, interpolation](Nyx::Entity entity, Nyx::MeshRendererComponent& meshRenderer)
			{
				if (!meshRenderer.bVisible)
				{
					return;
				}

				if (!meshRenderer.MeshAsset || !meshRenderer.MaterialAsset)
				{
					return;
				}

				glm::mat4 worldTransform = glm::mat4(1.0f);
				glm::mat4 drawTransform = glm::mat4(1.0f);

				if (registry.Has<Nyx::TransformComponent>(entity))
				{
					worldTransform = registry.Get<Nyx::TransformComponent>(entity).ToMatrix();
					drawTransform = Nyx::GetDrawnTransform(registry, entity, interpolation).ToMatrix();
				}

				RenderObject obj{};
				obj.SourceEntity = entity;
				obj.MeshAsset = meshRenderer.MeshAsset;
				obj.MaterialAsset = meshRenderer.MaterialAsset;
				obj.WorldTransform = worldTransform;
				obj.DrawTransform = drawTransform;
				obj.LocalBoundsMin = meshRenderer.MeshAsset->GetLocalBoundsMin();
				obj.LocalBoundsMax = meshRenderer.MeshAsset->GetLocalBoundsMax();

				// Dense picking ID: 1..N, with 0 reserved for "nothing"
				world.PickingIdToEntity.push_back(entity);
				obj.PickingId = static_cast<uint32_t>(world.PickingIdToEntity.size());

				world.Objects.push_back(obj);
			});
	}

	void VulkanRenderer::DrawRenderObjects(SceneViewInstance& view, const RenderWorld& world, vk::raii::CommandBuffer& cmd)
	{
		BindSceneSets(view, cmd);
		for (const RenderObject& obj : world.Objects)
		{
			if (!obj.MeshAsset || !obj.MaterialAsset)
			{
				continue;
			}

			cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **obj.MaterialAsset->Pipeline);

			ObjectConstants object{};
			object.Model = obj.DrawTransform;
			object.MaterialOffset = obj.MaterialAsset->MaterialOffset;
			object.EntityId = obj.PickingId;
			cmd.pushConstants<ObjectConstants>(*ScenePipelineLayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, { object });

			vk::DeviceSize offsets[] = { 0 };
			cmd.bindVertexBuffers(0, { obj.MeshAsset->GetVertexBuffer() }, offsets);
			cmd.bindIndexBuffer(obj.MeshAsset->GetIndexBuffer(), 0, vk::IndexType::eUint32);
			cmd.drawIndexed(obj.MeshAsset->GetIndexCount(), 1, 0, 0, 0);
		}
	}

	void VulkanRenderer::DrawRenderDecals(SceneViewInstance& view, const RenderWorld& world, vk::raii::CommandBuffer& cmd)
	{
		if (world.Decals.empty())
		{
			return;
		}

		BindSceneSets(view, cmd);
		vk::DeviceSize offsets[] = { 0 };
		cmd.bindVertexBuffers(0, { PlaneMesh.GetVertexBuffer() }, offsets);
		cmd.bindIndexBuffer(PlaneMesh.GetIndexBuffer(), 0, vk::IndexType::eUint32);
		for (const RenderDecal& decal : world.Decals)
		{
			cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **decal.MaterialAsset->DecalPipeline);

			ObjectConstants object{};
			object.Model = decal.Model;
			object.MaterialOffset = decal.MaterialAsset->MaterialOffset;
			object.CustomData = decal.CustomData;
			object.CustomData2 = decal.CustomData2;
			cmd.pushConstants<ObjectConstants>(*ScenePipelineLayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, { object });
			cmd.drawIndexed(PlaneMesh.GetIndexCount(), 1, 0, 0, 0);
		}
	}

	void VulkanRenderer::CreateSceneDescriptorSetLayout()
	{
		vk::DescriptorSetLayoutBinding uboBinding{};
		uboBinding.binding = 0;
		uboBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
		uboBinding.descriptorCount = 1;
		uboBinding.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

		vk::DescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.bindingCount = 1;
		layoutInfo.pBindings = &uboBinding;

		SceneDescriptorSetLayout = vk::raii::DescriptorSetLayout(Context.GetDevice(), layoutInfo);
	}

	void VulkanRenderer::CreateScenePipelineLayout()
	{
		vk::PushConstantRange pushConstantRange{};
		pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
		pushConstantRange.offset = 0;
		pushConstantRange.size = sizeof(ObjectConstants);

		const std::array<vk::DescriptorSetLayout, 2> setLayouts = { *Bindless.GetSetLayout(), *SceneDescriptorSetLayout };
		vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
		pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
		pipelineLayoutInfo.pSetLayouts = setLayouts.data();
		pipelineLayoutInfo.pushConstantRangeCount = 1;
		pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

		ScenePipelineLayout = vk::raii::PipelineLayout(Context.GetDevice(), pipelineLayoutInfo);
	}

	void VulkanRenderer::BindSceneSets(SceneViewInstance& view, vk::raii::CommandBuffer& cmd)
	{
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *ScenePipelineLayout, 0, { Bindless.GetSet(), *view.SceneDescriptorSets.front() }, {});
	}

	vk::RenderPass VulkanRenderer::GetSceneRenderPass() const
	{
		ASSERT(!SceneViews.empty() && "No SceneViews exist yet.");
		return *SceneViews.front().RenderTarget.GetSceneRenderPass();
	}

	vk::RenderPass VulkanRenderer::GetPickingRenderPass() const
	{
		ASSERT(!SceneViews.empty() && "No SceneViews exist yet.");
		return *SceneViews.front().Overlays.PickingRenderPass;
	}

	vk::RenderPass VulkanRenderer::GetPictureRenderPass() const
	{
		ASSERT(!SceneViews.empty() && "No SceneViews exist yet.");
		return *SceneViews.front().RenderTarget.GetRenderPass();
	}

	void VulkanRenderer::RegisterHdrPicture(SceneViewInstance& view)
	{
		Bindless.RemoveTexture(view.HdrTextureSlot);
		view.HdrTextureSlot = Bindless.AddTexture(view.RenderTarget.GetHdrImageView());
	}

	void VulkanRenderer::CreateTonemapPipeline()
	{
		const std::string vertexName = "tonemapVertex";
		const std::string fragmentName = "tonemapFragment";
		const std::vector<uint32_t>& vertCode = Programs.GetSpirv("Tonemap.slang", vertexName);
		const std::vector<uint32_t>& fragCode = Programs.GetSpirv("Tonemap.slang", fragmentName);
		vk::raii::ShaderModule vertShaderModule = CreateShaderModule(Context, vertCode);
		vk::raii::ShaderModule fragShaderModule = CreateShaderModule(Context, fragCode);
		const std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages = {
			vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex, *vertShaderModule, vertexName.c_str()),
			vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, *fragShaderModule, fragmentName.c_str()),
		};

		// One triangle over the view, no vertices, no depth
		vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
		vk::PipelineInputAssemblyStateCreateInfo inputAssembly({}, vk::PrimitiveTopology::eTriangleList);
		vk::PipelineViewportStateCreateInfo viewportState{};
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;
		vk::PipelineRasterizationStateCreateInfo rasterizer{};
		rasterizer.polygonMode = vk::PolygonMode::eFill;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = vk::CullModeFlagBits::eNone;
		vk::PipelineMultisampleStateCreateInfo multisampling{};
		multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;
		vk::PipelineDepthStencilStateCreateInfo depthStencil{};
		vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
		colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
		vk::PipelineColorBlendStateCreateInfo colorBlending{};
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &colorBlendAttachment;
		const std::array<vk::DynamicState, 2> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
		vk::PipelineDynamicStateCreateInfo dynamicState({}, dynamicStates);

		vk::GraphicsPipelineCreateInfo pipelineInfo{};
		pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineInfo.pStages = shaderStages.data();
		pipelineInfo.pVertexInputState = &vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState = &multisampling;
		pipelineInfo.pDepthStencilState = &depthStencil;
		pipelineInfo.pColorBlendState = &colorBlending;
		pipelineInfo.pDynamicState = &dynamicState;
		pipelineInfo.layout = *ScenePipelineLayout;
		pipelineInfo.renderPass = GetPictureRenderPass();
		pipelineInfo.subpass = 0;

		TonemapPipeline = vk::raii::Pipeline(Context.GetDevice(), Context.GetPipelineCache(), pipelineInfo);
	}

	void VulkanRenderer::DrawTonemap(SceneViewInstance& view, vk::raii::CommandBuffer& cmd)
	{
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *TonemapPipeline);
		BindSceneSets(view, cmd);
		cmd.draw(3, 1, 0, 0);
	}

	void VulkanRenderer::CreateSkyboxPipeline()
	{
		const std::vector<uint32_t>& vertCode = Programs.GetSpirv("Skybox.slang", "skyboxVertex");
		const std::vector<uint32_t>& fragCode = Programs.GetSpirv("Skybox.slang", "skyboxFragment");

		vk::raii::ShaderModule vertShaderModule = CreateShaderModule(Context, vertCode);
		vk::raii::ShaderModule fragShaderModule = CreateShaderModule(Context, fragCode);

		vk::PipelineShaderStageCreateInfo vertStageInfo{};
		vertStageInfo.stage = vk::ShaderStageFlagBits::eVertex;
		vertStageInfo.module = *vertShaderModule;
		vertStageInfo.pName = "skyboxVertex";

		vk::PipelineShaderStageCreateInfo fragStageInfo{};
		fragStageInfo.stage = vk::ShaderStageFlagBits::eFragment;
		fragStageInfo.module = *fragShaderModule;
		fragStageInfo.pName = "skyboxFragment";

		vk::PipelineShaderStageCreateInfo shaderStages[] = { vertStageInfo, fragStageInfo };

		// Only position is used by the skybox shader.
		vk::VertexInputBindingDescription bindingDescription{};
		bindingDescription.binding = 0;
		bindingDescription.stride = sizeof(Vertex);
		bindingDescription.inputRate = vk::VertexInputRate::eVertex;

		vk::VertexInputAttributeDescription positionAttribute{};
		positionAttribute.binding = 0;
		positionAttribute.location = 0;
		positionAttribute.format = vk::Format::eR32G32B32Sfloat;
		positionAttribute.offset = offsetof(Vertex, Position);

		vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
		vertexInputInfo.vertexAttributeDescriptionCount = 1;
		vertexInputInfo.pVertexAttributeDescriptions = &positionAttribute;

		vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
		inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		vk::PipelineViewportStateCreateInfo viewportState{};
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;

		vk::PipelineRasterizationStateCreateInfo rasterizer{};
		rasterizer.depthClampEnable = VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode = vk::PolygonMode::eFill;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = vk::CullModeFlagBits::eNone;
		rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
		rasterizer.depthBiasEnable = VK_FALSE;

		vk::PipelineMultisampleStateCreateInfo multisampling{};
		multisampling.sampleShadingEnable = VK_FALSE;
		multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

		vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
		colorBlendAttachment.blendEnable = VK_FALSE;
		colorBlendAttachment.colorWriteMask =
			vk::ColorComponentFlagBits::eR |
			vk::ColorComponentFlagBits::eG |
			vk::ColorComponentFlagBits::eB |
			vk::ColorComponentFlagBits::eA;

		vk::PipelineColorBlendStateCreateInfo colorBlending{};
		colorBlending.logicOpEnable = VK_FALSE;
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &colorBlendAttachment;

		std::array<vk::DynamicState, 2> dynamicStates = {
			vk::DynamicState::eViewport,
			vk::DynamicState::eScissor
		};

		vk::PipelineDynamicStateCreateInfo dynamicState{};
		dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
		dynamicState.pDynamicStates = dynamicStates.data();

		// Draw after opaque geometry.
		vk::PipelineDepthStencilStateCreateInfo depthStencil{};
		depthStencil.depthTestEnable = VK_TRUE;
		depthStencil.depthWriteEnable = VK_FALSE;
		depthStencil.depthCompareOp = vk::CompareOp::eLessOrEqual;
		depthStencil.depthBoundsTestEnable = VK_FALSE;
		depthStencil.stencilTestEnable = VK_FALSE;

		vk::GraphicsPipelineCreateInfo pipelineInfo{};
		pipelineInfo.stageCount = 2;
		pipelineInfo.pStages = shaderStages;
		pipelineInfo.pVertexInputState = &vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState = &multisampling;
		pipelineInfo.pDepthStencilState = &depthStencil;
		pipelineInfo.pColorBlendState = &colorBlending;
		pipelineInfo.pDynamicState = &dynamicState;
		pipelineInfo.layout = *ScenePipelineLayout;
		pipelineInfo.renderPass = GetSceneRenderPass();
		pipelineInfo.subpass = 0;

		SkyboxPipeline = std::move(vk::raii::Pipeline(
			Context.GetDevice(),
			Context.GetPipelineCache(),
			pipelineInfo));
	}

	void VulkanRenderer::LoadSkyboxCubemap()
	{
		SkyboxCubemap.SetContext(Context);

		const bool bLoaded = SkyboxCubemap.Load();
		ASSERT(bLoaded && "Failed to load skybox cubemap.");
		SkyboxTextureIndex = Bindless.AddTexture(SkyboxCubemap.GetImageView());
	}

	std::vector<uint32_t> VulkanRenderer::ReadSpirvFile(const std::string& fileName)
	{
		const std::string suffix = ".spv";
		const std::string name = fileName.ends_with(suffix) ? fileName.substr(0, fileName.size() - suffix.size()) : fileName;
		return Programs.GetSpirv(name);
	}

	void VulkanRenderer::CreateSceneResourcesForView(SceneViewInstance& view)
	{
		// ---------------------------------------------------------
		// Per-view scene UBO
		// ---------------------------------------------------------
		CreateBuffer(
			Context,
			sizeof(SceneUBO),
			vk::BufferUsageFlagBits::eUniformBuffer,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
			view.SceneUniformBuffer,
			view.SceneUniformBufferMemory);

		// ---------------------------------------------------------
		// Per-view scene descriptor pool: binding 0 = SceneUBO
		// ---------------------------------------------------------
		const vk::DescriptorPoolSize poolSize(vk::DescriptorType::eUniformBuffer, 1);

		vk::DescriptorPoolCreateInfo poolInfo{};
		poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
		poolInfo.maxSets = 1;
		poolInfo.poolSizeCount = 1;
		poolInfo.pPoolSizes = &poolSize;

		view.SceneDescriptorPool = vk::raii::DescriptorPool(Context.GetDevice(), poolInfo);

		// ---------------------------------------------------------
		// Per-view scene descriptor set allocation
		// ---------------------------------------------------------
		vk::DescriptorSetLayout layout = *SceneDescriptorSetLayout;

		vk::DescriptorSetAllocateInfo allocInfo{};
		allocInfo.descriptorPool = *view.SceneDescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &layout;

		view.SceneDescriptorSets = vk::raii::DescriptorSets(Context.GetDevice(), allocInfo);

		// ---------------------------------------------------------
		// Per-view scene descriptor set update
		// ---------------------------------------------------------
		vk::DescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = *view.SceneUniformBuffer;
		bufferInfo.offset = 0;
		bufferInfo.range = sizeof(SceneUBO);

		vk::WriteDescriptorSet write{};
		write.dstSet = *view.SceneDescriptorSets.front();
		write.dstBinding = 0;
		write.dstArrayElement = 0;
		write.descriptorType = vk::DescriptorType::eUniformBuffer;
		write.descriptorCount = 1;
		write.pBufferInfo = &bufferInfo;

		Context.GetDevice().updateDescriptorSets(write, {});
	}

	void VulkanRenderer::UpdateSceneUniforms(SceneViewInstance& view)
	{
		const vk::Extent2D extent = view.RenderTarget.GetExtent();

		SceneUBO ubo{};
		ubo.ViewProj = view.SceneGlobals.ViewProjection;
		ubo.InvViewProj = glm::inverse(view.SceneGlobals.ViewProjection);
		ubo.ViewportSize = glm::vec2(
			static_cast<float>(extent.width),
			static_cast<float>(extent.height));
		ubo.SkyRotation = glm::vec2(std::cos(view.SkyRotation), std::sin(view.SkyRotation));
		ubo.DebugView = static_cast<uint32_t>(view.Debug);

		ubo.CameraWorldPos = glm::vec4(view.SceneGlobals.CameraWorldPos, 1.0f);
		ubo.LightDirectionWS = glm::vec4(view.SceneGlobals.LightDirectionWS, 0.0f);
		ubo.LightColor = glm::vec4(view.SceneGlobals.LightColor, view.SceneGlobals.Ambient);

		const glm::mat4 skyboxView = glm::mat4(glm::mat3(view.SceneGlobals.View));
		ubo.SkyboxViewProj = view.SceneGlobals.Projection * skyboxView;
		const uint32_t sky = GetSkySlot(view.Sky);
		ubo.EnvironmentTexture = sky;
		ubo.SceneTexture = view.HdrTextureSlot;
		ubo.ExposureScale = std::exp2(view.ExposureStops);
		ubo.SpecularTexture = Environment.GetSpecularSlot(sky);
		ubo.SpecularLevels = Environment.GetSpecularLevels();
		ubo.BrdfLookup = Environment.GetLookupSlot();
		ubo.Time = SceneTime;
		for (size_t term = 0; term < 9; ++term)
		{
			ubo.Irradiance[term] = glm::vec4(Environment.GetIrradiance(sky)[term], 0.0f);
		}

		void* mapped = view.SceneUniformBufferMemory.mapMemory(0, sizeof(SceneUBO));
		std::memcpy(mapped, &ubo, sizeof(SceneUBO));
		view.SceneUniformBufferMemory.unmapMemory();
	}

	void VulkanRenderer::CreateTestMeshData()
	{
		// Cube with individual faces (no shared corner vertices)
		// {Position}, {Color}, {UV}, {Normal}
		MeshVertices = {
			// Front (+Z)
			{ { -0.5f, -0.5f, 0.5f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
			{ { 0.5f, -0.5f, 0.5f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
			{ { 0.5f, 0.5f, 0.5f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },
			{ { -0.5f, 0.5f, 0.5f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },

			// Back (-Z)
			{ { 0.5f, -0.5f, -0.5f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } },
			{ { -0.5f, -0.5f, -0.5f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } },
			{ { -0.5f, 0.5f, -0.5f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } },
			{ { 0.5f, 0.5f, -0.5f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } },

			// Left (-X)
			{ { -0.5f, -0.5f, -0.5f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f } },
			{ { -0.5f, -0.5f, 0.5f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f }, { -1.0f, 0.0f, 0.0f } },
			{ { -0.5f, 0.5f, 0.5f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f }, { -1.0f, 0.0f, 0.0f } },
			{ { -0.5f, 0.5f, -0.5f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f }, { -1.0f, 0.0f, 0.0f } },

			// Right (+X)
			{ { 0.5f, -0.5f, 0.5f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
			{ { 0.5f, -0.5f, -0.5f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
			{ { 0.5f, 0.5f, -0.5f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },
			{ { 0.5f, 0.5f, 0.5f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },

			// Top (+Y)
			{ { -0.5f, 0.5f, 0.5f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
			{ { 0.5f, 0.5f, 0.5f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
			{ { 0.5f, 0.5f, -0.5f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
			{ { -0.5f, 0.5f, -0.5f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },

			// Bottom (-Y)
			{ { -0.5f, -0.5f, -0.5f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f }, { 0.0f, -1.0f, 0.0f } },
			{ { 0.5f, -0.5f, -0.5f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f } },
			{ { 0.5f, -0.5f, 0.5f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f }, { 0.0f, -1.0f, 0.0f } },
			{ { -0.5f, -0.5f, 0.5f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, -1.0f, 0.0f } }
		};

		MeshIndices = {
			// Front
			0, 1, 2, 2, 3, 0,

			// Back
			4, 5, 6, 6, 7, 4,

			// Left
			8, 9, 10, 10, 11, 8,

			// Right
			12, 13, 14, 14, 15, 12,

			// Top
			16, 17, 18, 18, 19, 16,

			// Bottom
			20, 21, 22, 22, 23, 20
		};

		// @todo: Move Cube Mesh Management to a more generalized place for mesh handling
		{
			CubeMesh.Release();

			Nyx::MeshData cubeData;
			cubeData.Vertices = MeshVertices;
			cubeData.Indices = MeshIndices;
			Nyx::ComputeTangents(cubeData);
			MeshVertices = cubeData.Vertices;

			{
				glm::vec3 localMin{};
				glm::vec3 localMax{};
				ComputeLocalBounds(MeshVertices, localMin, localMax);

				CubeMesh.SetLocalBounds(localMin, localMax);
			}

			CubeMesh.Upload(Context, cubeData);
		}

		SphereMesh.Release();
		SphereMesh.SetLocalBounds(glm::vec3(-0.5f), glm::vec3(0.5f));
		SphereMesh.Upload(Context, Nyx::MakeSphere(0.5f, 32, 64));

		CapsuleMesh.Release();
		CapsuleMesh.SetLocalBounds(glm::vec3(-0.3f, 0.0f, -0.3f), glm::vec3(0.3f, 1.8f, 0.3f));
		CapsuleMesh.Upload(Context, Nyx::MakeCapsule(0.3f, 1.8f, 32, 48));

		PlaneMesh.Release();
		PlaneMesh.SetLocalBounds(glm::vec3(-0.5f, 0.0f, -0.5f), glm::vec3(0.5f, 0.0f, 0.5f));
		PlaneMesh.Upload(Context, Nyx::MakePlane(1.0f));
	}

	void VulkanRenderer::StartLiveShaderReload()
	{
		const std::filesystem::path compiler = Nyx::Paths::GetExecutableDir() / "NyxShaderCompiler.exe";
		if (!std::filesystem::is_directory(Nyx::Paths::GetShadersDir()))
		{
			LOG_INFO("Live shader reload is off: no shader sources at {}", Nyx::Paths::GetShadersDir().string());
			return;
		}
		if (!std::filesystem::exists(compiler))
		{
			LOG_INFO("Live shader reload is off: {} is missing", compiler.string());
			return;
		}
		auto service = std::make_unique<Nyx::RemoteShaderCompileService>(compiler, std::vector<std::filesystem::path>{ Nyx::Paths::GetShaderLibraryDir() });
		// The asset folders for material shaders
		std::vector<std::filesystem::path> watched{ Nyx::Paths::GetShaderLibraryDir() };
		const std::vector<std::filesystem::path>& assetFolders = Nyx::Paths::GetAssetSearchDirs();
		watched.insert(watched.end(), assetFolders.begin(), assetFolders.end());
		if (!Programs.EnableLiveReload(std::move(service), watched))
		{
			LOG_WARNING("Live shader reload is off: {}", Programs.GetProblem());
			return;
		}
		LOG_INFO("Live shader reload: saving a shader in {} compiles it again", Nyx::Paths::GetShadersDir().string());
	}

	void VulkanRenderer::RecordCompile(const Nyx::ShaderProgramUpdate& update)
	{
		using Nyx::EShaderCompileKind;
		const bool bMaterial = update.Request.Kind == EShaderCompileKind::Material || update.Request.Kind == EShaderCompileKind::DefaultsWriter;
		std::string name = update.Name;
		if (bMaterial)
		{
			// By its path in the asset folder it is in, as materials name it
			name = update.Request.File.filename().string();
			for (const std::filesystem::path& folder : Nyx::Paths::GetAssetSearchDirs())
			{
				std::error_code error;
				const std::filesystem::path relative = std::filesystem::relative(update.Request.File, folder, error);
				if (!error && !relative.empty() && *relative.begin() != "..")
				{
					name = relative.generic_string();
					break;
				}
			}
		}
		if (update.Request.Kind == EShaderCompileKind::DefaultsWriter)
		{
			// The defaults writer is the material's own business: shown only while it fails
			name += " (defaults)";
			if (update.Result.bSucceeded)
			{
				ShaderCompiles.Remove(name);
				return;
			}
		}

		Nyx::ShaderLogEntry entry;
		entry.Name = std::move(name);
		entry.File = update.Request.File;
		entry.bSucceeded = update.Result.bSucceeded;
		entry.Milliseconds = update.Result.Milliseconds;
		entry.EntryPoints = static_cast<uint32_t>(update.Result.EntryPoints.size());
		for (const Nyx::CompiledEntryPoint& entryPoint : update.Result.EntryPoints)
		{
			entry.SpirvBytes += entryPoint.Spirv.size() * sizeof(uint32_t);
		}
		entry.When = std::chrono::system_clock::now();
		entry.Diagnostics = update.Result.Diagnostics;
		ShaderCompiles.Add(std::move(entry));
	}

	void VulkanRenderer::ApplyShaderUpdates()
	{
		const std::vector<Nyx::ShaderProgramUpdate> updates = Programs.Update();
		// Every frame: saved material files too
		Materials.Update(updates);
		if (updates.empty())
		{
			return;
		}

		std::set<std::string> changed;
		for (const Nyx::ShaderProgramUpdate& update : updates)
		{
			// A material shader's two compiles by its file's name
			const std::string name = update.Request.Kind == Nyx::EShaderCompileKind::Material ? update.Request.File.filename().string()
				: update.Request.Kind == Nyx::EShaderCompileKind::DefaultsWriter              ? update.Request.File.filename().string() + " (defaults)"
																									  : update.Name;
			RecordCompile(update);
			// Compiled here, not taken from the editor: for the games the editor started
			if (OnShaderCompiled && update.bChanged && !Programs.IsFollowingEditor())
			{
				OnShaderCompiled(update.Request, update.Result);
			}
			if (update.bChanged)
			{
				LOG_INFO("Shader {} compiled in {:.0f} ms", name, update.Result.Milliseconds);
				changed.insert(update.Name);
			}
			else
			{
				LOG_ERROR("Shader {} doesn't compile; it keeps its last good code", name);
			}
			// With the whole path, so the line can be opened from the log
			for (const Nyx::ShaderDiagnostic& diagnostic : update.Result.Diagnostics)
			{
				std::string text = diagnostic.File.empty() ? std::string() : diagnostic.File.string();
				if (diagnostic.Line > 0)
				{
					text += "(" + std::to_string(diagnostic.Line) + (diagnostic.Column > 0 ? "," + std::to_string(diagnostic.Column) : "") + ")";
				}
				text += (text.empty() ? "" : ": ") + diagnostic.Message;
				switch (diagnostic.Severity)
				{
				case Nyx::EShaderDiagnosticSeverity::Error:
					LOG_ERROR("{}", text);
					break;
				case Nyx::EShaderDiagnosticSeverity::Warning:
					LOG_WARNING("{}", text);
					break;
				default:
					LOG_INFO("{}", text);
					break;
				}
			}
		}
		if (changed.empty())
		{
			return;
		}

		// The pipelines are made again from the new code; the GPU must be done with the old ones
		Context.GetDevice().waitIdle();
		const auto uses = [&changed](const char* shader)
		{
			return changed.contains(std::string(shader) + ".vert") || changed.contains(std::string(shader) + ".frag");
		};
		if (changed.contains("MissingMaterial.slang"))
		{
			Materials.RebuildMissingPipeline();
		}
		if (changed.contains("Skybox.slang"))
		{
			CreateSkyboxPipeline();
		}
		if (changed.contains("Tonemap.slang"))
		{
			CreateTonemapPipeline();
		}
		if (changed.contains("Environment.slang"))
		{
			Environment.Rebuild();
		}
		if (uses("DebugLines"))
		{
			DebugLines.CreatePipeline(SceneViews.front());
		}
		Overlays.RebuildPipelines(uses, SceneViews.front());
		if (uses("Painter") || changed.contains("PainterBin.comp"))
		{
			UI.RebuildPainterPipelines();
		}
		if (uses("Blur"))
		{
			UI.RebuildBlurPipeline();
		}
	}

	void VulkanRenderer::ExtractSceneGlobalsFromEditorCamera(SceneViewInstance& view)
	{
		const vk::Extent2D extent = view.RenderTarget.GetExtent();
		view.EditorCam.AspectRatio =
			Nyx::GetAspectRatio(static_cast<float>(extent.width), static_cast<float>(extent.height));

		view.SceneGlobals = {};
		view.SceneGlobals.View = view.EditorCam.GetViewMatrix();
		view.SceneGlobals.Projection = view.EditorCam.GetProjectionMatrix();
		view.SceneGlobals.ViewProjection = view.EditorCam.GetViewProjectionMatrix();
		view.SceneGlobals.CameraWorldPos = view.EditorCam.Position;

		view.SceneGlobals.LightDirectionWS = glm::normalize(glm::vec3(0.4f, 1.0f, 0.2f));
		view.SceneGlobals.LightColor = glm::vec3(1.0f, 0.98f, 0.95f);
		view.SceneGlobals.Ambient = 0.18f;
	}

	void VulkanRenderer::ExtractSceneGlobalsFromWorldCamera(
		SceneViewInstance& view,
		const Nyx::Registry& world)
	{
		const vk::Extent2D extent = view.RenderTarget.GetExtent();

		view.SceneGlobals = {};
		view.SceneGlobals.CameraWorldPos = glm::vec3(0.0f, 0.0f, 3.0f);
		view.SceneGlobals.LightDirectionWS = glm::normalize(glm::vec3(0.4f, 1.0f, 0.2f));
		view.SceneGlobals.LightColor = glm::vec3(1.0f, 0.98f, 0.95f);
		view.SceneGlobals.Ambient = 0.18f;

		// The same view and projection a ray through a pixel goes back through (ScreenPointToRay())
		if (const std::optional<Nyx::WorldCamera> camera = Nyx::FindPrimaryCamera(world))
		{
			const float aspectRatio =
				Nyx::GetAspectRatio(static_cast<float>(extent.width), static_cast<float>(extent.height));
			view.SceneGlobals.View = Nyx::GetCameraView(camera->Transform);
			view.SceneGlobals.Projection = Nyx::GetCameraProjection(camera->Camera, aspectRatio);
			view.SceneGlobals.ViewProjection = view.SceneGlobals.Projection * view.SceneGlobals.View;
			view.SceneGlobals.CameraWorldPos = camera->Transform.Position;
			view.SceneGlobals.bHasCamera = true;
		}

		if (!view.SceneGlobals.bHasCamera)
		{
			ExtractSceneGlobalsFromEditorCamera(view);
			return;
		}

		world.Each<Nyx::DirectionalLightComponent>(
			[&](Nyx::Entity entity, const Nyx::DirectionalLightComponent& lightComp)
			{
				if (view.SceneGlobals.bHasDirectionalLight || !lightComp.bPrimary)
				{
					return;
				}

				glm::vec3 lightDir = glm::normalize(glm::vec3(0.4f, 1.0f, 0.2f));

				if (world.Has<Nyx::TransformComponent>(entity))
				{
					const auto& transform = world.Get<Nyx::TransformComponent>(entity);
					glm::mat4 worldMatrix = transform.ToMatrix();
					glm::vec3 forward = glm::normalize(glm::vec3(worldMatrix * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
					lightDir = -forward;
				}

				view.SceneGlobals.LightDirectionWS = glm::normalize(lightDir);
				view.SceneGlobals.LightColor = lightComp.Color * lightComp.Intensity;
				view.SceneGlobals.Ambient = lightComp.Ambient;
				view.SceneGlobals.bHasDirectionalLight = true;
			});
	}

	void VulkanRenderer::UpdateViewportSceneGlobals(SceneViewInstance& view, const Nyx::Registry& registry)
	{
		switch (view.CameraMode)
		{
		case EViewportCameraMode::EditorFreeCamera:
			ExtractSceneGlobalsFromEditorCamera(view);
			break;

		case EViewportCameraMode::ScenePrimaryCamera:
			ExtractSceneGlobalsFromWorldCamera(view, registry);
			break;

		default:
			ExtractSceneGlobalsFromEditorCamera(view);
			break;
		}
	}

	uint64_t VulkanRenderer::CreateSceneView()
	{
		ASSERT(SceneDescriptorSetLayout != nullptr);

		Nyx::SceneViewInstance view{};
		view.Id = NextSceneViewId++;

		view.RenderTarget.Initialize(Context, 1, 1, vk::Format::eR8G8B8A8Unorm);
		view.HdrTextureSlot = Bindless.AddTexture(view.RenderTarget.GetHdrImageView());

		CreateSceneResourcesForView(view);
		Overlays.CreateViewResources(view);

		// Made while the frame is recorded: not drawn in it, so not shown in it
		view.bRecreatedThisFrame = bRecordingFrame;

		const uint64_t newId = view.Id;
		SceneViews.push_back(std::move(view));
		return newId;
	}

	void VulkanRenderer::DestroySceneView(uint64_t id)
	{
		if (bRecordingFrame)
		{
			if (std::find(SceneViewsToDestroy.begin(), SceneViewsToDestroy.end(), id) == SceneViewsToDestroy.end())
			{
				SceneViewsToDestroy.push_back(id);
			}
			return;
		}
		DestroySceneViewNow(id);
	}

	void VulkanRenderer::DestroySceneViewNow(uint64_t id)
	{
		for (auto it = SceneViews.begin(); it != SceneViews.end(); ++it)
		{
			if (it->Id != id)
			{
				continue;
			}

			// The last frames may still use the view's sets, buffers and images
			Context.WaitIdle();

			it->SceneDescriptorSets = vk::raii::DescriptorSets{ nullptr };
			it->SceneDescriptorPool = nullptr;
			it->SceneUniformBuffer = nullptr;
			it->SceneUniformBufferMemory = nullptr;

			Overlays.DestroyViewResources(*it);

			Bindless.RemoveTexture(it->HdrTextureSlot);
			it->RenderTarget.Shutdown(Context);
			UI.ReleaseSceneViewBlur(*it);

			SceneViews.erase(it);
			return;
		}
	}

	void VulkanRenderer::SetSceneViewSize(uint64_t id, uint32_t width, uint32_t height)
	{
		if (SceneViewInstance* view = FindSceneView(id))
		{
			EnsureSceneViewSize(*view, width, height);
		}
	}

	ImTextureID VulkanRenderer::GetSceneViewTextureId(uint64_t id) const
	{
		if (const SceneViewInstance* view = FindSceneView(id))
		{
			return view->RenderTarget.GetImGuiTextureId();
		}

		return ImTextureID{};
	}

	Extent2D VulkanRenderer::GetSceneViewExtent(uint64_t id) const
	{
		if (const SceneViewInstance* view = FindSceneView(id))
		{
			const vk::Extent2D extent = view->RenderTarget.GetExtent();
			return Extent2D{ extent.width, extent.height };
		}

		return Extent2D{ 0, 0 };
	}

	ImTextureID VulkanRenderer::GetSceneViewBlurTextureId(uint64_t id) const
	{
		const SceneViewInstance* view = FindSceneView(id);
		if (view == nullptr || view->BlurImGuiTexture == VK_NULL_HANDLE)
		{
			return ImTextureID_Invalid;
		}
		return reinterpret_cast<ImTextureID>(view->BlurImGuiTexture);
	}

	Extent2D VulkanRenderer::GetSceneViewBlurExtent(uint64_t id) const
	{
		if (const SceneViewInstance* view = FindSceneView(id))
		{
			const vk::Extent2D extent = view->Blur.GetExtent();
			return Extent2D{ extent.width, extent.height };
		}
		return Extent2D{ 0, 0 };
	}

	std::vector<uint64_t> VulkanRenderer::GetSceneViewIds() const
	{
		std::vector<uint64_t> ids;
		for (const SceneViewInstance& view : SceneViews)
		{
			ids.push_back(view.Id);
		}
		return ids;
	}

	bool VulkanRenderer::WasSceneViewRecreatedThisFrame(uint64_t id) const
	{
		if (const SceneViewInstance* view = FindSceneView(id))
		{
			return view->bRecreatedThisFrame;
		}

		return false;
	}

	bool VulkanRenderer::ReadSceneViewPixels(uint64_t id, Nyx::RgbaImage& outImage)
	{
		SceneViewInstance* view = FindSceneView(id);
		if (view == nullptr)
		{
			return false;
		}

		vk::raii::Device& device = Context.GetDevice();
		device.waitIdle();

		const vk::Extent2D extent = view->RenderTarget.GetExtent();
		const vk::DeviceSize byteCount = static_cast<vk::DeviceSize>(extent.width) * extent.height * 4;
		vk::raii::Buffer readback{ nullptr };
		vk::raii::DeviceMemory readbackMemory{ nullptr };
		CreateBuffer(Context, byteCount, vk::BufferUsageFlagBits::eTransferDst,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, readback, readbackMemory);

		vk::CommandBufferAllocateInfo commandBufferInfo{};
		commandBufferInfo.commandPool = *Context.GetGraphicsCommandPool();
		commandBufferInfo.level = vk::CommandBufferLevel::ePrimary;
		commandBufferInfo.commandBufferCount = 1;
		vk::raii::CommandBuffer copyCommands = std::move(vk::raii::CommandBuffers(device, commandBufferInfo).front());
		copyCommands.begin(vk::CommandBufferBeginInfo{ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

		// The picture is left ShaderReadOnlyOptimal by its render pass, and goes back to that
		const vk::ImageSubresourceRange wholeImage(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
		TransitionImageLayout(*copyCommands, view->RenderTarget.GetImage(), wholeImage, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eTransferSrcOptimal,
			vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::AccessFlagBits::eColorAttachmentWrite, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferRead);

		vk::BufferImageCopy copy{};
		copy.imageSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
		copy.imageExtent = vk::Extent3D(extent.width, extent.height, 1);
		copyCommands.copyImageToBuffer(view->RenderTarget.GetImage(), vk::ImageLayout::eTransferSrcOptimal, *readback, copy);

		TransitionImageLayout(*copyCommands, view->RenderTarget.GetImage(), wholeImage, vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
			vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferRead, vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eShaderRead);
		copyCommands.end();

		vk::SubmitInfo submitInfo{};
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &*copyCommands;
		Context.GetGraphicsQueue().submit(submitInfo);
		Context.GetGraphicsQueue().waitIdle();

		// The target is R8G8B8A8, so the bytes are RGBA already
		outImage.Width = extent.width;
		outImage.Height = extent.height;
		outImage.Pixels.resize(static_cast<size_t>(byteCount));
		std::memcpy(outImage.Pixels.data(), readbackMemory.mapMemory(0, byteCount), outImage.Pixels.size());
		readbackMemory.unmapMemory();
		return true;
	}

	SceneViewInstance* VulkanRenderer::FindSceneView(uint64_t id)
	{
		for (SceneViewInstance& view : SceneViews)
		{
			if (view.Id == id)
			{
				return &view;
			}
		}

		return nullptr;
	}

	const SceneViewInstance* VulkanRenderer::FindSceneView(uint64_t id) const
	{
		for (const SceneViewInstance& view : SceneViews)
		{
			if (view.Id == id)
			{
				return &view;
			}
		}

		return nullptr;
	}

	void VulkanRenderer::EnsureSceneViewSize(SceneViewInstance& view, uint32_t width, uint32_t height)
	{
		width = std::max(1u, width);
		height = std::max(1u, height);

		const vk::Extent2D currentExtent = view.RenderTarget.GetExtent();

		if (currentExtent.width == width && currentExtent.height == height)
		{
			return;
		}

		if (view.PendingWidth == width && view.PendingHeight == height && view.bResizePending)
		{
			return;
		}

		view.PendingWidth = width;
		view.PendingHeight = height;
		view.bResizePending = true;
	}

	void VulkanRenderer::RenderSceneView(SceneViewInstance& view, vk::raii::CommandBuffer& cmd)
	{
		// 1. The scene in HDR: opaque objects, then the sky, then the surface decals blended over
		// both, as what is blended comes after all that is opaque
		const vk::ClearColorValue clear = vk::ClearColorValue(std::array<float, 4>{ 0.2f, 0.05f, 0.35f, 1.0f });
		const RenderWorld* world = FindShownWorld(view);
		view.RenderTarget.BeginScenePass(cmd, clear);
		if (world)
		{
			DrawRenderObjects(view, *world, cmd);
		}
		DrawSkybox(view, cmd);
		if (world)
		{
			DrawRenderDecals(view, *world, cmd);
		}
		view.RenderTarget.EndRenderPass(cmd);

		// 2. The picture: the scene tonemapped, then the editor's overlays over it (grid, debug
		// lines, the selection outline), which look as they always did, then the world's debug
		// drawing
		view.RenderTarget.BeginPicturePass(cmd);
		DrawTonemap(view, cmd);
		if (view.bShowEditorOverlays)
		{
			Overlays.Draw(view, world, cmd);
		}
		if (world && view.bShowDebugDrawing)
		{
			DebugLines.Draw(view, world->DebugDrawLines, cmd);
		}
		view.RenderTarget.EndRenderPass(cmd);
	}

	void VulkanRenderer::RequestPick(uint64_t sceneViewId, uint32_t pixelX, uint32_t pixelY)
	{
		SceneViewInstance* view = FindSceneView(sceneViewId);
		if (view && view->bShowEditorOverlays)
		{
			Overlays.RequestPick(*view, GetShownWorldRevision(*view), pixelX, pixelY);
		}
	}

	Nyx::IEditorRenderer::PickResult VulkanRenderer::ConsumeLastPickResult(uint64_t sceneViewId)
	{
		SceneViewInstance* view = FindSceneView(sceneViewId);
		return view ? Overlays.ConsumeLastPickResult(*view, GetShownWorldRevision(*view)) : PickResult{};
	}

	void VulkanRenderer::ReadBackPickResults()
	{
		for (SceneViewInstance& view : SceneViews)
		{
			Overlays.ReadBackPick(view, FindShownWorld(view));
		}
	}

	void VulkanRenderer::DrawSkybox(SceneViewInstance& view, vk::raii::CommandBuffer& cmd)
	{
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *SkyboxPipeline);

		BindSceneSets(view, cmd);

		vk::DeviceSize offsets[] = { 0 };
		cmd.bindVertexBuffers(0, { CubeMesh.GetVertexBuffer() }, offsets);
		cmd.bindIndexBuffer(CubeMesh.GetIndexBuffer(), 0, vk::IndexType::eUint32);
		cmd.drawIndexed(CubeMesh.GetIndexCount(), 1, 0, 0, 0);
	}

}
