#pragma once

#include "PainterTiles.h"

#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <cstdint>

namespace Nyx
{
	class VulkanContext;
}

namespace Nyx::Engine
{
	class Painter;
	class VulkanPainter;

	// What the painter shader shows besides the painted shapes
	enum class EPainterDebugView : uint32_t
	{
		None = 0,
		// Tints each tile by how many commands it holds: blue for few, through green, to red for
		// 16 or more. The values match DebugView in Painter.frag.
		Heatmap = 1,
	};

	struct PainterDrawOptions
	{
		// The target is cleared to this before painting; straight (not premultiplied) RGBA
		glm::vec4 ClearColor{ 0.0f };
		EPainterDebugView DebugView = EPainterDebugView::None;
		PainterBudget Budget;
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

		// A storage buffer the CPU writes every frame; it only ever grows.
		struct UploadBuffer
		{
			vk::raii::Buffer Buffer{ nullptr };
			vk::raii::DeviceMemory Memory{ nullptr };
			void* Mapped = nullptr;
			vk::DeviceSize Size = 0;
		};

		vk::Extent2D Extent{ 0, 0 };
		vk::raii::Image Image{ nullptr };
		vk::raii::DeviceMemory ImageMemory{ nullptr };
		vk::raii::ImageView ImageView{ nullptr };
		vk::raii::Framebuffer Framebuffer{ nullptr };

		UploadBuffer Commands;
		UploadBuffer Groups;
		UploadBuffer TileBuffer;
		UploadBuffer Entries;
		vk::raii::DescriptorSet DescriptorSet{ nullptr };

		PainterTiles Tiles;
	};

	// Draws a Painter's frame with the painter shader (Painter.vert and Painter.frag): bins the
	// commands into tiles, uploads them, and draws one triangle over the target.
	//
	//     VulkanPainter vulkanPainter;
	//     vulkanPainter.Initialize(context);
	//     ...
	//     vulkanPainter.Draw(commandBuffer, painter, target, options);
	//
	// The renderer owns one. It waits for the previous frame's GPU work before recording the next,
	// so every target has one set of upload buffers, rewritten each frame. A target is drawn at
	// most once per frame.
	class VulkanPainter
	{
	public:
		// Creates the render pass, pipeline and descriptor pool. The compiled shaders are loaded
		// from the Shaders folder next to the executable.
		void Initialize(Nyx::VulkanContext& context);
		void Shutdown();

		bool IsInitialized() const { return Context != nullptr; }

		// Records painting `painter`'s frame into `target`, which takes the painter's target
		// size. Outside a render pass; the target's image ends in ShaderReadOnlyOptimal layout.
		void Draw(vk::raii::CommandBuffer& commandBuffer, const Painter& painter, VulkanPainterTarget& target, const PainterDrawOptions& options);

	private:
		void CreateRenderPass();
		void CreateDescriptorSetLayout();
		void CreateDescriptorPool();
		void CreatePipeline();

		// Gives the target an image and framebuffer of this size, if it has none of that size yet
		void PrepareImage(VulkanPainterTarget& target, vk::Extent2D extent);

		// Copies the frame's data into the target's buffers, growing them and updating its
		// descriptor set when needed
		void Upload(VulkanPainterTarget& target, const Painter& painter);

		Nyx::VulkanContext* Context = nullptr;

		vk::raii::RenderPass RenderPass{ nullptr };
		vk::raii::DescriptorSetLayout DescriptorSetLayout{ nullptr };
		vk::raii::DescriptorPool DescriptorPool{ nullptr };
		vk::raii::PipelineLayout PipelineLayout{ nullptr };
		vk::raii::Pipeline Pipeline{ nullptr };
	};
}
