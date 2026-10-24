#pragma once
#include <vulkan/vulkan_raii.hpp>
#include <imgui.h>

#include <vector>

namespace Nyx
{
	class VulkanContext;

	// A scene view's targets: the scene is drawn in HDR (R16G16B16A16Sfloat) with a depth buffer,
	// then tonemapped into the 8-bit picture the editor shows, and the editor's overlays are drawn
	// over the picture, tested against the scene's depth. While the view has ambient occlusion, the
	// scene pass writes the sky's light apart into a second target (the ambient light), for the
	// occlusion, which reads the depth, to darken it alone.
	//
	// With more than one sample a pixel (multisample anti-aliasing, Docs/Design/anti-aliasing.md),
	// the scene pass draws into multisampled HDR, ambient light and depth targets of its own, which
	// it resolves at its end into the single-sampled ones: the colours to the mean of their samples,
	// the depth to its nearest sample (or its first, where the device can't). Everything after the
	// scene pass reads those as it does with one sample.
	class VulkanViewportTarget
	{
	public:
		static constexpr vk::Format HdrFormat = vk::Format::eR16G16B16A16Sfloat;
		// The sky's light apart, as precise as the scene's: where nothing is occluded, the picture is
		// the one without occlusion within a step (B10G11R11's 5 bits of blue were 2 steps off)
		static constexpr vk::Format AmbientFormat = HdrFormat;

		// `samples`: a pixel's in the scene pass, one the device can do for both colour and depth
		void Initialize(VulkanContext& context, uint32_t width, uint32_t height, vk::Format format, vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1);
		void Shutdown(VulkanContext& context);

		void EnsureSize(VulkanContext& context, uint32_t width, uint32_t height);
		void Recreate(VulkanContext& context, uint32_t width, uint32_t height, vk::Format format, vk::SampleCountFlagBits samples);

		// How the depth's samples are resolved, of the modes the device has (`supported`): the
		// nearest (the greatest, as depth is reversed), else the first sample, which every device has
		static vk::ResolveModeFlagBits ChooseDepthResolveMode(vk::ResolveModeFlags supported);

		// The scene: the HDR image, the ambient light and the depth, all cleared first, the ambient
		// light to black, the depth to the far plane's; multisampled, resolved at the pass's end
		void BeginScenePass(vk::raii::CommandBuffer& commandBuffer, const vk::ClearColorValue& clearValue);
		// The picture: written whole (the tonemap), then drawn over with the scene's depth
		void BeginPicturePass(vk::raii::CommandBuffer& commandBuffer);
		void EndRenderPass(vk::raii::CommandBuffer& commandBuffer);

		ImTextureID GetImGuiTextureId() const { return ImGuiTextureId; }
		vk::Extent2D GetExtent() const { return Extent; }
		vk::Format GetFormat() const { return Format; }
		// A pixel's samples in the scene pass, for the pipelines that draw into it
		vk::SampleCountFlagBits GetSamples() const { return Samples; }
		// The memory of all of its images, in bytes
		vk::DeviceSize GetMemorySize() const { return MemorySize; }
		// The rendered picture, in ShaderReadOnlyOptimal layout after the render pass, e.g. to blur
		vk::ImageView GetImageView() const { return *ImageView; }
		// The same picture as an image, e.g. to copy it out for a test
		vk::Image GetImage() const { return *Image; }
		// The scene in HDR, in ShaderReadOnlyOptimal layout after the scene pass (resolved)
		vk::ImageView GetHdrImageView() const { return *HdrImageView; }
		// The sky's light the scene pass wrote apart, in ShaderReadOnlyOptimal layout after it
		// (resolved)
		vk::ImageView GetAmbientImageView() const { return *AmbientImageView; }
		// The scene's depth, in DepthStencilAttachmentOptimal layout after the scene pass (resolved);
		// its depth aspect sampled in ShaderReadOnlyOptimal
		vk::Image GetDepthImage() const { return *DepthImage; }
		vk::ImageView GetDepthImageView() const { return *DepthImageView; }
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
		// The scene pass with more than one sample: the multisampled targets, resolved at its end
		void CreateMultisampledScenePass(VulkanContext& context);
		void CreateFramebuffer(VulkanContext& context);
		// An image the scene pass draws into with Samples samples, and its view
		void CreateMultisampledImage(VulkanContext& context, vk::Format format, vk::ImageUsageFlags usage, vk::ImageAspectFlags aspect, vk::raii::Image& outImage,
			vk::raii::DeviceMemory& outMemory, vk::raii::ImageView& outView);
		// Memory for the image, bound, counted in MemorySize
		vk::raii::DeviceMemory AllocateImageMemory(VulkanContext& context, const vk::raii::Image& image);
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

		vk::raii::Image AmbientImage{ nullptr };
		vk::raii::DeviceMemory AmbientMemory{ nullptr };
		vk::raii::ImageView AmbientImageView{ nullptr };

		// With more than one sample, what the scene pass draws into before it resolves
		vk::SampleCountFlagBits Samples = vk::SampleCountFlagBits::e1;
		vk::ResolveModeFlagBits DepthResolveMode = vk::ResolveModeFlagBits::eSampleZero;
		vk::raii::Image MultisampledHdrImage{ nullptr };
		vk::raii::DeviceMemory MultisampledHdrMemory{ nullptr };
		vk::raii::ImageView MultisampledHdrImageView{ nullptr };
		vk::raii::Image MultisampledAmbientImage{ nullptr };
		vk::raii::DeviceMemory MultisampledAmbientMemory{ nullptr };
		vk::raii::ImageView MultisampledAmbientImageView{ nullptr };
		vk::raii::Image MultisampledDepthImage{ nullptr };
		vk::raii::DeviceMemory MultisampledDepthMemory{ nullptr };
		vk::raii::ImageView MultisampledDepthImageView{ nullptr };

		vk::DeviceSize MemorySize = 0;

		vk::raii::RenderPass SceneRenderPass{ nullptr };
		vk::raii::Framebuffer SceneFramebuffer{ nullptr };
		// The picture's
		vk::raii::RenderPass RenderPass{ nullptr };
		vk::raii::Framebuffer Framebuffer{ nullptr };

		ImTextureID ImGuiTextureId{};
	};
}