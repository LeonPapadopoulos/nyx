#pragma once

#include "PainterDrawOptions.h"
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

		// The descriptor set layout of one texture: a combined image sampler at binding 0, for the
		// fragment shader. It is defined exactly like ImGui's (imgui_impl_vulkan.cpp), so that the
		// sets ImGui makes for its textures bind as the painter's textures.
		const vk::raii::DescriptorSetLayout& GetTextureSetLayout() const { return TextureSetLayout; }

	private:
		void CheckDeviceLimits();
		void CreateRenderPass();
		void CreateDescriptorSetLayouts();
		void CreateDescriptorPool();
		void CreatePipeline();
		void CreateEmptyTexture();

		// Clears the empty texture to transparent and makes it readable, once
		void PrepareEmptyTexture(vk::raii::CommandBuffer& commandBuffer);

		// Binds the frame's textures to sets 1 to PainterMaxTextures; unused slots, and textures
		// ImGui hasn't uploaded yet, get the empty texture.
		void BindTextures(vk::raii::CommandBuffer& commandBuffer, const Painter& painter);

		// Gives the target an image and framebuffer of this size, if it has none of that size yet
		void PrepareImage(VulkanPainterTarget& target, vk::Extent2D extent);

		// Copies the frame's data into the target's buffers, growing them and updating its
		// descriptor set when needed
		void Upload(VulkanPainterTarget& target, const Painter& painter);

		Nyx::VulkanContext* Context = nullptr;

		vk::raii::RenderPass RenderPass{ nullptr };
		// Set 0: the frame's commands, groups, tiles and entries
		vk::raii::DescriptorSetLayout DescriptorSetLayout{ nullptr };
		// Sets 1 to PainterMaxTextures: one texture each
		vk::raii::DescriptorSetLayout TextureSetLayout{ nullptr };
		vk::raii::DescriptorPool DescriptorPool{ nullptr };
		vk::raii::PipelineLayout PipelineLayout{ nullptr };
		vk::raii::Pipeline Pipeline{ nullptr };

		// A transparent 1x1 texture for the texture slots a frame doesn't use: the shader reads
		// every slot, so every slot needs a texture.
		vk::raii::Image EmptyImage{ nullptr };
		vk::raii::DeviceMemory EmptyImageMemory{ nullptr };
		vk::raii::ImageView EmptyImageView{ nullptr };
		vk::raii::Sampler EmptySampler{ nullptr };
		vk::raii::DescriptorSet EmptyTextureSet{ nullptr };
		bool bEmptyTextureReady = false;
	};
}
