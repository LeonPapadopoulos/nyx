#pragma once

#include "VulkanBlur.h"

#include <vulkan/vulkan_raii.hpp>

#include <functional>

namespace Nyx
{
	class VulkanContext;
}

namespace Nyx
{
	// What glass shows: a picture of what lies behind the main window's floating windows, drawn
	// into an image of its own and blurred. The renderer draws ImGui's windows behind the first
	// glass window into it (the docked layer), before it draws the main window itself.
	class VulkanGlassBackdrop
	{
	public:
		// Records drawing the backdrop and blurring it, outside any render pass. `draw` records
		// into a render pass on an image of `extent` in `format`, cleared to black first. The
		// render pass is made like the swapchain's, so the pipelines made for the main window
		// (ImGui's, the painter's segments) draw into it too. Afterwards the blurred picture is
		// ready to sample.
		void Record(vk::raii::CommandBuffer& commandBuffer, VulkanContext& context, VulkanBlur& blur, vk::Format format, vk::Extent2D extent,
			const std::function<void(vk::CommandBuffer)>& draw);

		// The blurred picture, in ShaderReadOnlyOptimal layout after Record(); null before. It
		// changes when the size does, so read it after each Record().
		vk::ImageView GetBlurredView() const { return Blurred.GetImageView(); }
		// Changes when the blurred picture is made anew (VulkanBlurTarget::GetGeneration())
		uint64_t GetBlurredGeneration() const { return Blurred.GetGeneration(); }

		// Gives the images back. Wait for the GPU to be done with them first.
		void Release();

	private:
		void Create(VulkanContext& context, vk::Format format, vk::Extent2D extent);

		vk::Format Format = vk::Format::eUndefined;
		vk::Extent2D Extent{ 0, 0 };
		vk::raii::RenderPass RenderPass{ nullptr };
		vk::raii::Image Image{ nullptr };
		vk::raii::DeviceMemory Memory{ nullptr };
		vk::raii::ImageView View{ nullptr };
		vk::raii::Framebuffer Framebuffer{ nullptr };
		VulkanBlurTarget Blurred;
	};
}
