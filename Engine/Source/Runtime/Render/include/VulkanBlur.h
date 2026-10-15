#pragma once

#include "ShaderPrograms.h"

#include <vulkan/vulkan_raii.hpp>

#include <cstdint>
#include <vector>

namespace Nyx
{
	class VulkanContext;
}

namespace Nyx
{
	class VulkanBlur;

	// How much an image is blurred
	struct BlurOptions
	{
		// How often the image is halved: 4 makes it 1/16 of its size
		uint32_t Downsamples = 4;
		// Gaussian passes at the smallest size, each along x and then along y
		uint32_t GaussianPasses = 1;

		bool operator==(const BlurOptions&) const = default;
	};

	// The small blurred copy of one image (VulkanBlur::Blur()), and the images on the way to it.
	// Empty until first blurred into, which sizes it after the source. Release it (or let it
	// go) before the Vulkan device is destroyed.
	class VulkanBlurTarget
	{
	public:
		// The blurred image, in ShaderReadOnlyOptimal layout once blurred; null before. It changes
		// when the source's size or the options change, so read it after each Blur().
		vk::Image GetImage() const;
		vk::ImageView GetImageView() const;
		vk::Extent2D GetExtent() const;

		// Changes each time the blurred image is made anew. Tell a new image by this, not by its
		// view: Vulkan may give the new view the handle of the old one, destroyed just before.
		uint64_t GetGeneration() const { return Generation; }

		void Release();

	private:
		friend class VulkanBlur;

		// One image of the chain, with what is needed to draw into it and to sample it
		struct Level
		{
			vk::Extent2D Extent{ 0, 0 };
			vk::raii::Image Image{ nullptr };
			vk::raii::DeviceMemory Memory{ nullptr };
			vk::raii::ImageView View{ nullptr };
			vk::raii::Framebuffer Framebuffer{ nullptr };
			// Samples this level, for the pass after it
			vk::raii::DescriptorSet SampleSet{ nullptr };
		};

		// The halvings, the last of which ends up holding the result, then one image of the
		// smallest size that the Gaussian goes through on its way along x
		std::vector<Level> Levels;
		Level GaussianTemporary;

		// Samples the source, for the first halving
		vk::raii::DescriptorSet SourceSet{ nullptr };
		vk::Extent2D SourceExtent{ 0, 0 };
		BlurOptions Options;

		// Counts the images made, across Release() (GetGeneration())
		uint64_t Generation = 0;
	};

	// Blurs images on the GPU (Blur.vert and Blur.frag): the image is halved a few times, each
	// time averaging around every pixel, and the small result blurred with a Gaussian along x
	// and y. A small image makes a wide blur cheap: a few passes over a few thousand pixels.
	//
	//     VulkanBlur blur;
	//     blur.Initialize(context, programs);
	//     ...
	//     blur.Blur(commandBuffer, sceneView.GetImageView(), sceneView.GetExtent(), target, BlurOptions{});
	//
	// Any image works as the source; it must be in ShaderReadOnlyOptimal layout, 8 bits per
	// channel or more. The result is RGBA, 8 bits per channel. The renderer owns one, and waits
	// for the previous frame's GPU work before recording the next, so a target is rewritten in
	// place each frame.
	class VulkanBlur
	{
	public:
		// Creates the render pass, pipeline, sampler and descriptor pool. The shaders, Blur.vert and
		// Blur.frag, come from `programs`, which must outlive the blur.
		void Initialize(Nyx::VulkanContext& context, ShaderPrograms& programs);

		// Makes the pipeline again from the programs' current code, after they changed. The GPU
		// must be done with the old one.
		void RebuildPipeline();
		void Shutdown();

		// Records blurring `source` into `target`, outside a render pass. The target's image ends
		// in ShaderReadOnlyOptimal layout.
		void Blur(vk::raii::CommandBuffer& commandBuffer, vk::ImageView source, vk::Extent2D sourceExtent, VulkanBlurTarget& target, const BlurOptions& options);

		// The linear, clamp-to-edge sampler the blur reads with; also right for showing the result
		const vk::raii::Sampler& GetSampler() const { return Sampler; }

	private:
		void CreateRenderPass();
		void CreateDescriptors();
		void CreatePipeline();

		// Makes the target's images for this source size and these options, if it hasn't them yet,
		// and points its first pass at the source
		void PrepareTarget(VulkanBlurTarget& target, vk::ImageView source, vk::Extent2D sourceExtent, const BlurOptions& options);
		void CreateLevel(VulkanBlurTarget::Level& level, vk::Extent2D extent);
		vk::raii::DescriptorSet MakeSampleSet(vk::ImageView view);
		void PointSampleSet(const vk::raii::DescriptorSet& set, vk::ImageView view);

		// One pass: draws `input` into `output` with the shader in this mode
		void DrawPass(vk::raii::CommandBuffer& commandBuffer, const vk::raii::DescriptorSet& input, vk::Extent2D inputExtent, const VulkanBlurTarget::Level& output, uint32_t mode, float directionX, float directionY);

		Nyx::VulkanContext* Context = nullptr;
		ShaderPrograms* Programs = nullptr;

		vk::raii::RenderPass RenderPass{ nullptr };
		vk::raii::DescriptorSetLayout SetLayout{ nullptr };
		vk::raii::DescriptorPool DescriptorPool{ nullptr };
		vk::raii::PipelineLayout PipelineLayout{ nullptr };
		vk::raii::Pipeline Pipeline{ nullptr };
		vk::raii::Sampler Sampler{ nullptr };
	};
}
