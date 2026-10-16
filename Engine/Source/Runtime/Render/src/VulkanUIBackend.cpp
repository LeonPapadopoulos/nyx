#include "NyxPCH.h"
#include "VulkanUIBackend.h"

#include "ImageFile.h"
#include "Log.h"
#include "SceneView.h"
#include "ShaderPrograms.h"
#include "UIChrome.h"
#include "UIDiagnostics.h"
#include "UITheme.h"
#include "VulkanContext.h"
#include "VulkanImGuiBackend.h"
#include "VulkanSwapchain.h"
#include "VulkanUtil.h"

#include "backends/imgui_impl_vulkan.h"
#include <imgui_internal.h> // An OS window's ImGui window, for its name

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Nyx
{
	VulkanUIBackend::VulkanUIBackend(VulkanContext& context, VulkanSwapchain& swapchain, Nyx::ShaderPrograms& programs)
		: Context(context)
		, Swapchain(swapchain)
		, Programs(programs)
	{
	}

	VulkanUIBackend::~VulkanUIBackend() = default;

	void VulkanUIBackend::InitializeImGui(GLFWwindow* window)
	{
		ImGuiBackend = std::make_unique<VulkanImGuiBackend>(window, Context, Swapchain);
		ImGuiBackend->Initialize(
			static_cast<float>(Swapchain.GetExtent().width),
			static_cast<float>(Swapchain.GetExtent().height));
	}

	void VulkanUIBackend::InitializePasses()
	{
		// 2D shapes, drawn into their own targets and inside ImGui's windows
		PainterPass.Initialize(Context, Programs);
		Nyx::UI::WindowPainter::SetSegmentDrawer(&VulkanUIBackend::DrawWindowSegment, this);

		// The scene views' pictures, blurred small, for glows and glass
		BlurPass.Initialize(Context, Programs);
		CreateBlurTimestamps();
		CreateUITimestamps();
	}

	void VulkanUIBackend::Shutdown()
	{
		if (GlassImGuiTexture != VK_NULL_HANDLE)
		{
			ImGui_ImplVulkan_RemoveTexture(GlassImGuiTexture);
			GlassImGuiTexture = VK_NULL_HANDLE;
		}
		GlassImGuiTextureGeneration = 0;
		GlassBackdrop.Release();
		BlurTimestamps = nullptr;
		UITimestamps = nullptr;
		BlurPass.Shutdown();

		// Canvases need the ImGui backend to give back their ImGui textures. Other painter targets
		// belong to their users, who release them before the renderer shuts down.
		for (const std::unique_ptr<PainterCanvas>& canvas : PainterCanvases)
		{
			ReleasePainterCanvas(*canvas);
		}
		PainterCanvases.clear();

		// UI images too give back their ImGui textures; a screenshot still pending is written.
		FinishScreenshot();
		Screenshot.reset();
		for (std::vector<std::unique_ptr<UIImage>>* images : { &UIImages, &UIImagesToRelease })
		{
			for (const std::unique_ptr<UIImage>& image : *images)
			{
				ReleaseUIImage(*image);
			}
			images->clear();
		}

		Nyx::UI::WindowPainter::SetSegmentDrawer(nullptr, nullptr);
		WindowSegments.Release();
		WindowSegmentDraws.clear();
		WindowPainters = {};
		RunPainters.clear();
		PainterPass.Shutdown();

		if (ImGuiBackend)
		{
			ImGuiBackend.reset();
		}
	}

	void VulkanUIBackend::RebuildPainterPipelines()
	{
		PainterPass.RebuildPipelines();
	}

	void VulkanUIBackend::RebuildBlurPipeline()
	{
		BlurPass.RebuildPipeline();
	}

	void VulkanUIBackend::ReadLastFrame()
	{
		// The previous frame is done: its screenshot can be written, and the UI images destroyed
		// during it are no longer shown.
		FinishScreenshot();
		ReadBlurTimestamps();
		ReadWindowSegments();
		ReadUITimestamps();
		for (const std::unique_ptr<PainterCanvas>& canvas : PainterCanvases)
		{
			canvas->Target.ReadBack();
		}
		for (const std::unique_ptr<UIImage>& image : UIImagesToRelease)
		{
			ReleaseUIImage(*image);
		}
		UIImagesToRelease.clear();
	}

	void VulkanUIBackend::BeginCommands(vk::CommandBuffer commandBuffer)
	{
		// Before any of this frame's UI timestamps, also those the OS windows' command buffers
		// write: they are submitted after this one.
		if (*UITimestamps)
		{
			commandBuffer.resetQueryPool(*UITimestamps, 0, UITimestampCount);
		}
		WriteUITimestamp(commandBuffer, FramePair, false);
	}

	void VulkanUIBackend::EndCommands(vk::CommandBuffer commandBuffer)
	{
		WriteUITimestamp(commandBuffer, FramePair, true);
	}

	float VulkanUIBackend::TakeStepTime()
	{
		const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
		const float milliseconds = std::chrono::duration<float, std::milli>(now - StepStart).count();
		StepStart = now;
		return milliseconds;
	}

	void VulkanUIBackend::BuildUI(vk::raii::CommandBuffer& cmd, const std::function<void()>& buildUI, const Nyx::UI::PainterWindowStats::Times& rendererTimes)
	{
		StepStart = std::chrono::steady_clock::now();
		ImGuiBackend->BeginFrame();
		buildUI();
		// The frame ends here rather than in ImGui::Render(), so that the windows ImGui begins
		// itself while ending it (drag and drop tooltips) are there when the cards are painted.
		ImGui::EndFrame();
		PendingFrameTimes = rendererTimes;
		PendingFrameTimes.BuildUI = TakeStepTime();
		Nyx::UI::SetGlassBackdrop(GlassImGuiTexture != VK_NULL_HANDLE ? reinterpret_cast<ImTextureID>(GlassImGuiTexture) : ImTextureID_Invalid);
		Nyx::UI::SetPlainGlassBackdrop(GetPlainGlassBackdrop());
		Nyx::UI::PaintWindowCards();
		PendingFrameTimes.Chrome = TakeStepTime();
		// After the chrome, which takes over some of ImGui's shapes
		Nyx::UI::MarkImGuiShapes();
		// The last frame's heat, which the statistics still hold; after the marks, which would
		// mark its rects
		Nyx::UI::ShowPainterHeatmap(WindowPainterStats);
		DrawPainterCanvases(cmd);
		PendingFrameTimes.Canvases = TakeStepTime();
		GatherWindowPainters();
		WindowPainterStats.FrameTimes.Gather = TakeStepTime();
	}

	void VulkanUIBackend::RecordUI(vk::raii::CommandBuffer& cmd, uint32_t imageIndex)
	{
		// With glass in the main window, what lies behind it is drawn and blurred first, so the
		// glass shows this frame's. ImGui::Render() makes the draw data for both; the backend's
		// DrawFrame() finds it made. The window segments are drawn in runs, which only the draw
		// data shows.
		ImGui::Render();
		WindowPainterStats.FrameTimes.ImGuiRender = TakeStepTime();
		UploadWindowSegments(*cmd);
		WindowPainterStats.FrameTimes.Segments = TakeStepTime();
		DrawGlassBackdrop(cmd);

		vk::ClearValue clearValue{};
		clearValue.color = vk::ClearColorValue(std::array<float, 4>{ 0.1f, 0.1f, 0.1f, 1.0f });

		vk::RenderPassBeginInfo renderPassInfo{};
		renderPassInfo.renderPass = *Swapchain.GetRenderPass();
		renderPassInfo.framebuffer = *Swapchain.GetFramebuffers()[imageIndex];
		renderPassInfo.renderArea.offset = vk::Offset2D{ 0, 0 };
		renderPassInfo.renderArea.extent = Swapchain.GetExtent();
		renderPassInfo.clearValueCount = 1;
		renderPassInfo.pClearValues = &clearValue;

		WriteUITimestamp(*cmd, MainWindowUIPair, false);
		cmd.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
		ImGuiBackend->DrawFrame(cmd);
		cmd.endRenderPass();
		WriteUITimestamp(*cmd, MainWindowUIPair, true);
		RecordScreenshot(cmd, imageIndex);
	}

	void VulkanUIBackend::RenderPlatformWindows()
	{
		ImGuiBackend->RenderPlatformWindows();
	}

	void VulkanUIBackend::BeforePresent()
	{
		// From the segments to here: the glass backdrop and the UI pass recorded, the frame
		// submitted, and ImGui's OS windows rendered
		WindowPainterStats.FrameTimes.RecordUI = TakeStepTime();
	}

	void VulkanUIBackend::AfterPresent(float frameMilliseconds)
	{
		WindowPainterStats.FrameTimes.Present = TakeStepTime();
		// The frame as its steps are timed: from the last frame's end to this one's, so that the
		// steps add up to it
		WindowPainterStats.FrameTimes.Frame = frameMilliseconds;
	}

	void VulkanUIBackend::BlurSceneViews(vk::raii::CommandBuffer& cmd, std::span<SceneViewInstance> views)
	{
		// Timed from when the scene views are done to when the blurs are
		if (*BlurTimestamps)
		{
			cmd.resetQueryPool(*BlurTimestamps, 0, 2);
			cmd.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, *BlurTimestamps, 0);
		}

		for (SceneViewInstance& view : views)
		{
			const vk::ImageView picture = view.RenderTarget.GetImageView();
			if (!picture)
			{
				continue;
			}
			BlurPass.Blur(cmd, picture, view.RenderTarget.GetExtent(), view.Blur, Nyx::BlurOptions{});

			// ImGui's texture for the blur points at its image, which changes with the view's size.
			PointImGuiTexture(view.BlurImGuiTexture, view.BlurImGuiTextureGeneration, view.Blur.GetImageView(), view.Blur.GetGeneration());
		}

		if (*BlurTimestamps)
		{
			cmd.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, *BlurTimestamps, 1);
			bBlurTimestampsWritten = true;
		}
	}

	void VulkanUIBackend::PointImGuiTexture(VkDescriptorSet& texture, uint64_t& shownGeneration, vk::ImageView view, uint64_t generation)
	{
		if (!view || generation == shownGeneration)
		{
			return;
		}
		if (texture == VK_NULL_HANDLE)
		{
			texture = ImGui_ImplVulkan_AddTexture(*BlurPass.GetSampler(), view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		}
		else
		{
			// The previous frame has finished (InFlightFence), and ImGui records this frame's use
			// of the set only after this, so the set can change now.
			vk::DescriptorImageInfo imageInfo{ *BlurPass.GetSampler(), view, vk::ImageLayout::eShaderReadOnlyOptimal };
			vk::WriteDescriptorSet write{};
			write.dstSet = texture;
			write.dstBinding = 0;
			write.descriptorCount = 1;
			write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
			write.pImageInfo = &imageInfo;
			Context.GetDevice().updateDescriptorSets(write, {});
		}
		shownGeneration = generation;
	}

	ImTextureID VulkanUIBackend::GetPlainGlassBackdrop()
	{
		if (PlainGlassImageId == 0)
		{
			const glm::vec4 color = Nyx::UI::GetTheme().Backdrop;
			Nyx::RgbaImage pixel;
			pixel.Width = 1;
			pixel.Height = 1;
			for (const float channel : { color.r, color.g, color.b, 1.0f })
			{
				pixel.Pixels.push_back(static_cast<uint8_t>(std::lround(std::clamp(channel, 0.0f, 1.0f) * 255.0f)));
			}
			PlainGlassImageId = CreateUIImage(pixel);
		}
		return GetUIImageTextureId(PlainGlassImageId);
	}

	void VulkanUIBackend::DrawGlassBackdrop(vk::raii::CommandBuffer& cmd)
	{
		const std::span<const ImDrawList* const> glass = Nyx::UI::GetGlassDrawLists();
		ImDrawData* drawData = ImGui::GetMainViewport()->DrawData;
		if (glass.empty() || drawData == nullptr || !drawData->Valid)
		{
			return;
		}

		// What lies behind the glass: the main window's draw lists before the first glass
		// window's. ImGui keeps them in display order, the dock space at the back.
		ImDrawData behind = *drawData;
		const auto firstGlass = std::find_if(drawData->CmdLists.begin(), drawData->CmdLists.end(), [&](const ImDrawList* drawList)
			{
				return std::find(glass.begin(), glass.end(), drawList) != glass.end();
			});
		behind.CmdLists.resize(static_cast<int>(firstGlass - drawData->CmdLists.begin()));
		behind.CmdListsCount = behind.CmdLists.Size;
		behind.TotalVtxCount = 0;
		behind.TotalIdxCount = 0;
		for (const ImDrawList* drawList : behind.CmdLists)
		{
			behind.TotalVtxCount += drawList->VtxBuffer.Size;
			behind.TotalIdxCount += drawList->IdxBuffer.Size;
		}
		// ImGui::Render() has resolved the callbacks' data. The textures (the font atlas) are
		// created and updated by the first draw that has them in its list, which is this one;
		// the main window's draw finds them done.

		// ImGui's backend takes new vertex buffers for each draw, from as many as the swapchain
		// has images; two draws a frame are fine, as each frame waits for the last.
		const vk::Extent2D extent{
			static_cast<uint32_t>(drawData->DisplaySize.x * drawData->FramebufferScale.x),
			static_cast<uint32_t>(drawData->DisplaySize.y * drawData->FramebufferScale.y) };
		WriteUITimestamp(*cmd, GlassBackdropPair, false);
		GlassBackdrop.Record(cmd, Context, BlurPass, Swapchain.GetSurfaceFormat().format, extent, [&](vk::CommandBuffer commandBuffer)
			{
				ImGui_ImplVulkan_RenderDrawData(&behind, commandBuffer);
			});
		WriteUITimestamp(*cmd, GlassBackdropPair, true);
		PointImGuiTexture(GlassImGuiTexture, GlassImGuiTextureGeneration, GlassBackdrop.GetBlurredView(), GlassBackdrop.GetBlurredGeneration());
	}

	void VulkanUIBackend::ReleaseSceneViewBlur(SceneViewInstance& view)
	{
		if (view.BlurImGuiTexture != VK_NULL_HANDLE)
		{
			ImGui_ImplVulkan_RemoveTexture(view.BlurImGuiTexture);
			view.BlurImGuiTexture = VK_NULL_HANDLE;
		}
		view.BlurImGuiTextureGeneration = 0;
		view.Blur.Release();
	}

	void VulkanUIBackend::CreateBlurTimestamps()
	{
		// Not every queue can write timestamps; without them, the blur just isn't timed.
		const std::vector<vk::QueueFamilyProperties> families = Context.GetPhysicalDevice().getQueueFamilyProperties();
		const uint32_t family = Context.GetGraphicsQueueFamily();
		if (family >= families.size() || families[family].timestampValidBits == 0)
		{
			return;
		}

		vk::QueryPoolCreateInfo queryInfo{};
		queryInfo.queryType = vk::QueryType::eTimestamp;
		queryInfo.queryCount = 2;
		BlurTimestamps = vk::raii::QueryPool(Context.GetDevice(), queryInfo);
	}

	void VulkanUIBackend::ReadBlurTimestamps()
	{
		if (!*BlurTimestamps || !bBlurTimestampsWritten)
		{
			return;
		}

		// The frame that wrote them is done, so they are there.
		const auto [result, ticks] = BlurTimestamps.getResults<uint64_t>(0, 2, 2 * sizeof(uint64_t), sizeof(uint64_t), vk::QueryResultFlagBits::e64);
		if (result != vk::Result::eSuccess || ticks.size() != 2)
		{
			return;
		}
		const double nanosecondsPerTick = Context.GetPhysicalDevice().getProperties().limits.timestampPeriod;
		BlurMilliseconds = static_cast<float>(static_cast<double>(ticks[1] - ticks[0]) * nanosecondsPerTick / 1.0e6);
		bBlurTimestampsWritten = false;
	}

	void VulkanUIBackend::CreateUITimestamps()
	{
		// As the blur's: without timestamps, the UI's GPU times are just not there.
		const std::vector<vk::QueueFamilyProperties> families = Context.GetPhysicalDevice().getQueueFamilyProperties();
		const uint32_t family = Context.GetGraphicsQueueFamily();
		if (family >= families.size() || families[family].timestampValidBits == 0)
		{
			return;
		}

		vk::QueryPoolCreateInfo queryInfo{};
		queryInfo.queryType = vk::QueryType::eTimestamp;
		queryInfo.queryCount = UITimestampCount;
		UITimestamps = vk::raii::QueryPool(Context.GetDevice(), queryInfo);
	}

	void VulkanUIBackend::WriteUITimestamp(vk::CommandBuffer commandBuffer, uint32_t pair, bool bEnd, uint32_t osWindow)
	{
		if (!*UITimestamps)
		{
			return;
		}
		commandBuffer.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, *UITimestamps, pair * 2 + (bEnd ? 1 : 0));
		if (bEnd)
		{
			UITimedPairs.push_back(TimedPair{ pair, osWindow });
		}
	}

	void VulkanUIBackend::ReadUITimestamps()
	{
		// The frame that wrote them is done, so they are there. Only the pairs written are read:
		// the others were reset but never written.
		const double nanosecondsPerTick = Context.GetPhysicalDevice().getProperties().limits.timestampPeriod;
		Nyx::UI::PainterWindowStats::Times& times = WindowPainterStats.FrameTimes;
		for (const TimedPair& timed : UITimedPairs)
		{
			const auto [result, ticks] = UITimestamps.getResults<uint64_t>(timed.Pair * 2, 2, 2 * sizeof(uint64_t), sizeof(uint64_t), vk::QueryResultFlagBits::e64);
			if (result != vk::Result::eSuccess || ticks.size() != 2)
			{
				continue;
			}
			const float milliseconds = static_cast<float>(static_cast<double>(ticks[1] - ticks[0]) * nanosecondsPerTick / 1.0e6);
			times.bGpu = true;
			if (timed.Pair == MainWindowUIPair)
			{
				times.GpuMainWindowUI = milliseconds;
			}
			else if (timed.Pair == GlassBackdropPair)
			{
				times.GpuGlassBackdrop = milliseconds;
			}
			else if (timed.Pair == BinningPair)
			{
				times.GpuBinning = milliseconds;
			}
			else if (timed.Pair == EditorPassesPair)
			{
				times.GpuEditorPasses = milliseconds;
			}
			else if (timed.Pair == SceneViewsPair)
			{
				times.GpuSceneViews = milliseconds;
			}
			else if (timed.Pair == FramePair)
			{
				times.GpuFrame = milliseconds;
			}
			else
			{
				times.GpuPainter += milliseconds;
				if (timed.OsWindow < WindowPainterStats.OsWindows.size())
				{
					WindowPainterStats.OsWindows[timed.OsWindow].GpuMilliseconds += milliseconds;
				}
			}
		}
		UITimedPairs.clear();
		NextSegmentPair = FirstSegmentPair;
	}

	uint64_t VulkanUIBackend::CreatePainterCanvas()
	{
		auto canvas = std::make_unique<PainterCanvas>();
		canvas->Id = NextPainterCanvasId++;

		// Nearest filtering: a canvas is shown at its own size, pixel for pixel, and should stay
		// exactly as crisp as it was painted.
		vk::SamplerCreateInfo samplerInfo{};
		samplerInfo.magFilter = vk::Filter::eNearest;
		samplerInfo.minFilter = vk::Filter::eNearest;
		samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
		canvas->Sampler = vk::raii::Sampler(Context.GetDevice(), samplerInfo);

		const uint64_t id = canvas->Id;
		PainterCanvases.push_back(std::move(canvas));
		return id;
	}

	void VulkanUIBackend::DestroyPainterCanvas(uint64_t id)
	{
		const auto found = std::find_if(PainterCanvases.begin(), PainterCanvases.end(), [id](const std::unique_ptr<PainterCanvas>& canvas)
			{
				return canvas->Id == id;
			});
		if (found == PainterCanvases.end())
		{
			return;
		}

		// The last frame may still be painting the canvas or showing it.
		Context.WaitIdle();
		ReleasePainterCanvas(**found);
		PainterCanvases.erase(found);
	}

	void VulkanUIBackend::ReleasePainterCanvas(PainterCanvas& canvas)
	{
		if (canvas.ImGuiTexture != VK_NULL_HANDLE)
		{
			ImGui_ImplVulkan_RemoveTexture(canvas.ImGuiTexture);
			canvas.ImGuiTexture = VK_NULL_HANDLE;
			canvas.ImGuiTextureGeneration = 0;
		}
		canvas.Target.Release();
		canvas.Sampler = nullptr;
	}

	void VulkanUIBackend::SubmitPainterCanvas(uint64_t id, const Nyx::UI::Painter& painter, const Nyx::UI::PainterDrawOptions& options)
	{
		if (PainterCanvas* canvas = FindPainterCanvas(id))
		{
			canvas->Frame = painter;
			canvas->Options = options;
			canvas->bSubmitted = true;
		}
	}

	ImTextureID VulkanUIBackend::GetPainterCanvasTextureId(uint64_t id) const
	{
		// ImGui's "no texture" is ImTextureID_Invalid, which is -1 since ImGui 1.92.7, not 0.
		const PainterCanvas* canvas = FindPainterCanvas(id);
		if (canvas == nullptr || canvas->ImGuiTexture == VK_NULL_HANDLE)
		{
			return ImTextureID_Invalid;
		}
		return reinterpret_cast<ImTextureID>(canvas->ImGuiTexture);
	}

	std::optional<Nyx::UI::PainterTiles> VulkanUIBackend::GetPainterCanvasTiles(uint64_t id) const
	{
		const PainterCanvas* canvas = FindPainterCanvas(id);
		if (canvas == nullptr || !canvas->Target.GetImage())
		{
			return std::nullopt;
		}
		return canvas->Target.GetTiles();
	}

	void VulkanUIBackend::DrawPainterCanvases(vk::raii::CommandBuffer& cmd)
	{
		for (const std::unique_ptr<PainterCanvas>& canvasPointer : PainterCanvases)
		{
			PainterCanvas& canvas = *canvasPointer;
			if (!canvas.bSubmitted)
			{
				continue;
			}
			canvas.bSubmitted = false;

			PainterPass.Draw(cmd, canvas.Frame, canvas.Target, canvas.Options);

			// ImGui's texture for the canvas points at the target's image: a new texture on the
			// first draw, and the same texture pointed at the new image after a resize. The
			// target's generation tells a new image; its view's handle may be the old one's.
			const vk::ImageView imageView = canvas.Target.GetImageView();
			const uint64_t generation = canvas.Target.GetGeneration();
			if (!imageView || generation == canvas.ImGuiTextureGeneration)
			{
				continue;
			}
			if (canvas.ImGuiTexture == VK_NULL_HANDLE)
			{
				canvas.ImGuiTexture = ImGui_ImplVulkan_AddTexture(*canvas.Sampler, imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			}
			else
			{
				// The previous frame has finished (InFlightFence, OS windows included), and ImGui
				// records this frame's use of the set only after this, so the set can change now.
				vk::DescriptorImageInfo imageInfo{ *canvas.Sampler, imageView, vk::ImageLayout::eShaderReadOnlyOptimal };
				vk::WriteDescriptorSet write{};
				write.dstSet = canvas.ImGuiTexture;
				write.dstBinding = 0;
				write.descriptorCount = 1;
				write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
				write.pImageInfo = &imageInfo;
				Context.GetDevice().updateDescriptorSets(write, {});
			}
			canvas.ImGuiTextureGeneration = generation;
		}
	}

	VulkanUIBackend::PainterCanvas* VulkanUIBackend::FindPainterCanvas(uint64_t id)
	{
		for (const std::unique_ptr<PainterCanvas>& canvas : PainterCanvases)
		{
			if (canvas->Id == id)
			{
				return canvas.get();
			}
		}
		return nullptr;
	}

	const VulkanUIBackend::PainterCanvas* VulkanUIBackend::FindPainterCanvas(uint64_t id) const
	{
		return const_cast<VulkanUIBackend*>(this)->FindPainterCanvas(id);
	}

	void VulkanUIBackend::GatherWindowPainters()
	{
		WindowPainterStats = Nyx::UI::PainterWindowStats{};
		WindowPainterStats.FrameTimes = PendingFrameTimes;
		WindowPainters = Nyx::UI::EndWindowPainters();
		for (const Nyx::UI::WindowPainter* painter : WindowPainters)
		{
			for (const std::string& problem : painter->GetProblems())
			{
				WindowPainterStats.Problems.push_back(painter->GetWindowName() + ": " + problem);
			}
			WindowPainterStats.Windows += painter->GetSegments().empty() ? 0 : 1;
		}
	}

	void VulkanUIBackend::ReadWindowSegments()
	{
		// The last frame's binning, which the GPU has finished: its numbers go into the last
		// frame's statistics, which the UI built next shows.
		WindowSegments.ReadBack();
		for (uint32_t draw = 0; draw < WindowSegments.GetSegmentCount() && draw < WindowSegmentDrawOsWindows.size(); ++draw)
		{
			const uint32_t osWindowIndex = WindowSegmentDrawOsWindows[draw];
			if (osWindowIndex >= WindowPainterStats.OsWindows.size())
			{
				continue;
			}
			Nyx::UI::PainterWindowStats::OsWindow& osWindow = WindowPainterStats.OsWindows[osWindowIndex];
			const Nyx::UI::PainterBinningStats& stats = WindowSegments.GetStats(draw);
			osWindow.TilesInUse += stats.TilesInUse;
			osWindow.TileEntries += stats.TileEntries;
			WindowPainterStats.TilesInUse += stats.TilesInUse;
			WindowPainterStats.TileEntries += stats.TileEntries;
			// The heat of the tiles read back, while the heatmap is on
			if (osWindow.Heat.TilesX > 0)
			{
				WindowSegments.AddHeat(draw, osWindow.Heat);
			}
		}
		WindowPainterStats.Reduction = WindowSegments.GetReduction();
		WindowPainterStats.Problems.insert(WindowPainterStats.Problems.end(), WindowSegments.GetReadBackProblems().begin(), WindowSegments.GetReadBackProblems().end());
	}

	void VulkanUIBackend::UploadWindowSegments(vk::CommandBuffer commandBuffer)
	{
		WindowSegments.Clear();
		WindowSegmentDraws.clear();
		WindowSegmentDrawOsWindows.clear();
		// The heatmap needs the tiles read back.
		const bool bHeat = Nyx::UI::GetPainterHeatmap() != Nyx::UI::EPainterHeatmap::Off;
		WindowSegments.SetReadBackTiles(bHeat);

		size_t runPainterCount = 0;
		// A segment's commands, if it is one of this frame's painters'
		const auto findSegment = [&](const Nyx::UI::WindowSegmentRef& ref) -> const Nyx::UI::PainterSegment*
		{
			const bool bThisFrame = std::find(WindowPainters.begin(), WindowPainters.end(), ref.Painter) != WindowPainters.end();
			return bThisFrame && ref.Segment < ref.Painter->GetSegments().size() ? &ref.Painter->GetSegments()[ref.Segment] : nullptr;
		};

		for (ImGuiViewport* viewport : ImGui::GetPlatformIO().Viewports)
		{
			if (viewport->DrawData == nullptr)
			{
				continue;
			}
			Nyx::UI::PainterWindowStats::OsWindow osWindow;
			const ImGuiWindow* window = static_cast<ImGuiViewportP*>(viewport)->Window;
			osWindow.Name = viewport == ImGui::GetMainViewport() ? "Main window" : window != nullptr ? window->Name : "OS window";
			// Where it goes in the statistics, if it paints
			const uint32_t osWindowIndex = static_cast<uint32_t>(WindowPainterStats.OsWindows.size());
			osWindow.ViewportId = viewport->ID;
			osWindow.PixelsPerPoint = viewport->DrawData->FramebufferScale.x;
			// The heatmap's tiles cover the whole target, as the segments' grids are placed on it;
			// filled once the GPU has binned them (ReadWindowSegments()).
			if (bHeat)
			{
				osWindow.Heat.Reset(static_cast<uint32_t>(viewport->DrawData->DisplaySize.x * osWindow.PixelsPerPoint),
					static_cast<uint32_t>(viewport->DrawData->DisplaySize.y * viewport->DrawData->FramebufferScale.y));
			}

			// What ImGui still draws itself here; the painter's segments are callbacks.
			for (const ImDrawList* drawList : viewport->DrawData->CmdLists)
			{
				for (const ImDrawCmd& command : drawList->CmdBuffer)
				{
					osWindow.ImGuiDrawCommands += command.UserCallback == nullptr && command.ElemCount > 0 ? 1 : 0;
				}
				osWindow.ImGuiVertices += static_cast<uint32_t>(drawList->VtxBuffer.Size);
			}

			// Runs don't reach across where glass cuts the main window's draw data.
			for (const Nyx::UI::WindowSegmentRun& run : Nyx::UI::FindWindowSegmentRuns(*viewport->DrawData, Nyx::UI::GetGlassDrawLists()))
			{
				std::vector<Nyx::UI::WindowSegmentRef> refs;
				for (const Nyx::UI::WindowSegmentRef& ref : run.Segments)
				{
					if (findSegment(ref) != nullptr)
					{
						refs.push_back(ref);
					}
				}

				// As many segments as one painter has room for the textures of go into one draw;
				// a segment alone is added as it is.
				for (size_t first = 0; first < refs.size();)
				{
					const Nyx::UI::Painter& firstPainter = refs[first].Painter->GetPainter();
					size_t count = 1;
					uint32_t draw = 0;
					if (first + 1 < refs.size())
					{
						if (runPainterCount == RunPainters.size())
						{
							RunPainters.push_back(std::make_unique<Nyx::UI::Painter>());
						}
						Nyx::UI::Painter& runPainter = *RunPainters[runPainterCount];
						runPainter.BeginFrame(firstPainter.GetTargetWidth(), firstPainter.GetTargetHeight());
						runPainter.AppendSegment(firstPainter, *findSegment(refs[first]));
						while (first + count < refs.size() && runPainter.AppendSegment(refs[first + count].Painter->GetPainter(), *findSegment(refs[first + count])))
						{
							++count;
						}
						if (count > 1)
						{
							++runPainterCount;
							draw = WindowSegments.Add(runPainter, Nyx::UI::PainterSegment{ 0, static_cast<uint32_t>(runPainter.GetCommands().size()) });
						}
					}
					if (count == 1)
					{
						draw = WindowSegments.Add(firstPainter, *findSegment(refs[first]));
					}

					WindowSegmentDraws[{ refs[first].Painter, refs[first].Segment }] = draw;
					for (size_t other = first + 1; other < first + count; ++other)
					{
						WindowSegmentDraws[{ refs[other].Painter, refs[other].Segment }] = NoWindowDraw;
					}

					// Draws are added one after the other: a draw's index is its place here.
					WindowSegmentDrawOsWindows.push_back(osWindowIndex);

					// The measuring's numbers now; the GPU's once it has binned (ReadWindowSegments())
					const Nyx::UI::PainterBinningStats& stats = WindowSegments.GetStats(draw);
					++osWindow.Draws;
					osWindow.Segments += static_cast<uint32_t>(count);
					osWindow.Commands += stats.Commands;
					osWindow.BinningMilliseconds += stats.BinningMilliseconds;
					WindowPainterStats.Commands += stats.Commands;
					WindowPainterStats.BinningMilliseconds += stats.BinningMilliseconds;
					first += count;
				}
			}

			WindowPainterStats.Segments += osWindow.Segments;
			WindowPainterStats.Draws += osWindow.Draws;
			if (osWindow.Segments > 0)
			{
				WindowPainterStats.OsWindows.push_back(std::move(osWindow));
			}
		}
		WindowPainterStats.Problems.insert(WindowPainterStats.Problems.end(), WindowSegments.GetProblems().begin(), WindowSegments.GetProblems().end());
		WindowPainterStats.UploadBytes = WindowSegments.GetUploadBytes();

		// Binned on the GPU before anything draws them, in this command buffer or the OS windows'
		// later ones
		WriteUITimestamp(commandBuffer, BinningPair, false);
		PainterPass.Bin(commandBuffer, WindowSegments);
		WriteUITimestamp(commandBuffer, BinningPair, true);
	}

	void VulkanUIBackend::DrawWindowSegment(const Nyx::UI::WindowPainter& painter, uint32_t segment, const ImDrawCmd&, void* backend)
	{
		static_cast<VulkanUIBackend*>(backend)->DrawWindowSegment(painter, segment);
	}

	void VulkanUIBackend::DrawWindowSegment(const Nyx::UI::WindowPainter& painter, uint32_t segment)
	{
		// ImGui's Vulkan backend hands over the command buffer it renders the viewport with, in
		// its render pass.
		const auto* renderState = static_cast<const ImGui_ImplVulkan_RenderState*>(ImGui::GetPlatformIO().Renderer_RenderState);
		if (renderState == nullptr)
		{
			return;
		}

		// Only this frame's segments are uploaded, each run at its first segment.
		const auto found = WindowSegmentDraws.find({ &painter, segment });
		if (found == WindowSegmentDraws.end() || found->second == NoWindowDraw)
		{
			return;
		}

		ImGuiViewport* viewport = ImGui::FindViewportByID(painter.GetViewportId());
		if (viewport == nullptr || viewport->DrawData == nullptr)
		{
			return;
		}

		// The framebuffer's size, worked out as ImGui's backend does
		const ImDrawData& drawData = *viewport->DrawData;
		const vk::Extent2D extent{
			static_cast<uint32_t>(drawData.DisplaySize.x * drawData.FramebufferScale.x),
			static_cast<uint32_t>(drawData.DisplaySize.y * drawData.FramebufferScale.y) };

		// The main window renders into the swapchain; an OS window into its own, in the format
		// ImGui's backend chose for it.
		vk::Format format = Swapchain.GetSurfaceFormat().format;
		if (viewport != ImGui::GetMainViewport())
		{
			const ImGui_ImplVulkanH_Window* window = ImGui_ImplVulkanH_GetWindowDataFromViewport(viewport);
			if (window == nullptr)
			{
				return;
			}
			format = static_cast<vk::Format>(window->SurfaceFormat.format);
		}

		// Timed while pairs are left
		const bool bTimed = *UITimestamps && NextSegmentPair < FirstSegmentPair + MaxTimedSegmentDraws && found->second < WindowSegmentDrawOsWindows.size();
		const uint32_t pair = bTimed ? NextSegmentPair++ : 0;
		if (bTimed)
		{
			WriteUITimestamp(renderState->CommandBuffer, pair, false);
		}
		PainterPass.DrawSegment(renderState->CommandBuffer, WindowSegments, found->second, format, extent);
		if (bTimed)
		{
			WriteUITimestamp(renderState->CommandBuffer, pair, true, WindowSegmentDrawOsWindows[found->second]);
		}
	}

	uint64_t VulkanUIBackend::CreateUIImage(const Nyx::RgbaImage& source)
	{
		if (source.Width == 0 || source.Height == 0 || source.Pixels.size() != static_cast<size_t>(source.Width) * source.Height * 4)
		{
			LOG_ERROR("CreateUIImage(): the image has no pixels, or not as many as its size needs");
			return 0;
		}

		vk::raii::Device& device = Context.GetDevice();
		auto image = std::make_unique<UIImage>();
		image->Id = NextUIImageId++;

		// Not sRGB: ImGui blends the sRGB-encoded values as they are, and so do the painter and this image.
		constexpr vk::Format Format = vk::Format::eR8G8B8A8Unorm;
		vk::ImageCreateInfo imageInfo{};
		imageInfo.imageType = vk::ImageType::e2D;
		imageInfo.extent = vk::Extent3D(source.Width, source.Height, 1);
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = Format;
		imageInfo.tiling = vk::ImageTiling::eOptimal;
		imageInfo.initialLayout = vk::ImageLayout::eUndefined;
		imageInfo.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
		imageInfo.samples = vk::SampleCountFlagBits::e1;
		image->Image = vk::raii::Image(device, imageInfo);
		const vk::MemoryRequirements imageRequirements = image->Image.getMemoryRequirements();
		image->Memory = vk::raii::DeviceMemory(device, vk::MemoryAllocateInfo(imageRequirements.size,
			FindMemoryType(*Context.GetPhysicalDevice(), imageRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)));
		image->Image.bindMemory(*image->Memory, 0);

		vk::ImageViewCreateInfo viewInfo{};
		viewInfo.image = *image->Image;
		viewInfo.viewType = vk::ImageViewType::e2D;
		viewInfo.format = Format;
		viewInfo.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
		image->View = vk::raii::ImageView(device, viewInfo);

		// Smooth when shown scaled
		vk::SamplerCreateInfo samplerInfo{};
		samplerInfo.magFilter = vk::Filter::eLinear;
		samplerInfo.minFilter = vk::Filter::eLinear;
		samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
		image->Sampler = vk::raii::Sampler(device, samplerInfo);

		// The pixels go through a host-visible buffer, in a short submission of its own. Images are
		// made rarely (a file opened), so waiting for it is fine.
		vk::raii::Buffer staging{ nullptr };
		vk::raii::DeviceMemory stagingMemory{ nullptr };
		CreateBuffer(Context, source.Pixels.size(), vk::BufferUsageFlagBits::eTransferSrc,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, staging, stagingMemory);
		std::memcpy(stagingMemory.mapMemory(0, source.Pixels.size()), source.Pixels.data(), source.Pixels.size());
		stagingMemory.unmapMemory();

		vk::CommandBufferAllocateInfo commandBufferInfo{};
		commandBufferInfo.commandPool = *Context.GetGraphicsCommandPool();
		commandBufferInfo.level = vk::CommandBufferLevel::ePrimary;
		commandBufferInfo.commandBufferCount = 1;
		vk::raii::CommandBuffer upload = std::move(vk::raii::CommandBuffers(device, commandBufferInfo).front());
		upload.begin(vk::CommandBufferBeginInfo{ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

		const vk::ImageSubresourceRange wholeImage(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
		TransitionImageLayout(*upload, *image->Image, wholeImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
			vk::PipelineStageFlagBits::eTopOfPipe, {}, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite);

		vk::BufferImageCopy copy{};
		copy.imageSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
		copy.imageExtent = imageInfo.extent;
		upload.copyBufferToImage(*staging, *image->Image, vk::ImageLayout::eTransferDstOptimal, copy);

		TransitionImageLayout(*upload, *image->Image, wholeImage, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
			vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eShaderRead);
		upload.end();

		vk::SubmitInfo submitInfo{};
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &*upload;
		Context.GetGraphicsQueue().submit(submitInfo);
		Context.GetGraphicsQueue().waitIdle();

		image->ImGuiTexture = ImGui_ImplVulkan_AddTexture(*image->Sampler, *image->View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		const uint64_t id = image->Id;
		UIImages.push_back(std::move(image));
		return id;
	}

	void VulkanUIBackend::DestroyUIImage(uint64_t id)
	{
		const auto found = std::find_if(UIImages.begin(), UIImages.end(), [&](const std::unique_ptr<UIImage>& image)
			{
				return image->Id == id;
			});
		if (found != UIImages.end())
		{
			UIImagesToRelease.push_back(std::move(*found));
			UIImages.erase(found);
		}
	}

	ImTextureID VulkanUIBackend::GetUIImageTextureId(uint64_t id) const
	{
		for (const std::unique_ptr<UIImage>& image : UIImages)
		{
			if (image->Id == id && image->ImGuiTexture != VK_NULL_HANDLE)
			{
				return reinterpret_cast<ImTextureID>(image->ImGuiTexture);
			}
		}
		return ImTextureID_Invalid;
	}

	void VulkanUIBackend::ReleaseUIImage(UIImage& image)
	{
		if (image.ImGuiTexture != VK_NULL_HANDLE)
		{
			ImGui_ImplVulkan_RemoveTexture(image.ImGuiTexture);
			image.ImGuiTexture = VK_NULL_HANDLE;
		}
		image.Sampler = nullptr;
		image.View = nullptr;
		image.Image = nullptr;
		image.Memory = nullptr;
	}

	void VulkanUIBackend::SaveScreenshot(const std::filesystem::path& path, ImVec2 min, ImVec2 max)
	{
		if (Screenshot)
		{
			ScreenshotMessage = "A screenshot is being saved already";
			return;
		}
		if (!Swapchain.CanCopyImages())
		{
			ScreenshotMessage = "Couldn't save: this GPU can't copy out of the window's images";
			return;
		}
		const vk::Format format = Swapchain.GetSurfaceFormat().format;
		const bool bBlueFirst = format == vk::Format::eB8G8R8A8Unorm || format == vk::Format::eB8G8R8A8Srgb;
		const bool bRedFirst = format == vk::Format::eR8G8B8A8Unorm || format == vk::Format::eR8G8B8A8Srgb;
		if (!bBlueFirst && !bRedFirst)
		{
			ScreenshotMessage = "Couldn't save: the window's images aren't 8 bits per channel";
			return;
		}

		// ImGui's screen coordinates to the main window's pixels, whole ones, inside the window
		const ImGuiViewport& viewport = *ImGui::GetMainViewport();
		const float scale = viewport.FramebufferScale.x > 0.0f ? viewport.FramebufferScale.x : 1.0f;
		const vk::Extent2D extent = Swapchain.GetExtent();
		const auto toPixels = [&](float position, float origin, uint32_t size, float (*round)(float))
			{
				return static_cast<int32_t>(std::clamp(round((position - origin) * scale), 0.0f, static_cast<float>(size)));
			};
		const int32_t left = toPixels(min.x, viewport.Pos.x, extent.width, std::floor);
		const int32_t top = toPixels(min.y, viewport.Pos.y, extent.height, std::floor);
		const int32_t right = toPixels(max.x, viewport.Pos.x, extent.width, std::ceil);
		const int32_t bottom = toPixels(max.y, viewport.Pos.y, extent.height, std::ceil);
		if (right <= left || bottom <= top)
		{
			ScreenshotMessage = "Couldn't save: the rect isn't inside the main window";
			return;
		}

		Screenshot.emplace();
		Screenshot->Path = path;
		Screenshot->Region = vk::Rect2D({ left, top }, { static_cast<uint32_t>(right - left), static_cast<uint32_t>(bottom - top) });
		Screenshot->bBlueFirst = bBlueFirst;
		ScreenshotMessage = "Saving " + path.string() + " ...";
	}

	void VulkanUIBackend::RecordScreenshot(vk::raii::CommandBuffer& cmd, uint32_t imageIndex)
	{
		if (!Screenshot || Screenshot->bRecorded)
		{
			return;
		}
		ScreenshotRequest& request = *Screenshot;
		vk::raii::Device& device = Context.GetDevice();

		const vk::DeviceSize byteCount = static_cast<vk::DeviceSize>(request.Region.extent.width) * request.Region.extent.height * 4;
		CreateBuffer(Context, byteCount, vk::BufferUsageFlagBits::eTransferDst,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, request.Buffer, request.Memory);

		// The render pass left the image ready to present; it is copied from, then made ready again.
		const vk::Image swapchainImage = Swapchain.GetImages()[imageIndex];
		const vk::ImageSubresourceRange wholeImage(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
		TransitionImageLayout(*cmd, swapchainImage, wholeImage, vk::ImageLayout::ePresentSrcKHR, vk::ImageLayout::eTransferSrcOptimal,
			vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::AccessFlagBits::eColorAttachmentWrite, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferRead);

		vk::BufferImageCopy copy{};
		copy.imageSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
		copy.imageOffset = vk::Offset3D(request.Region.offset.x, request.Region.offset.y, 0);
		copy.imageExtent = vk::Extent3D(request.Region.extent.width, request.Region.extent.height, 1);
		cmd.copyImageToBuffer(swapchainImage, vk::ImageLayout::eTransferSrcOptimal, *request.Buffer, copy);

		TransitionImageLayout(*cmd, swapchainImage, wholeImage, vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::ePresentSrcKHR,
			vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferRead, vk::PipelineStageFlagBits::eBottomOfPipe, {});

		request.bRecorded = true;
	}

	void VulkanUIBackend::FinishScreenshot()
	{
		if (!Screenshot || !Screenshot->bRecorded)
		{
			return;
		}
		const ScreenshotRequest& request = *Screenshot;

		Nyx::RgbaImage image;
		image.Width = request.Region.extent.width;
		image.Height = request.Region.extent.height;
		image.Pixels.resize(static_cast<size_t>(image.Width) * image.Height * 4);
		std::memcpy(image.Pixels.data(), request.Memory.mapMemory(0, image.Pixels.size()), image.Pixels.size());
		request.Memory.unmapMemory();

		// RGBA, and opaque: the window's alpha is whatever blending left there, not meant to be seen.
		for (size_t offset = 0; offset < image.Pixels.size(); offset += 4)
		{
			if (request.bBlueFirst)
			{
				std::swap(image.Pixels[offset], image.Pixels[offset + 2]);
			}
			image.Pixels[offset + 3] = 255;
		}

		std::string error;
		ScreenshotMessage = Nyx::SavePng(request.Path, image, error) ? "Saved " + request.Path.string() : "Couldn't save: " + error;
		Screenshot.reset();
	}
}
