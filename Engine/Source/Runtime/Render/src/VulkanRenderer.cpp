#include "NyxPCH.h"
#include "Assertions.h"
#include "VulkanRenderer.h"
#include "VulkanContext.h"
#include "VulkanSwapchain.h"
#include "VulkanUtil.h"
#include "Log.h"
#include "GltfImporter.h"
#include "AmbientOcclusion.h"
#include "Atmosphere.h"
#include "Bloom.h"
#include "EnvironmentComponent.h"
#include "TransformComponent.h"
#include "CameraProjection.h"
#include "PreviousTransformComponent.h"
#include "FixedStepClock.h"
#include "DrawnWorldTime.h"
#include "MeshInstancesComponent.h"
#include "MeshRendererComponent.h"
#include "RuntimeMeshComponent.h"
#include "Paths.h"
#include "Profiler.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
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
	// The profiler's names of the shadow cascades' zones, which must be literals
	constexpr const char* ShadowCascadeZoneNames[Nyx::MaxShadowCascades] = { "Shadow cascade 0", "Shadow cascade 1", "Shadow cascade 2", "Shadow cascade 3" };

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
		// The instance stream of what is drawn once
		MeshInstances.Initialize(Context);

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
			PlaneMesh, MeshInstances.GetIdentity());
		Shadows.Initialize(Bindless, *ScenePipelineLayout, [this](SceneViewInstance& view, vk::raii::CommandBuffer& cmd)
			{
				BindSceneSets(view, cmd);
			});
		Bloom.Initialize(Bindless, *ScenePipelineLayout, [this](SceneViewInstance& view, vk::raii::CommandBuffer& cmd)
			{
				BindSceneSets(view, cmd);
			});
		AmbientOcclusion.Initialize(Bindless, *ScenePipelineLayout, [this](SceneViewInstance& view, vk::raii::CommandBuffer& cmd)
			{
				BindSceneSets(view, cmd);
			});

		LoadSkyboxCubemap();
		// Its light: the specular cube, the irradiance, the BRDF table
		Environment.Initialize(Context, Bindless, Programs);
		Environment.Build(SkyboxTextureIndex);

		// First scene view: creates per-view resources
		CreateSceneView();

		// Pipelines that depend on view-owned render passes / framebuffers
		DebugLines.CreatePipeline(SceneViews.front());
		Overlays.CreatePipelines(SceneViews.front());
		Shadows.CreatePipelines(GetPictureRenderPass());

		// Shared pipelines
		CreateSkyboxPipeline();
		CreateTonemapPipeline();

		// 2D shapes, the scene views' blur for glows and glass, and the frame's GPU times
		UI.InitializePasses();
		CreatePassTimestamps();

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
		RuntimeMeshes.Clear();
		MeshInstances.Clear();
		FrameInstanceBuffer = nullptr;
		FrameInstanceMemory = nullptr;
		RetiredFrameInstanceBuffers.clear();

		for (SceneViewInstance& view : SceneViews)
		{
			view.SceneDescriptorSets = vk::raii::DescriptorSets{ nullptr };
			view.SceneDescriptorPool = nullptr;
			view.SceneUniformBuffer = nullptr;
			view.SceneUniformBufferMemory = nullptr;

			UI.ReleaseSceneViewBlur(view);
			Bloom.Release(view.Bloom);
			AmbientOcclusion.Release(view.AmbientOcclusion);
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
		Shadows.Shutdown();
		Bloom.Shutdown();
		AmbientOcclusion.Shutdown();
		PassTimestamps = nullptr;
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
			ReadPassTimestamps();
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

		// Views let go of during the last frame, which the GPU is done with now, and meshes made at
		// run time and instances that were replaced or no longer drawn
		for (const uint64_t id : std::exchange(SceneViewsToDestroy, {}))
		{
			DestroySceneViewNow(id);
		}
		RuntimeMeshes.ReleaseRetired();
		MeshInstances.ReleaseRetired();
		RetiredFrameInstanceBuffers.clear();
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

		// Each shown world's state, once per frame, for all views that show it; then what each view
		// draws of it, with the lines that shows; then the frame's debug lines go up
		SceneTime = std::chrono::duration<float>(sceneStart - StartTime).count();
		ExtractRenderWorlds();
		{
			NYX_PROFILE_SCOPE("Culling");
			PrepareSceneViews();
		}
		DebugLines.Upload();

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
			if (*PassTimestamps)
			{
				cmd.resetQueryPool(*PassTimestamps, 0, MaxTimedViews * static_cast<uint32_t>(ETimedPass::Count) * 2);
			}
			uint32_t viewIndex = 0;
			for (SceneViewInstance& view : SceneViews)
			{
				view.TimedViewIndex = viewIndex++;
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
			const RenderWorld* world = FindShownWorld(view);
			UpdateSceneUniforms(view, world);

			// Editor-only passes: picking, and the masks the selection outline is drawn from
			if (world && view.bShowEditorOverlays)
			{
				Overlays.DrawPasses(view, *world, cmd);
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

	void VulkanRenderer::SetSceneViewCullingDebug(uint64_t id, ESceneViewCullingDebug mode)
	{
		SceneViewInstance* view = FindSceneView(id);
		if (view && view->CullingDebug != mode)
		{
			view->CullingDebug = mode;
			// Frozen anew where the camera is at the next frame
			view->FrozenCullingViewProjection.reset();
		}
	}

	const SceneViewCullingStats* VulkanRenderer::GetSceneViewCullingStats(uint64_t id) const
	{
		const SceneViewInstance* view = FindSceneView(id);
		return view ? &view->Culling : nullptr;
	}

	void VulkanRenderer::SetSceneViewShadowDebug(uint64_t id, ESceneViewShadowDebug mode)
	{
		if (SceneViewInstance* view = FindSceneView(id))
		{
			view->ShadowDebug = mode;
		}
	}

	const SceneViewShadows* VulkanRenderer::GetSceneViewShadows(uint64_t id) const
	{
		const SceneViewInstance* view = FindSceneView(id);
		return view ? &view->Shadows : nullptr;
	}

	const SceneViewBloom* VulkanRenderer::GetSceneViewBloom(uint64_t id) const
	{
		const SceneViewInstance* view = FindSceneView(id);
		return view ? &view->Bloom : nullptr;
	}

	const SceneViewGpuTimes* VulkanRenderer::GetSceneViewGpuTimes(uint64_t id) const
	{
		const SceneViewInstance* view = FindSceneView(id);
		return view ? &view->GpuTimes : nullptr;
	}

	void VulkanRenderer::CreatePassTimestamps()
	{
		const std::vector<vk::QueueFamilyProperties> families = Context.GetPhysicalDevice().getQueueFamilyProperties();
		const uint32_t family = Context.GetGraphicsQueueFamily();
		if (family >= families.size() || families[family].timestampValidBits == 0)
		{
			return;
		}
		vk::QueryPoolCreateInfo queryInfo{};
		queryInfo.queryType = vk::QueryType::eTimestamp;
		queryInfo.queryCount = MaxTimedViews * static_cast<uint32_t>(ETimedPass::Count) * 2;
		PassTimestamps = vk::raii::QueryPool(Context.GetDevice(), queryInfo);
	}

	void VulkanRenderer::WritePassTimestamp(vk::raii::CommandBuffer& cmd, const SceneViewInstance& view, ETimedPass pass, bool bEnd)
	{
		if (!*PassTimestamps || view.TimedViewIndex >= MaxTimedViews)
		{
			return;
		}
		const uint32_t firstQuery = (view.TimedViewIndex * static_cast<uint32_t>(ETimedPass::Count) + static_cast<uint32_t>(pass)) * 2;
		cmd.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, *PassTimestamps, firstQuery + (bEnd ? 1 : 0));
		if (bEnd)
		{
			TimedPasses.push_back(TimedPass{ view.Id, pass, firstQuery });
		}
	}

	void VulkanRenderer::ReadPassTimestamps()
	{
		for (SceneViewInstance& view : SceneViews)
		{
			view.GpuTimes = {};
		}
		if (!*PassTimestamps)
		{
			return;
		}
		const double nanosecondsPerTick = Context.GetPhysicalDevice().getProperties().limits.timestampPeriod;
		for (const TimedPass& timed : std::exchange(TimedPasses, {}))
		{
			SceneViewInstance* view = FindSceneView(timed.ViewId);
			const auto [result, ticks] = PassTimestamps.getResults<uint64_t>(timed.FirstQuery, 2, 2 * sizeof(uint64_t), sizeof(uint64_t), vk::QueryResultFlagBits::e64);
			if (view == nullptr || result != vk::Result::eSuccess || ticks.size() != 2)
			{
				continue;
			}
			const float milliseconds = static_cast<float>(static_cast<double>(ticks[1] - ticks[0]) * nanosecondsPerTick / 1.0e6);
			switch (timed.Pass)
			{
			case ETimedPass::Bloom:
				view->GpuTimes.Bloom = milliseconds;
				break;
			case ETimedPass::AmbientOcclusion:
				view->GpuTimes.AmbientOcclusion = milliseconds;
				break;
			default:
				break;
			}
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
		world->Sections.clear();
		world->Cells.clear();
		world->CasterBounds.reset();
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
			world->Time = GetWorldTime(world->Registry);
			ExtractRenderObjects(*world, interpolation);
			ExtractSurfaceDecals(*world->Registry, interpolation, DecalAssets, world->DecalMaterials, world->Decals);
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
		// Meshes made at run time and instances that no shown world draws any more
		RuntimeMeshes.EndFrame();
		MeshInstances.EndFrame();
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

	float VulkanRenderer::GetWorldTime(const Nyx::Registry* registry) const
	{
		// The editor's own worlds have no clock: material previews and edits keep moving
		const auto clock = WorldInterpolationClocks.find(registry);
		if (clock == WorldInterpolationClocks.end())
		{
			return SceneTime;
		}
		return GetDrawnWorldTime(*clock->second);
	}

	void VulkanRenderer::ExtractRenderObjects(RenderWorld& world, std::optional<float> interpolation)
	{
		world.Objects.clear();
		world.Sections.clear();
		world.Cells.clear();
		world.CasterBounds.reset();
		world.PickingIdToEntity.clear();

		const Nyx::Registry& registry = *world.Registry;

		const_cast<Nyx::Registry&>(registry).Each<Nyx::MeshRendererComponent>(
			[this, &world, &registry, interpolation](Nyx::Entity entity, Nyx::MeshRendererComponent& meshRenderer)
			{
				if (!meshRenderer.bVisible)
				{
					return;
				}

				// A mesh made at run time takes the place of the one loaded by its path; it is uploaded
				// here, before this frame records a draw, when it is new or its revision changed
				Nyx::Mesh* mesh = registry.Has<Nyx::RuntimeMeshComponent>(entity)
					? RuntimeMeshes.Acquire(Context, registry.Get<Nyx::RuntimeMeshComponent>(entity))
					: meshRenderer.MeshAsset;
				if (!mesh || !meshRenderer.MaterialAsset)
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
				obj.MeshAsset = mesh;
				obj.MaterialAsset = meshRenderer.MaterialAsset;
				obj.WorldTransform = worldTransform;
				obj.DrawTransform = drawTransform;
				obj.bMirrored = glm::determinant(glm::mat3(drawTransform)) < 0.0f;
				obj.bCastShadows = meshRenderer.bCastShadows;
				obj.Custom = meshRenderer.Custom;
				obj.LocalBoundsMin = mesh->GetLocalBoundsMin();
				obj.LocalBoundsMax = mesh->GetLocalBoundsMax();
				obj.InstanceBuffer = MeshInstances.GetIdentity();

				// Instanced: the mesh at every transform of its instances, uploaded here as a mesh made
				// at run time is, and culled cell by cell where it is drawn. Without instances to
				// draw, nothing.
				if (registry.Has<Nyx::MeshInstancesComponent>(entity))
				{
					const MeshInstanceCache::Instances* instances = MeshInstances.Acquire(Context, registry.Get<Nyx::MeshInstancesComponent>(entity),
						BoundingBox{ obj.LocalBoundsMin, obj.LocalBoundsMax });
					if (!instances)
					{
						return;
					}
					obj.InstanceBuffer = instances->Buffer;
					obj.InstanceUploadId = instances->UploadId;
					obj.LocalBoundsMin = instances->Bounds.Min;
					obj.LocalBoundsMax = instances->Bounds.Max;
					obj.FirstCell = static_cast<uint32_t>(world.Cells.size());
					for (const InstanceCullCell& cell : instances->Cells)
					{
						world.Cells.push_back(InstanceCullCell{ .Bounds = TransformBox(cell.Bounds, drawTransform), .First = cell.First, .Count = cell.Count });
					}
					obj.CellCount = static_cast<uint32_t>(instances->Cells.size());
				}
				obj.WorldBounds = TransformBox(BoundingBox{ obj.LocalBoundsMin, obj.LocalBoundsMax }, drawTransform);
				if (obj.bCastShadows)
				{
					world.CasterBounds = world.CasterBounds ? UniteBoxes(*world.CasterBounds, obj.WorldBounds) : obj.WorldBounds;
				}

				obj.FirstSection = static_cast<uint32_t>(world.Sections.size());
				for (const Nyx::MeshSection& section : mesh->GetSections())
				{
					world.Sections.push_back(RenderSection{ .FirstIndex = section.FirstIndex,
						.IndexCount = section.IndexCount,
						.MaterialAsset = Nyx::GetSlotMaterial(meshRenderer, section.MaterialSlot) });
				}
				obj.SectionCount = static_cast<uint32_t>(world.Sections.size()) - obj.FirstSection;

				// Dense picking ID: 1..N, with 0 reserved for "nothing"
				world.PickingIdToEntity.push_back(entity);
				obj.PickingId = static_cast<uint32_t>(world.PickingIdToEntity.size());

				world.Objects.push_back(obj);
			});
	}

	void VulkanRenderer::PrepareSceneViews()
	{
		FrameInstances.clear();
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

			view.Draws.clear();
			view.Culling = {};
			view.CullingLines = {};
			view.Shadows.CascadeCount = 0;
			if (const RenderWorld* world = FindShownWorld(view))
			{
				UpdateViewportSceneGlobals(view, *world->Registry);
				CullSceneView(view, *world);
				PrepareSceneViewShadows(view, *world);
			}

			// Its bloom's chain and its ambient occlusion's targets, for its size; none without a
			// world, or in a debug view of the materials, whose outputs aren't light
			const uint32_t debugView = static_cast<uint32_t>(view.Debug);
			const bool bLit = FindShownWorld(view) && (debugView < static_cast<uint32_t>(ESceneViewDebug::BaseColor) || debugView > static_cast<uint32_t>(ESceneViewDebug::UvChecker));
			SceneViewBloomSettings bloom = view.SceneGlobals.Bloom;
			bloom.bBloom = bloom.bBloom && bLit;
			Bloom.Prepare(view.Bloom, view.RenderTarget.GetExtent(), bloom);
			SceneViewAmbientOcclusionSettings occlusion = view.SceneGlobals.AmbientOcclusion;
			occlusion.bAmbientOcclusion = occlusion.bAmbientOcclusion && bLit;
			AmbientOcclusion.Prepare(view.AmbientOcclusion, view.RenderTarget, occlusion, view.bRecreatedThisFrame);
		}

		// One shadow map for every view: as big as the largest any of them asks for
		uint32_t shadowMapSize = 0;
		for (const SceneViewInstance& view : SceneViews)
		{
			if (view.Shadows.CascadeCount > 0)
			{
				shadowMapSize = std::max(shadowMapSize, view.Shadows.Settings.Resolution);
			}
		}
		Shadows.EnsureSize(shadowMapSize);
		UploadFrameInstances();
	}

	void VulkanRenderer::CullSceneView(SceneViewInstance& view, const RenderWorld& world)
	{
		// Frozen: by the camera as it was at the first culling since
		glm::mat4 cullingViewProjection = view.SceneGlobals.ViewProjection;
		if (view.CullingDebug == ESceneViewCullingDebug::Frozen)
		{
			if (!view.FrozenCullingViewProjection)
			{
				view.FrozenCullingViewProjection = cullingViewProjection;
			}
			cullingViewProjection = *view.FrozenCullingViewProjection;
		}
		else
		{
			view.FrozenCullingViewProjection.reset();
		}
		const Frustum frustum = MakeFrustum(cullingViewProjection);
		CullDraws(world, frustum, false, view.Draws, view.Culling);

		if (view.CullingDebug != ESceneViewCullingDebug::Off)
		{
			AddCullingLines(view, world, frustum, cullingViewProjection);
		}
	}

	void VulkanRenderer::CullDraws(const RenderWorld& world, const Frustum& frustum, bool bCastersOnly, std::vector<SceneViewDraw>& outDraws, SceneViewCullingStats& stats)
	{
		const size_t firstDraw = outDraws.size();
		std::vector<uint32_t> drawnOnce;
		std::vector<InstanceRun> runs;
		for (uint32_t index = 0; index < world.Objects.size(); ++index)
		{
			const RenderObject& obj = world.Objects[index];
			if (bCastersOnly && !obj.bCastShadows)
			{
				continue;
			}
			++stats.Objects;
			if (!IsBoxVisible(frustum, obj.WorldBounds))
			{
				++stats.ObjectsCulled;
				continue;
			}
			if (obj.CellCount == 0)
			{
				drawnOnce.push_back(index);
				continue;
			}

			// Instanced: the cells in view, each run of them one draw
			runs.clear();
			const uint32_t drawnCells = AppendInstanceRuns(std::span(world.Cells).subspan(obj.FirstCell, obj.CellCount), &frustum, runs);
			stats.Cells += obj.CellCount;
			stats.CellsCulled += obj.CellCount - drawnCells;
			for (const InstanceRun& run : runs)
			{
				outDraws.push_back(SceneViewDraw{ .Object = index, .InstanceBuffer = obj.InstanceBuffer, .FirstInstance = run.First, .InstanceCount = run.Count, .Model = obj.DrawTransform });
				stats.Instances += run.Count;
			}
		}

		// Those drawn once that draw alike, the same mesh with the same material in each section,
		// mirrored alike, are drawn together: their transforms in this frame's stream, one draw for
		// them all. In the order of their entities among themselves, so the same world draws the
		// same way.
		const auto drawsBefore = [&world](uint32_t a, uint32_t b)
		{
			const RenderObject& first = world.Objects[a];
			const RenderObject& second = world.Objects[b];
			if (first.MeshAsset != second.MeshAsset)
			{
				return std::less<const Mesh*>{}(first.MeshAsset, second.MeshAsset);
			}
			// Mirrored, its faces wind the other way round
			if (first.bMirrored != second.bMirrored)
			{
				return second.bMirrored;
			}
			// One mesh has the same sections
			for (uint32_t section = 0; section < first.SectionCount; ++section)
			{
				const Material* firstMaterial = world.Sections[first.FirstSection + section].MaterialAsset;
				const Material* secondMaterial = world.Sections[second.FirstSection + section].MaterialAsset;
				if (firstMaterial != secondMaterial)
				{
					return std::less<const Material*>{}(firstMaterial, secondMaterial);
				}
			}
			return false;
		};
		std::stable_sort(drawnOnce.begin(), drawnOnce.end(), drawsBefore);
		for (size_t first = 0; first < drawnOnce.size();)
		{
			size_t end = first + 1;
			while (end < drawnOnce.size() && !drawsBefore(drawnOnce[first], drawnOnce[end]))
			{
				++end;
			}
			const uint32_t firstInstance = static_cast<uint32_t>(FrameInstances.size());
			for (size_t index = first; index < end; ++index)
			{
				const RenderObject& member = world.Objects[drawnOnce[index]];
				FrameInstances.push_back(MeshInstanceData{ .Transform = member.DrawTransform, .Custom = member.Custom });
			}
			outDraws.push_back(SceneViewDraw{ .Object = drawnOnce[first], .InstanceBuffer = nullptr, .FirstInstance = firstInstance,
				.InstanceCount = static_cast<uint32_t>(end - first), .Model = glm::mat4(1.0f) });
			stats.Instances += static_cast<uint32_t>(end - first);
			first = end;
		}
		stats.Draws = static_cast<uint32_t>(outDraws.size() - firstDraw);
	}

	void VulkanRenderer::PrepareSceneViewShadows(SceneViewInstance& view, const RenderWorld& world)
	{
		SceneViewShadows& shadows = view.Shadows;
		shadows.CascadeCount = 0;
		const SceneViewShadowSettings& settings = view.SceneGlobals.Shadows;
		if (!settings.bCastShadows || !world.CasterBounds)
		{
			return;
		}
		shadows.Settings = settings;

		// Slices of the view from its near plane out to the shadow distance, each in a sphere that
		// keeps its size however the camera turns, snapped to its texels; each reaches back towards
		// the light as far as anything that casts
		const glm::vec3 towardsLight = glm::normalize(view.SceneGlobals.LightDirectionWS);
		const ShadowCamera camera = MakeShadowCamera(view.SceneGlobals.View, view.SceneGlobals.Projection, view.SceneGlobals.NearPlane);
		const std::array<float, MaxShadowCascades + 1> splits = ComputeCascadeSplits(camera.NearPlane, settings.Distance, settings.CascadeCount, settings.SplitBlend);
		for (uint32_t index = 0; index < settings.CascadeCount; ++index)
		{
			const BoundingSphere sphere = FitCascadeSphere(camera, splits[index], splits[index + 1]);
			ShadowCascade& cascade = shadows.Cascades[index];
			cascade = MakeShadowCascade(sphere, towardsLight, settings.Resolution, GetCasterReach(*world.CasterBounds, sphere.Center, towardsLight));
			cascade.SplitNear = splits[index];
			cascade.SplitFar = splits[index + 1];

			// What casts into it, by its own frustum, as the view's camera culls
			shadows.Draws[index].clear();
			shadows.Culling[index] = {};
			CullDraws(world, MakeFrustum(cascade.ViewProjection), true, shadows.Draws[index], shadows.Culling[index]);
		}
		shadows.CascadeCount = settings.CascadeCount;
	}

	void VulkanRenderer::DrawShadowCascades(SceneViewInstance& view, const RenderWorld& world, vk::raii::CommandBuffer& cmd)
	{
		const SceneViewShadows& shadows = view.Shadows;
		for (uint32_t cascade = 0; cascade < shadows.CascadeCount; ++cascade)
		{
			NYX_PROFILE_SCOPE("Shadow cascade");
			NYX_PROFILE_ZONE_NAME(ShadowCascadeZoneNames[cascade]);
			Shadows.BeginCascade(cmd, cascade, shadows.Settings.Resolution);
			BindSceneSets(view, cmd);
			const Mesh* boundMesh = nullptr;
			vk::Buffer boundInstances;
			for (const SceneViewDraw& draw : shadows.Draws[cascade])
			{
				const RenderObject& obj = world.Objects[draw.Object];
				const vk::Buffer instances = draw.InstanceBuffer ? draw.InstanceBuffer : *FrameInstanceBuffer;
				if (obj.MeshAsset != boundMesh || instances != boundInstances)
				{
					cmd.bindVertexBuffers(0, { obj.MeshAsset->GetVertexBuffer(), instances }, { vk::DeviceSize{ 0 }, vk::DeviceSize{ 0 } });
					cmd.bindIndexBuffer(obj.MeshAsset->GetIndexBuffer(), 0, vk::IndexType::eUint32);
					boundMesh = obj.MeshAsset;
					boundInstances = instances;
				}

				// The cascade in CustomData2.x; each section with the faces its material draws, so a
				// two-sided one casts from both sides
				ObjectConstants object{};
				object.Model = draw.Model;
				object.CustomData2 = glm::vec2(static_cast<float>(cascade), 0.0f);
				cmd.pushConstants<ObjectConstants>(*ScenePipelineLayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, { object });
				for (uint32_t index = obj.FirstSection; index < obj.FirstSection + obj.SectionCount; ++index)
				{
					const RenderSection& section = world.Sections[index];
					SetMeshFaces(cmd, section.MaterialAsset->bTwoSided, obj.bMirrored);
					cmd.drawIndexed(section.IndexCount, draw.InstanceCount, section.FirstIndex, 0, draw.FirstInstance);
				}
			}
			Shadows.EndCascade(cmd);
		}
	}

	void VulkanRenderer::AddCullingLines(SceneViewInstance& view, const RenderWorld& world, const Frustum& frustum, const glm::mat4& cullingViewProjection)
	{
		const glm::vec3 drawnColor(0.2f, 0.9f, 0.3f);
		const glm::vec3 culledColor(0.95f, 0.2f, 0.15f);
		CullingDrawing.Clear();
		for (const RenderObject& obj : world.Objects)
		{
			const bool bVisible = IsBoxVisible(frustum, obj.WorldBounds);
			if (obj.CellCount == 0 || !bVisible)
			{
				CullingDrawing.Box(obj.WorldBounds.Min, obj.WorldBounds.Max, bVisible ? drawnColor : culledColor);
				continue;
			}
			for (uint32_t cell = obj.FirstCell; cell < obj.FirstCell + obj.CellCount; ++cell)
			{
				const BoundingBox& bounds = world.Cells[cell].Bounds;
				CullingDrawing.Box(bounds.Min, bounds.Max, IsBoxVisible(frustum, bounds) ? drawnColor : culledColor);
			}
		}

		// The frozen camera's frustum: its corners where depth is at the near and the far plane
		if (view.CullingDebug == ESceneViewCullingDebug::Frozen)
		{
			const glm::mat4 toWorld = glm::inverse(cullingViewProjection);
			std::array<glm::vec3, 8> corners{};
			for (int corner = 0; corner < 8; ++corner)
			{
				const glm::vec4 clip((corner & 1) ? 1.0f : -1.0f, (corner & 2) ? 1.0f : -1.0f, (corner & 4) ? Nyx::FarPlaneDepth : Nyx::NearPlaneDepth, 1.0f);
				const glm::vec4 point = toWorld * clip;
				corners[corner] = glm::vec3(point) / point.w;
			}
			const glm::vec3 frustumColor(1.0f, 0.85f, 0.1f);
			for (int corner = 0; corner < 8; ++corner)
			{
				for (const int axis : { 1, 2, 4 })
				{
					if ((corner & axis) == 0)
					{
						CullingDrawing.Line(corners[corner], corners[corner | axis], frustumColor);
					}
				}
			}
		}
		view.CullingLines = DebugLines.Add(CullingDrawing);
	}

	void VulkanRenderer::UploadFrameInstances()
	{
		if (FrameInstances.empty())
		{
			return;
		}
		if (FrameInstances.size() > FrameInstanceCapacity)
		{
			if (*FrameInstanceBuffer)
			{
				RetiredFrameInstanceBuffers.emplace_back(std::move(FrameInstanceBuffer), std::move(FrameInstanceMemory));
			}
			FrameInstanceCapacity = std::max<size_t>(FrameInstances.size() + FrameInstances.size() / 2, 1024);
			CreateBuffer(Context, FrameInstanceCapacity * sizeof(MeshInstanceData), vk::BufferUsageFlagBits::eVertexBuffer,
				vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, FrameInstanceBuffer, FrameInstanceMemory);
		}
		const vk::DeviceSize size = FrameInstances.size() * sizeof(MeshInstanceData);
		void* mapped = FrameInstanceMemory.mapMemory(0, size);
		std::memcpy(mapped, FrameInstances.data(), static_cast<size_t>(size));
		FrameInstanceMemory.unmapMemory();
	}

	void VulkanRenderer::DrawRenderObjects(SceneViewInstance& view, const RenderWorld& world, vk::raii::CommandBuffer& cmd)
	{
		BindSceneSets(view, cmd);
		const Mesh* boundMesh = nullptr;
		vk::Buffer boundInstances;
		for (const SceneViewDraw& draw : view.Draws)
		{
			const RenderObject& obj = world.Objects[draw.Object];
			const vk::Buffer instances = draw.InstanceBuffer ? draw.InstanceBuffer : *FrameInstanceBuffer;
			if (obj.MeshAsset != boundMesh || instances != boundInstances)
			{
				cmd.bindVertexBuffers(0, { obj.MeshAsset->GetVertexBuffer(), instances }, { vk::DeviceSize{ 0 }, vk::DeviceSize{ 0 } });
				cmd.bindIndexBuffer(obj.MeshAsset->GetIndexBuffer(), 0, vk::IndexType::eUint32);
				boundMesh = obj.MeshAsset;
				boundInstances = instances;
			}

			// Each section with its slot's material
			for (uint32_t index = obj.FirstSection; index < obj.FirstSection + obj.SectionCount; ++index)
			{
				const RenderSection& section = world.Sections[index];
				cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **section.MaterialAsset->Pipeline);
				SetMeshFaces(cmd, section.MaterialAsset->bTwoSided, obj.bMirrored);

				ObjectConstants object{};
				object.Model = draw.Model;
				object.MaterialOffset = section.MaterialAsset->MaterialOffset;
				object.EntityId = obj.PickingId;
				cmd.pushConstants<ObjectConstants>(*ScenePipelineLayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, { object });
				cmd.drawIndexed(section.IndexCount, draw.InstanceCount, section.FirstIndex, 0, draw.FirstInstance);
			}
		}
	}

	void VulkanRenderer::DrawRenderDecals(SceneViewInstance& view, const RenderWorld& world, vk::raii::CommandBuffer& cmd)
	{
		if (world.Decals.empty())
		{
			return;
		}

		BindSceneSets(view, cmd);
		cmd.bindVertexBuffers(0, { PlaneMesh.GetVertexBuffer(), MeshInstances.GetIdentity() }, { vk::DeviceSize{ 0 }, vk::DeviceSize{ 0 } });
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
		Bindless.RemoveTexture(view.AmbientTextureSlot);
		Bindless.RemoveTexture(view.DepthTextureSlot);
		view.HdrTextureSlot = Bindless.AddTexture(view.RenderTarget.GetHdrImageView());
		view.AmbientTextureSlot = Bindless.AddTexture(view.RenderTarget.GetAmbientImageView());
		view.DepthTextureSlot = Bindless.AddTexture(view.RenderTarget.GetDepthImageView());
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
		const vk::VertexInputBindingDescription bindingDescription = GetVertexBindingDescription();
		const vk::VertexInputAttributeDescription positionAttribute = GetVertexAttributeDescriptions()[0];

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

		// The scene pass's second target, the sky's light apart for the ambient occlusion, is written
		// black: the sky isn't occluded. (Blended alike, as without independent blending they must.)
		const std::array<vk::PipelineColorBlendAttachmentState, 2> blendAttachments = { colorBlendAttachment, colorBlendAttachment };
		vk::PipelineColorBlendStateCreateInfo colorBlending{};
		colorBlending.logicOpEnable = VK_FALSE;
		colorBlending.attachmentCount = static_cast<uint32_t>(blendAttachments.size());
		colorBlending.pAttachments = blendAttachments.data();

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
		depthStencil.depthCompareOp = DepthTestNearerOrEqual;
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

	void VulkanRenderer::UpdateSceneUniforms(SceneViewInstance& view, const RenderWorld* world)
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
		ubo.Time = world ? world->Time : SceneTime;
		for (size_t term = 0; term < 9; ++term)
		{
			ubo.Irradiance[term] = glm::vec4(Environment.GetIrradiance(sky)[term], 0.0f);
		}

		// The shadows' cascades, in the part of each layer the view drew into
		const SceneViewShadows& shadows = view.Shadows;
		ubo.CameraForward = glm::vec4(-glm::normalize(glm::vec3(glm::inverse(view.SceneGlobals.View)[2])), 0.0f);
		ubo.ShadowMap = Shadows.GetSlot();
		ubo.ShadowCascadeCount = shadows.CascadeCount;
		ubo.ShadowMapTexel = 1.0f / static_cast<float>(std::max(Shadows.GetSize(), 1u));
		ubo.ShadowMapScale = static_cast<float>(std::min(shadows.Settings.Resolution, Shadows.GetSize())) * ubo.ShadowMapTexel;
		for (uint32_t cascade = 0; cascade < shadows.CascadeCount; ++cascade)
		{
			ubo.ShadowViewProjection[cascade] = shadows.Cascades[cascade].ViewProjection;
			ubo.ShadowSplits[cascade] = shadows.Cascades[cascade].SplitFar;
			ubo.ShadowTexelSizes[cascade] = shadows.Cascades[cascade].TexelSize;
		}
		ubo.ShadowSoftness = shadows.Settings.Softness;
		ubo.ShadowDepthBias = shadows.Settings.DepthBias;
		ubo.ShadowNormalBias = shadows.Settings.NormalBias;
		ubo.ShadowDistance = shadows.Settings.Distance;
		ubo.ShadowDebug = static_cast<uint32_t>(view.ShadowDebug);

		// The world's air
		const SceneViewAtmosphere& air = view.SceneGlobals.Atmosphere;
		ubo.FogColor = glm::vec4(air.FogColor, air.FogDensity);
		ubo.FogShape = glm::vec4(air.FogHeightFalloff, air.FogHeight, air.FogStartDistance, air.FogMaxOpacity);
		ubo.FogSun = glm::vec4(air.FogSunColor, air.FogSunLobe);
		ubo.SkyTint = glm::vec4(air.SkyTint, 0.0f);
		ubo.SunGlow = glm::vec4(air.SunGlowColor, air.SunGlowLobe);

		// Its bloom, the chain's first level
		if (view.Bloom.Settings.bBloom && view.Bloom.Levels > 0)
		{
			ubo.BloomTexture = view.Bloom.Slots.front();
			ubo.BloomIntensity = view.Bloom.Settings.Intensity;
		}

		// Its ambient occlusion; and the projection, for the picture and the depth back to view space
		const glm::mat4& projection = view.SceneGlobals.Projection;
		ubo.ProjectionInfo = glm::vec4(1.0f / projection[0][0], 1.0f / projection[1][1], projection[2][2], projection[3][2]);
		const SceneViewAmbientOcclusion& occlusion = view.AmbientOcclusion;
		if (occlusion.Settings.bAmbientOcclusion && occlusion.Extent.width > 0)
		{
			ubo.AmbientOcclusion = 1;
			ubo.AmbientLightTexture = view.AmbientTextureSlot;
			ubo.SceneDepthTexture = view.DepthTextureSlot;
			ubo.OcclusionTexture = occlusion.Occlusion.Slot;
			ubo.OcclusionDepthTexture = occlusion.Depth.Slot;
			ubo.OcclusionRadius = occlusion.Settings.Radius;
			ubo.OcclusionIntensity = occlusion.Settings.Intensity;
			ubo.OcclusionSlices = occlusion.Settings.Slices;
			ubo.OcclusionSteps = occlusion.Settings.Steps;
		}

		void* mapped = view.SceneUniformBufferMemory.mapMemory(0, sizeof(SceneUBO));
		std::memcpy(mapped, &ubo, sizeof(SceneUBO));
		view.SceneUniformBufferMemory.unmapMemory();
	}

	void VulkanRenderer::CreateTestMeshData()
	{
		CubeMesh.Release();
		CubeMesh.SetLocalBounds(glm::vec3(-0.5f), glm::vec3(0.5f));
		CubeMesh.Upload(Context, Nyx::MakeCube(1.0f));

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
		if (changed.contains("Shadow.slang"))
		{
			Shadows.CreatePipelines(GetPictureRenderPass());
		}
		if (changed.contains("Bloom.slang"))
		{
			Bloom.CreatePipelines();
		}
		if (changed.contains("AmbientOcclusion.slang"))
		{
			AmbientOcclusion.CreatePipelines();
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
		view.SceneGlobals.NearPlane = EditorCamera::NearPlane;
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
			view.SceneGlobals.NearPlane = camera->Camera.NearPlane;
			view.SceneGlobals.CameraWorldPos = camera->Transform.Position;
			view.SceneGlobals.bHasCamera = true;
		}

		if (!view.SceneGlobals.bHasCamera)
		{
			ExtractSceneGlobalsFromEditorCamera(view);
		}
	}

	void VulkanRenderer::ExtractSceneLight(SceneViewInstance& view, const Nyx::Registry& world)
	{
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

				// Its shadows' settings, in range
				SceneViewShadowSettings& shadows = view.SceneGlobals.Shadows;
				shadows.bCastShadows = lightComp.bCastShadows;
				shadows.CascadeCount = static_cast<uint32_t>(std::clamp<int32_t>(lightComp.CascadeCount, 1, static_cast<int32_t>(MaxShadowCascades)));
				shadows.Distance = std::max(lightComp.ShadowDistance, view.SceneGlobals.NearPlane + 1.0f);
				shadows.SplitBlend = std::clamp(lightComp.SplitBlend, 0.0f, 1.0f);
				shadows.DepthBias = std::max(lightComp.DepthBias, 0.0f);
				shadows.NormalBias = std::max(lightComp.NormalBias, 0.0f);
				shadows.Softness = std::clamp(lightComp.FilterSoftness, 0.0f, 3.0f);
				shadows.Resolution = VulkanShadows::ClampResolution(lightComp.ShadowMapResolution);
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
		// The scene's sun, and its shadows, through the editor's camera too; then its air
		ExtractSceneLight(view, registry);
		ExtractSceneEnvironment(view, registry);
	}

	void VulkanRenderer::ExtractSceneEnvironment(SceneViewInstance& view, const Nyx::Registry& world)
	{
		bool bFound = false;
		world.Each<Nyx::EnvironmentComponent>(
			[&](Nyx::Entity, const Nyx::EnvironmentComponent& environment)
			{
				if (bFound)
				{
					return;
				}
				bFound = true;
				view.SceneGlobals.Atmosphere = Nyx::MakeSceneViewAtmosphere(environment, view.SceneGlobals.LightDirectionWS, view.SceneGlobals.LightColor);
				view.SceneGlobals.LightColor = view.SceneGlobals.Atmosphere.SunColor;
				view.SceneGlobals.Bloom = Nyx::MakeSceneViewBloomSettings(environment);
				view.SceneGlobals.AmbientOcclusion = Nyx::MakeSceneViewAmbientOcclusionSettings(environment);
			});
	}

	uint64_t VulkanRenderer::CreateSceneView()
	{
		ASSERT(SceneDescriptorSetLayout != nullptr);

		Nyx::SceneViewInstance view{};
		view.Id = NextSceneViewId++;

		view.RenderTarget.Initialize(Context, 1, 1, vk::Format::eR8G8B8A8Unorm);
		view.HdrTextureSlot = Bindless.AddTexture(view.RenderTarget.GetHdrImageView());
		view.AmbientTextureSlot = Bindless.AddTexture(view.RenderTarget.GetAmbientImageView());
		view.DepthTextureSlot = Bindless.AddTexture(view.RenderTarget.GetDepthImageView());

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
			Bloom.Release(it->Bloom);
			AmbientOcclusion.Release(it->AmbientOcclusion);

			Bindless.RemoveTexture(it->HdrTextureSlot);
			Bindless.RemoveTexture(it->AmbientTextureSlot);
			Bindless.RemoveTexture(it->DepthTextureSlot);
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
		// 0. Its shadow cascades, into the shadow map its scene pass reads
		if (world && view.Shadows.CascadeCount > 0)
		{
			DrawShadowCascades(view, *world, cmd);
		}
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

		// Its ambient occlusion: the sky's light the scene pass wrote apart, occluded, added to the
		// scene
		if (view.AmbientOcclusion.Settings.bAmbientOcclusion && view.AmbientOcclusion.Extent.width > 0)
		{
			WritePassTimestamp(cmd, view, ETimedPass::AmbientOcclusion, false);
			AmbientOcclusion.Draw(view, view.DepthTextureSlot, cmd);
			WritePassTimestamp(cmd, view, ETimedPass::AmbientOcclusion, true);
		}

		// Its bloom, from the scene in HDR, for the tonemap to add
		if (view.Bloom.Settings.bBloom && view.Bloom.Levels > 0)
		{
			WritePassTimestamp(cmd, view, ETimedPass::Bloom, false);
			Bloom.Draw(view, view.HdrTextureSlot, cmd);
			WritePassTimestamp(cmd, view, ETimedPass::Bloom, true);
		}

		// 2. The picture: the scene tonemapped with its bloom, then the editor's overlays over it
		// (grid, debug lines, the selection outline), which look as they always did, then the
		// world's debug drawing
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
		if (view.CullingDebug != ESceneViewCullingDebug::Off)
		{
			DebugLines.Draw(view, view.CullingLines, cmd);
		}
		if (view.ShadowDebug != ESceneViewShadowDebug::Off)
		{
			Shadows.DrawDebug(view, view.Shadows.CascadeCount, cmd);
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
