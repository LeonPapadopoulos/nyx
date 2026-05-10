#pragma once
#include <vulkan/vulkan_raii.hpp>
#include <imgui.h>

namespace Nyx
{
	class VulkanContext;

	class VulkanViewportTarget
	{
	public:
		void Initialize(VulkanContext& context, uint32_t width, uint32_t height, vk::Format format);
		void Shutdown(VulkanContext& context);

		void EnsureSize(VulkanContext& context, uint32_t width, uint32_t height);
		void Recreate(VulkanContext& context, uint32_t width, uint32_t height, vk::Format format);

		void BeginRenderPass(vk::raii::CommandBuffer& commandBuffer, const vk::ClearColorValue& clearValue, float clearDepth = 1.0f);
		void EndRenderPass(vk::raii::CommandBuffer& commandBuffer);

		ImTextureID GetImGuiTextureId() const { return ImGuiTextureId; }
		vk::Extent2D GetExtent() const { return Extent; }
		vk::Format GetFormat() const { return Format; }
		vk::raii::RenderPass& GetRenderPass() { return RenderPass; }
		const vk::raii::RenderPass& GetRenderPass() const { return RenderPass; };

	private:
		void CreateImage(VulkanContext& context);
		void CreateImageView(VulkanContext& context);
		void CreateSampler(VulkanContext& context);
		void CreateRenderPass(VulkanContext& context);
		void CreateFramebuffer(VulkanContext& context);
		void RegisterImGuiTexture();
		void DestroyImGuiTexture();

		void CreateDepthResources(VulkanContext& context);
		void CreateDepthImage(VulkanContext& context);
		void CreateDepthImageView(VulkanContext& context);

		vk::Format FindSupportedFormat(
			vk::PhysicalDevice physicalDevice,
			const std::vector<vk::Format>& candidates,
			vk::ImageTiling tiling,
			vk::FormatFeatureFlags features);

		vk::Format FindDepthFormat(vk::PhysicalDevice physicalDevice);

		uint32_t FindMemoryType(
			vk::PhysicalDevice physicalDevice,
			uint32_t typeFilter,
			vk::MemoryPropertyFlags properties);

	private:
		vk::Extent2D Extent{ 0, 0 };
		vk::Format Format = vk::Format::eUndefined;

		vk::raii::Image Image{ nullptr };
		vk::raii::DeviceMemory Memory{ nullptr };
		vk::raii::ImageView ImageView{ nullptr };
		vk::raii::Sampler Sampler{ nullptr };

		vk::Format DepthFormat = vk::Format::eUndefined;

		vk::raii::Image DepthImage{ nullptr };
		vk::raii::DeviceMemory DepthMemory{ nullptr };
		vk::raii::ImageView DepthImageView{ nullptr };

		vk::raii::RenderPass RenderPass{ nullptr };
		vk::raii::Framebuffer Framebuffer{ nullptr };

		ImTextureID ImGuiTextureId{};
	};
}