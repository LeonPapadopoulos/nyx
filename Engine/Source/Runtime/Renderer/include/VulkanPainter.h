#pragma once

#include "Painter.h"
#include "PainterDrawOptions.h"
#include "PainterTiles.h"

#include <glm/glm.hpp>
#include <imgui.h>
#include <vulkan/vulkan_raii.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Nyx
{
	class VulkanContext;
}

namespace Nyx::Engine
{
	class VulkanPainter;

	// The four storage buffers the painter shader reads (set 0: commands, groups, tiles, tile
	// entries), written by the CPU every frame, and the descriptor set that points at them. The
	// buffers only ever grow.
	struct VulkanPainterBuffers
	{
		struct UploadBuffer
		{
			vk::raii::Buffer Buffer{ nullptr };
			vk::raii::DeviceMemory Memory{ nullptr };
			void* Mapped = nullptr;
			vk::DeviceSize Size = 0;
		};

		UploadBuffer Commands;
		UploadBuffer Groups;
		UploadBuffer Tiles;
		UploadBuffer Entries;
		vk::raii::DescriptorSet DescriptorSet{ nullptr };

		void Release();
	};

	// An image the painter draws into, with everything one painted frame needs on the GPU: the
	// commands, groups, tiles and tile entries, uploaded each frame.
	//
	// Empty until VulkanPainter::Draw() first draws into it, which also gives it the painter's
	// size. Release it (or let it go) before the Vulkan device is destroyed.
	class VulkanPainterTarget
	{
	public:
		// The painted image, in ShaderReadOnlyOptimal layout once drawn; null before the first
		// draw. It changes when the size changes, so read it after each draw.
		vk::Image GetImage() const { return *Image; }
		vk::ImageView GetImageView() const { return *ImageView; }
		vk::Extent2D GetExtent() const { return Extent; }

		// The last drawn frame's tiles and their statistics, e.g. for the Painter Test window
		const PainterTiles& GetTiles() const { return Tiles; }

		void Release();

	private:
		friend class VulkanPainter;

		vk::Extent2D Extent{ 0, 0 };
		vk::raii::Image Image{ nullptr };
		vk::raii::DeviceMemory ImageMemory{ nullptr };
		vk::raii::ImageView ImageView{ nullptr };
		vk::raii::Framebuffer Framebuffer{ nullptr };

		VulkanPainterBuffers Buffers;
		PainterTiles Tiles;
	};

	// The segments of one frame (PainterSegment), from any number of painters, drawn into render
	// passes that already hold other drawing, e.g. ImGui's. Everything they need on the GPU goes
	// into one set of buffers: each painter's commands and groups, and each segment's tiles and
	// entries, one after the other. Drawing a segment only tells the shader where its part starts.
	//
	//     segments.Clear();                                                // each frame
	//     const uint32_t index = segments.Add(painter, segment, budget);   // for each segment
	//     vulkanPainter.Upload(segments);                                  // once, before any draw
	//     vulkanPainter.DrawSegment(commandBuffer, segments, index, format, extent); // in a render pass
	//
	// Add() copies what it needs, so the painters may change or go afterwards. The textures are
	// kept as ImGui texture references and resolved when the segment is drawn. Release it (or let
	// it go) before the Vulkan device is destroyed.
	class VulkanPainterSegments
	{
	public:
		// Starts a new frame: drops all painters and segments, keeps the buffers
		void Clear();

		// Bins the segment and adds it, with its painter's commands, groups and textures if this
		// frame doesn't have them yet. Returns the segment's index, for DrawSegment().
		uint32_t Add(const Painter& painter, const PainterSegment& segment, const PainterBudget& budget);

		uint32_t GetSegmentCount() const { return static_cast<uint32_t>(Segments.size()); }

		// One segment's binning statistics, e.g. for the Painter Test window
		const PainterBinningStats& GetStats(uint32_t segment) const { return Segments[segment].Stats; }

		// What went wrong while binning this frame's segments, one line per problem
		const std::vector<std::string>& GetProblems() const { return Problems; }

		// Adds the segment's tiles, each with its commands, to `grid`, a heat grid over the whole
		// target the segment is drawn into
		void AddHeat(uint32_t segment, PainterHeatGrid& grid) const;

		// What the per-tile reduction would keep of this frame's entries: per tile, those from the
		// last command that covers the whole tile opaquely, a solid rect inside its rounding and
		// clip rect. `alsoOpaque`: packed colours (PackPremultipliedColor()) to count as opaque too.
		PainterOcclusionEstimate EstimateOcclusion(std::span<const uint32_t> alsoOpaque) const;

		// What Upload() copies to the GPU this frame: commands, groups, tiles and entries
		size_t GetUploadBytes() const
		{
			return GetCommandUploadBytes() + Tiles.size() * sizeof(PainterTile) + Entries.size() * sizeof(uint32_t);
		}
		// The commands' and groups' part of it, which binning on the GPU would still upload
		size_t GetCommandUploadBytes() const { return Commands.size() * sizeof(PainterCommand) + Groups.size() * sizeof(PainterGroup); }

		void Release();

	private:
		friend class VulkanPainter;

		// One painter's part of the buffers
		struct PainterPart
		{
			const Painter* Source = nullptr;
			uint32_t CommandBase = 0;
			uint32_t GroupBase = 0;
			std::vector<ImTextureRef> Textures;
		};

		// One segment's part of the buffers, and its tile grid on the target
		struct SegmentPart
		{
			uint32_t Painter = 0;
			uint32_t CommandBase = 0;
			uint32_t TileBase = 0;
			uint32_t EntryBase = 0;
			uint32_t OriginX = 0;
			uint32_t OriginY = 0;
			uint32_t TilesX = 0;
			uint32_t TilesY = 0;
			PainterBinningStats Stats;
		};

		// The painter's part, added if this frame doesn't have it yet
		uint32_t FindOrAddPainter(const Painter& painter);

		std::vector<PainterCommand> Commands;
		std::vector<PainterGroup> Groups;
		std::vector<PainterTile> Tiles;
		std::vector<uint32_t> Entries;

		std::vector<PainterPart> Painters;
		std::vector<SegmentPart> Segments;
		std::vector<std::string> Problems;

		// Reused for binning each segment
		PainterTiles Binned;

		VulkanPainterBuffers Buffers;
	};

	// Draws a Painter's frame with the painter shader (Painter.vert and Painter.frag): bins the
	// commands into tiles, uploads them, and draws one quad over the tiles.
	//
	//     VulkanPainter vulkanPainter;
	//     vulkanPainter.Initialize(context);
	//     ...
	//     vulkanPainter.Draw(commandBuffer, painter, target, options);
	//
	// It draws either a whole frame into an image of its own (Draw(), a canvas), or segments
	// into someone else's render pass (DrawSegment(), e.g. ImGui's).
	//
	// The renderer owns one. It waits for the previous frame's GPU work before recording the next,
	// so every target and segment set has one set of upload buffers, rewritten each frame. A
	// target is drawn at most once per frame.
	class VulkanPainter
	{
	public:
		// Creates the canvases' render pass and pipeline and the descriptor pool, and prepares the
		// empty texture (one short submission). The compiled shaders are loaded from the Shaders folder next to
		// the executable.
		void Initialize(Nyx::VulkanContext& context);
		void Shutdown();

		bool IsInitialized() const { return Context != nullptr; }

		// Records painting `painter`'s frame into `target`, which takes the painter's target
		// size. Outside a render pass; the target's image ends in ShaderReadOnlyOptimal layout.
		void Draw(vk::raii::CommandBuffer& commandBuffer, const Painter& painter, VulkanPainterTarget& target, const PainterDrawOptions& options);

		// Copies the frame's segments into their buffers. Once per frame, after the last Add()
		// and before the first DrawSegment().
		void Upload(VulkanPainterSegments& segments);

		// Records drawing one segment, blended over what the render pass holds, inside a render
		// pass that is going on. It must be made like an ImGui window's: one colour attachment of
		// `colorFormat`, one sample, no depth, one subpass, and the one subpass dependency ImGui's
		// Vulkan backend and VulkanSwapchain give theirs (see GetSegmentPipeline()).
		// `framebufferExtent` is the render pass' size in pixels, which the segment's painter
		// recorded in. The first segment for a new colour format makes a pipeline for it.
		void DrawSegment(vk::CommandBuffer commandBuffer, const VulkanPainterSegments& segments, uint32_t segment, vk::Format colorFormat, vk::Extent2D framebufferExtent);

		// The descriptor set layout of one texture: a combined image sampler at binding 0, for the
		// fragment shader. It is defined exactly like ImGui's (imgui_impl_vulkan.cpp), so that the
		// sets ImGui makes for its textures bind as the painter's textures.
		const vk::raii::DescriptorSetLayout& GetTextureSetLayout() const { return TextureSetLayout; }

	private:
		// The segments' pipeline for window render passes of one colour format
		struct SegmentPipeline
		{
			vk::Format Format = vk::Format::eUndefined;
			// Only made to create the pipeline with; compatible with the render passes it runs in
			vk::raii::RenderPass RenderPass{ nullptr };
			vk::raii::Pipeline Pipeline{ nullptr };
		};

		void CheckDeviceLimits();
		void CreateRenderPass();
		void CreateDescriptorSetLayouts();
		void CreateDescriptorPool();
		void CreatePipelineLayout();
		void CreateEmptyTexture();

		// The painter's pipeline, for render passes compatible with `renderPass`
		vk::raii::Pipeline CreatePipeline(vk::RenderPass renderPass);

		// The segments' pipeline for window render passes of this colour format, made on first
		// use. A pipeline runs in any render pass compatible with the one it was made with, and
		// Vulkan counts the subpass dependencies in that, not only the formats. ImGui's OS windows
		// and the main window (VulkanSwapchain) have the same single dependency, so one pipeline
		// per format serves them all.
		const vk::raii::Pipeline& GetSegmentPipeline(vk::Format colorFormat);

		// Binds the textures to sets 1 to PainterMaxTextures; unused slots, and textures ImGui
		// hasn't uploaded yet, get the empty texture.
		void BindTextures(vk::CommandBuffer commandBuffer, const std::vector<ImTextureRef>& textures);

		// Gives the target an image and framebuffer of this size, if it has none of that size yet
		void PrepareImage(VulkanPainterTarget& target, vk::Extent2D extent);

		// Copies the data into the buffers, growing them and updating their descriptor set when
		// needed
		void Upload(VulkanPainterBuffers& buffers, std::span<const PainterCommand> commands, std::span<const PainterGroup> groups,
			std::span<const PainterTile> tiles, std::span<const uint32_t> entries);

		Nyx::VulkanContext* Context = nullptr;

		// The canvases' render pass: clears the image, and leaves it ready to be shown
		vk::raii::RenderPass RenderPass{ nullptr };
		// Set 0: the frame's commands, groups, tiles and entries
		vk::raii::DescriptorSetLayout DescriptorSetLayout{ nullptr };
		// Sets 1 to PainterMaxTextures: one texture each
		vk::raii::DescriptorSetLayout TextureSetLayout{ nullptr };
		vk::raii::DescriptorPool DescriptorPool{ nullptr };
		vk::raii::PipelineLayout PipelineLayout{ nullptr };
		vk::raii::Pipeline CanvasPipeline{ nullptr };
		std::vector<SegmentPipeline> SegmentPipelines;

		// A transparent 1x1 texture for the texture slots a frame doesn't use: the shader reads
		// every slot, so every slot needs a texture.
		vk::raii::Image EmptyImage{ nullptr };
		vk::raii::DeviceMemory EmptyImageMemory{ nullptr };
		vk::raii::ImageView EmptyImageView{ nullptr };
		vk::raii::Sampler EmptySampler{ nullptr };
		vk::raii::DescriptorSet EmptyTextureSet{ nullptr };
	};
}
