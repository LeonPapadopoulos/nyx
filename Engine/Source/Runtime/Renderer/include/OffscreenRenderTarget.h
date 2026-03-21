#pragma once
#include <vulkan/vulkan_raii.hpp>

namespace Nyx
{
	class VulkanContext;

	class OffscreenRenderTarget
	{
	public:
		void Initialize(VulkanContext& context, uint32_t width, uint32_t height, vk::Format format);
		void Shutdown();

		void Recreate(VulkanContext& context, uint32_t width, uint32_t height, vk::Format format);

		vk::Image GetImage() const { return *Image; }
		vk::ImageView GetImageView() const { return *ImageView; }
		vk::Format GetFormat() const { return Format; }
		vk::Extent2D GetExtent() const { return Extent; }

	private:
		vk::Extent2D Extent{};
		vk::Format Format = vk::Format::eUndefined;

		vk::raii::Image Image{ nullptr };
		vk::raii::DeviceMemory Memory{ nullptr };
		vk::raii::ImageView ImageView{ nullptr };
	};
}