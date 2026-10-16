#pragma once

#include "Painter.h"
#include "PainterDrawOptions.h"
#include "PainterTiles.h"
#include "VulkanBlur.h"
#include "VulkanGlass.h"
#include "VulkanPainter.h"
#include "WindowPainter.h"

#include <imgui.h>
#include <vulkan/vulkan_raii.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

struct GLFWwindow;

namespace Nyx
{
	class ShaderPrograms;
	class VulkanContext;
	class VulkanImGuiBackend;
	class VulkanSwapchain;
	struct RgbaImage;
	struct SceneViewInstance;

	// What the renderer draws the UI with: ImGui and its OS windows, the painter's canvases and the
	// painting inside ImGui's windows, glass and the scene views' blur, UI images, screenshots, and
	// the GPU times of the frame. VulkanRenderer calls it at fixed points of each frame.
	class VulkanUIBackend
	{
	public:
		// The renderer's; they outlive the backend
		VulkanUIBackend(VulkanContext& context, VulkanSwapchain& swapchain, Nyx::ShaderPrograms& programs);
		~VulkanUIBackend();

		// ImGui, once the swapchain is made
		void InitializeImGui(GLFWwindow* window);
		// The painter and blur passes, the window painters' drawer, and the GPU timestamps
		void InitializePasses();
		// Gives back every ImGui texture, writes a screenshot still pending, and ends ImGui. The
		// scene views' blurs are released before (ReleaseSceneViewBlur()).
		void Shutdown();

		// After their shaders changed
		void RebuildPainterPipelines();
		void RebuildBlurPipeline();

		// Once the last frame's fence is signalled: its screenshot written, its GPU times and the
		// painter's binning read, the canvases read back, the UI images it showed released
		void ReadLastFrame();

		// At the start of the frame's command buffer, and at its end: the frame's GPU time
		void BeginCommands(vk::CommandBuffer commandBuffer);
		void EndCommands(vk::CommandBuffer commandBuffer);

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
		// Writes a pair's start before what it times, or its end after it, which keeps the pair to
		// read, with its OS window for a segment draw. Nothing without UITimestamps.
		void WriteUITimestamp(vk::CommandBuffer commandBuffer, uint32_t pair, bool bEnd, uint32_t osWindow = UINT32_MAX);

		// Every scene view's picture blurred small, once the views have rendered, before the UI is
		// built: the UI shows this frame's blur, for glows and glass
		void BlurSceneViews(vk::raii::CommandBuffer& cmd, std::span<SceneViewInstance> views);
		void ReleaseSceneViewBlur(SceneViewInstance& view);

		// The frame's UI, before the main window's render pass starts: ImGui's frame with
		// buildUI() in it, the painted chrome, the canvases submitted while building it (drawn
		// outside any render pass, so ImGui shows them in this frame), and the window painters
		// ended. rendererTimes holds the frame's steps before the UI, for the painter's statistics.
		void BuildUI(vk::raii::CommandBuffer& cmd, const std::function<void()>& buildUI, const Nyx::UI::PainterWindowStats::Times& rendererTimes);

		// The main window's UI into the swapchain image: ImGui's draw data, the window painters'
		// segments, the glass backdrop, and a screenshot asked for
		void RecordUI(vk::raii::CommandBuffer& cmd, uint32_t imageIndex);

		// ImGui windows outside the main window, which ImGui submits itself: after the frame's
		// command buffer, so that the images they show (scene views, painter canvases) are painted
		// first, in queue order
		void RenderPlatformWindows();

		// The frame's last two steps, for the painter's statistics: recording the UI up to
		// presenting, and presenting. frameMilliseconds is the whole frame, from the last one's end.
		void BeforePresent();
		void AfterPresent(float frameMilliseconds);

		// What IRenderer offers of the UI; VulkanRenderer hands them on
		uint64_t CreatePainterCanvas();
		void DestroyPainterCanvas(uint64_t id);
		void SubmitPainterCanvas(uint64_t id, const Nyx::UI::Painter& painter, const Nyx::UI::PainterDrawOptions& options);
		ImTextureID GetPainterCanvasTextureId(uint64_t id) const;
		std::optional<Nyx::UI::PainterTiles> GetPainterCanvasTiles(uint64_t id) const;
		const Nyx::UI::PainterWindowStats& GetWindowPainterStats() const { return WindowPainterStats; }

		uint64_t CreateUIImage(const Nyx::RgbaImage& image);
		void DestroyUIImage(uint64_t id);
		ImTextureID GetUIImageTextureId(uint64_t id) const;

		void SaveScreenshot(const std::filesystem::path& path, ImVec2 min, ImVec2 max);
		const std::string& GetScreenshotMessage() const { return ScreenshotMessage; }

		float GetBlurMilliseconds() const { return BlurMilliseconds; }

		// Draws 2D shapes recorded with a Painter into a VulkanPainterTarget
		Nyx::VulkanPainter& GetPainterPass() { return PainterPass; }

	private:
		VulkanContext& Context;
		VulkanSwapchain& Swapchain;
		Nyx::ShaderPrograms& Programs;

		std::unique_ptr<VulkanImGuiBackend> ImGuiBackend;

		// The UI's steps as they are timed, from BuildUI() on: since the last step's end
		std::chrono::steady_clock::time_point StepStart;
		float TakeStepTime();

		Nyx::VulkanPainter PainterPass;

		// Blurs every scene view's picture after it renders
		Nyx::VulkanBlur BlurPass;

		// Points an ImGui texture at an image: a new texture the first time, the same texture
		// pointed at the new image when it changed (e.g. a blur target resized). An image is told
		// by its target's generation, not by its view: a new view may get the handle of the old
		// one, and the texture would go on showing the destroyed image. `shownGeneration` is the
		// generation the texture shows now.
		void PointImGuiTexture(VkDescriptorSet& texture, uint64_t& shownGeneration, vk::ImageView view, uint64_t generation);

		// What glass shows: ImGui's main window drawn up to the first glass window, blurred, each
		// frame there is glass, before the main window itself is drawn. Its ImGui texture keeps its
		// ID.
		Nyx::VulkanGlassBackdrop GlassBackdrop;
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
			Nyx::VulkanPainterTarget Target;

			// This frame's shapes, copied from SubmitPainterCanvas(); drawn once, then cleared
			Nyx::UI::Painter Frame;
			Nyx::UI::PainterDrawOptions Options;
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
		static void DrawWindowSegment(const Nyx::UI::WindowPainter& painter, uint32_t segment, const ImDrawCmd& command, void* backend);
		void DrawWindowSegment(const Nyx::UI::WindowPainter& painter, uint32_t segment);

		// This frame's window painters, and their draws
		std::span<Nyx::UI::WindowPainter* const> WindowPainters;
		Nyx::VulkanPainterSegments WindowSegments;
		// What each segment's callback draws: the index of its run's draw in WindowSegments at the
		// run's first segment, NoWindowDraw at the others
		static constexpr uint32_t NoWindowDraw = UINT32_MAX;
		std::map<std::pair<const Nyx::UI::WindowPainter*, uint32_t>, uint32_t> WindowSegmentDraws;
		// The painters runs are copied into, one per draw of several segments. Kept apart, so
		// that no two have the same address in a frame: WindowSegments tells painters apart by it.
		std::vector<std::unique_ptr<Nyx::UI::Painter>> RunPainters;
		// The OS window of each draw in WindowSegments, as an index into WindowPainterStats.OsWindows
		std::vector<uint32_t> WindowSegmentDrawOsWindows;
		// The last frame's statistics: its CPU times as the frame is built, its GPU times once its
		// fence has passed
		Nyx::UI::PainterWindowStats WindowPainterStats;
		// This frame's CPU times taken before GatherWindowPainters() starts its statistics
		Nyx::UI::PainterWindowStats::Times PendingFrameTimes;

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
	};
}
