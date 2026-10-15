#pragma once
#include <vulkan/vulkan_raii.hpp>
#include <imgui.h>

#include <vector>

namespace Nyx
{
	class VulkanContext;

	// A scene view's targets: the scene is drawn in HDR (R16G16B16A16Sfloat) with a depth buffer,
	// then tonemapped into the 8-bit picture the editor shows, and the editor's overlays are drawn
	// over the picture, tested against the scene's depth.
	class VulkanViewportTarget
	{
	public:
		static constexpr vk::Format HdrFormat = vk::Format::eR16G16B16A16Sfloat;

		void Initialize(VulkanContext& context, uint32_t width, uint32_t height, vk::Format format);
		void Shutdown(VulkanContext& context);

		void EnsureSize(VulkanContext& context, uint32_t width, uint32_t height);
		void Recreate(VulkanContext& context, uint32_t width, uint32_t height, vk::Format format);

		// The scene: the HDR image and the depth, both cleared first
		void BeginScenePass(vk::raii::CommandBuffer& commandBuffer, const vk::ClearColorValue& clearValue, float clearDepth = 1.0f);
		// The picture: written whole (the tonemap), then drawn over with the scene's depth
		void BeginPicturePass(vk::raii::CommandBuffer& commandBuffer);
		void EndRenderPass(vk::raii::CommandBuffer& commandBuffer);

		ImTextureID GetImGuiTextureId() const { return ImGuiTextureId; }
		vk::Extent2D GetExtent() const { return Extent; }
		vk::Format GetFormat() const { return Format; }
		// The rendered picture, in ShaderReadOnlyOptimal layout after the render pass, e.g. to blur
		vk::ImageView GetImageView() const { return *ImageView; }
		// The same picture as an image, e.g. to copy it out for a test
		vk::Image GetImage() const { return *Image; }
		// The scene in HDR, in ShaderReadOnlyOptimal layout after the scene pass
		vk::ImageView GetHdrImageView() const { return *HdrImageView; }
		// The picture pass's, for what draws over the picture
		vk::raii::RenderPass& GetRenderPass() { return RenderPass; }
		const vk::raii::RenderPass& GetRenderPass() const { return RenderPass; };
		// The scene pass's, for what draws the scene
		const vk::raii::RenderPass& GetSceneRenderPass() const { return SceneRenderPass; }

	private:
		void CreateImage(VulkanContext& context);
		void CreateImageView(VulkanContext& context);
		void CreateHdrImage(VulkanContext& context);
		void BeginPass(vk::raii::CommandBuffer& commandBuffer, vk::RenderPass renderPass, vk::Framebuffer framebuffer, const std::vector<vk::ClearValue>& clearValues);
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

		vk::raii::Image HdrImage{ nullptr };
		vk::raii::DeviceMemory HdrMemory{ nullptr };
		vk::raii::ImageView HdrImageView{ nullptr };

		vk::raii::RenderPass SceneRenderPass{ nullptr };
		vk::raii::Framebuffer SceneFramebuffer{ nullptr };
		// The picture's
		vk::raii::RenderPass RenderPass{ nullptr };
		vk::raii::Framebuffer Framebuffer{ nullptr };

		ImTextureID ImGuiTextureId{};
	};
}