// The painter on a real GPU: paints small frames with VulkanPainter, reads the pixels back and
// checks them. Needs a Vulkan device; the window it creates for that stays hidden.
#include "Painter.h"
#include "PainterTestSheet.h"
#include "VulkanBlur.h"
#include "VulkanContext.h"
#include "VulkanPainter.h"

#include <GLFW/glfw3.h>

#include <imgui.h>

// The PNG writer GLFW ships for its own tests, only to save images for looking at
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
	using namespace Nyx::Engine;

	const glm::vec4 Transparent{ 0.0f };
	const glm::vec4 White{ 1.0f };
	const glm::vec4 Black{ 0.0f, 0.0f, 0.0f, 1.0f };
	const glm::vec4 Red{ 1.0f, 0.0f, 0.0f, 1.0f };
	const glm::vec4 Orange{ 1.0f, 0.5f, 0.0f, 1.0f };

	// About 5 of 255 steps
	constexpr float Tolerance = 0.02f;

	void Require(bool condition, const std::string& message)
	{
		if (!condition)
		{
			throw std::runtime_error(message);
		}
	}

	std::string Describe(const glm::vec4& color)
	{
		return "(" + std::to_string(color.r) + ", " + std::to_string(color.g) + ", " + std::to_string(color.b) + ", " + std::to_string(color.a) + ")";
	}

	uint32_t FindMemoryType(vk::PhysicalDevice physicalDevice, uint32_t typeFilter, vk::MemoryPropertyFlags properties)
	{
		const vk::PhysicalDeviceMemoryProperties memoryProperties = physicalDevice.getMemoryProperties();
		for (uint32_t typeIndex = 0; typeIndex < memoryProperties.memoryTypeCount; ++typeIndex)
		{
			if ((typeFilter & (1u << typeIndex)) && (memoryProperties.memoryTypes[typeIndex].propertyFlags & properties) == properties)
			{
				return typeIndex;
			}
		}
		throw std::runtime_error("No suitable memory type");
	}

	// The painted pixels of one frame, premultiplied, each channel from 0 to 1
	struct PaintedImage
	{
		uint32_t Width = 0;
		uint32_t Height = 0;
		std::vector<uint8_t> Bytes; // RGBA8, row by row

		// The pixel whose centre is (x + 0.5, y + 0.5)
		glm::vec4 At(uint32_t x, uint32_t y) const
		{
			const size_t offset = (static_cast<size_t>(y) * Width + x) * 4;
			return glm::vec4(Bytes[offset], Bytes[offset + 1], Bytes[offset + 2], Bytes[offset + 3]) / 255.0f;
		}

		void RequirePixel(uint32_t x, uint32_t y, const glm::vec4& expected, const std::string& what) const
		{
			const glm::vec4 actual = At(x, y);
			const glm::vec4 difference = glm::abs(actual - expected);
			Require(difference.r <= Tolerance && difference.g <= Tolerance && difference.b <= Tolerance && difference.a <= Tolerance,
				what + ": pixel (" + std::to_string(x) + "," + std::to_string(y) + ") is " + Describe(actual) + ", expected " + Describe(expected));
		}

		void RequireAlpha(uint32_t x, uint32_t y, float expected, const std::string& what) const
		{
			const float actual = At(x, y).a;
			Require(std::abs(actual - expected) <= Tolerance,
				what + ": pixel (" + std::to_string(x) + "," + std::to_string(y) + ") has alpha " + std::to_string(actual) + ", expected " + std::to_string(expected));
		}
	};

	// A hidden window, a Vulkan device and a painter, for the whole test program
	class GpuPainter
	{
	public:
		GpuPainter()
		{
			if (!glfwInit())
			{
				throw std::runtime_error("GLFW failed to start");
			}
			glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
			glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
			Window = glfwCreateWindow(64, 64, "NyxPainterGpuTests", nullptr, nullptr);
			if (!Window)
			{
				throw std::runtime_error("No window for the Vulkan device");
			}
			Context.Initialize("NyxPainterGpuTests", Window);
			PainterPass.Initialize(Context);
			BlurPass.Initialize(Context);

			// Texture sets for the tests' own textures, as ImGui's backend makes them for its own
			const vk::DescriptorPoolSize poolSize{ vk::DescriptorType::eCombinedImageSampler, 32 };
			vk::DescriptorPoolCreateInfo poolInfo{};
			poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
			poolInfo.maxSets = 32;
			poolInfo.poolSizeCount = 1;
			poolInfo.pPoolSizes = &poolSize;
			TexturePool = vk::raii::DescriptorPool(Context.GetDevice(), poolInfo);
		}

		~GpuPainter()
		{
			Context.GetDevice().waitIdle();
			Textures.clear();
			TexturePool = nullptr;
			Target.Release();
			BlurPass.Shutdown();
			PainterPass.Shutdown();
			Context.Shutdown();
			glfwDestroyWindow(Window);
			glfwTerminate();
		}

		GpuPainter(const GpuPainter&) = delete;
		GpuPainter& operator=(const GpuPainter&) = delete;

		// Paints the frame, waits for the GPU, and reads the target back
		PaintedImage Paint(const Painter& painter, const PainterDrawOptions& options = PainterDrawOptions{})
		{
			vk::raii::Device& device = Context.GetDevice();

			vk::CommandBufferAllocateInfo allocInfo{};
			allocInfo.commandPool = *Context.GetGraphicsCommandPool();
			allocInfo.level = vk::CommandBufferLevel::ePrimary;
			allocInfo.commandBufferCount = 1;
			vk::raii::CommandBuffer commandBuffer = std::move(vk::raii::CommandBuffers(device, allocInfo).front());
			commandBuffer.begin(vk::CommandBufferBeginInfo{ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

			PainterPass.Draw(commandBuffer, painter, Target, options);

			// The painter leaves the image ready to be sampled; copying needs it as a transfer source.
			vk::ImageMemoryBarrier toTransfer{};
			toTransfer.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
			toTransfer.dstAccessMask = vk::AccessFlagBits::eTransferRead;
			toTransfer.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
			toTransfer.newLayout = vk::ImageLayout::eTransferSrcOptimal;
			toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toTransfer.image = Target.GetImage();
			toTransfer.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
			commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, toTransfer);

			return SubmitAndReadBack(commandBuffer, Target.GetImage(), Target.GetExtent(), false);
		}

		// Renders into a fresh image of `format` that starts out as `background`, the way ImGui
		// renders into a window: `record(commandBuffer)` records inside its render pass. Returns the
		// image as RGBA, whatever the format's channel order.
		template<typename TRecord>
		PaintedImage RenderInto(vk::Format format, vk::Extent2D extent, const glm::vec4& background, TRecord&& record)
		{
			vk::raii::Device& device = Context.GetDevice();

			vk::ImageCreateInfo imageInfo{};
			imageInfo.imageType = vk::ImageType::e2D;
			imageInfo.extent = vk::Extent3D(extent.width, extent.height, 1);
			imageInfo.mipLevels = 1;
			imageInfo.arrayLayers = 1;
			imageInfo.format = format;
			imageInfo.tiling = vk::ImageTiling::eOptimal;
			imageInfo.initialLayout = vk::ImageLayout::eUndefined;
			imageInfo.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc;
			imageInfo.samples = vk::SampleCountFlagBits::e1;
			vk::raii::Image image(device, imageInfo);
			const vk::MemoryRequirements requirements = image.getMemoryRequirements();
			vk::raii::DeviceMemory memory(device, vk::MemoryAllocateInfo(requirements.size, FindMemoryType(*Context.GetPhysicalDevice(), requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)));
			image.bindMemory(*memory, 0);

			vk::ImageViewCreateInfo viewInfo{};
			viewInfo.image = *image;
			viewInfo.viewType = vk::ImageViewType::e2D;
			viewInfo.format = format;
			viewInfo.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
			vk::raii::ImageView view(device, viewInfo);

			// Like an ImGui window's render pass (the swapchain's, or an OS window's): one colour
			// attachment, cleared, no depth, and their one subpass dependency. Segments need
			// exactly that (VulkanPainter::DrawSegment()).
			vk::AttachmentDescription colorAttachment{};
			colorAttachment.format = format;
			colorAttachment.samples = vk::SampleCountFlagBits::e1;
			colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
			colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
			colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
			colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
			colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
			colorAttachment.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
			vk::AttachmentReference colorReference{ 0, vk::ImageLayout::eColorAttachmentOptimal };
			vk::SubpassDescription subpass{};
			subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
			subpass.colorAttachmentCount = 1;
			subpass.pColorAttachments = &colorReference;
			vk::SubpassDependency dependency{};
			dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
			dependency.dstSubpass = 0;
			dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
			dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
			dependency.srcAccessMask = {};
			dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
			vk::RenderPassCreateInfo renderPassInfo{};
			renderPassInfo.attachmentCount = 1;
			renderPassInfo.pAttachments = &colorAttachment;
			renderPassInfo.subpassCount = 1;
			renderPassInfo.pSubpasses = &subpass;
			renderPassInfo.dependencyCount = 1;
			renderPassInfo.pDependencies = &dependency;
			vk::raii::RenderPass renderPass(device, renderPassInfo);

			vk::FramebufferCreateInfo framebufferInfo{};
			framebufferInfo.renderPass = *renderPass;
			framebufferInfo.attachmentCount = 1;
			framebufferInfo.pAttachments = &*view;
			framebufferInfo.width = extent.width;
			framebufferInfo.height = extent.height;
			framebufferInfo.layers = 1;
			vk::raii::Framebuffer framebuffer(device, framebufferInfo);

			vk::CommandBufferAllocateInfo allocInfo{};
			allocInfo.commandPool = *Context.GetGraphicsCommandPool();
			allocInfo.level = vk::CommandBufferLevel::ePrimary;
			allocInfo.commandBufferCount = 1;
			vk::raii::CommandBuffer commandBuffer = std::move(vk::raii::CommandBuffers(device, allocInfo).front());
			commandBuffer.begin(vk::CommandBufferBeginInfo{ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

			// The background is opaque or straight; the image holds premultiplied colour.
			vk::ClearValue clearValue{};
			clearValue.color = vk::ClearColorValue(std::array<float, 4>{ background.r * background.a, background.g * background.a, background.b * background.a, background.a });
			vk::RenderPassBeginInfo beginInfo{};
			beginInfo.renderPass = *renderPass;
			beginInfo.framebuffer = *framebuffer;
			beginInfo.renderArea = vk::Rect2D({ 0, 0 }, extent);
			beginInfo.clearValueCount = 1;
			beginInfo.pClearValues = &clearValue;
			commandBuffer.beginRenderPass(beginInfo, vk::SubpassContents::eInline);
			record(*commandBuffer);
			commandBuffer.endRenderPass();

			// Instead of being presented, the image is copied out.
			vk::ImageMemoryBarrier toTransfer{};
			toTransfer.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
			toTransfer.dstAccessMask = vk::AccessFlagBits::eTransferRead;
			toTransfer.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
			toTransfer.newLayout = vk::ImageLayout::eTransferSrcOptimal;
			toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toTransfer.image = *image;
			toTransfer.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
			commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, toTransfer);

			const bool bRedBlueSwapped = format == vk::Format::eB8G8R8A8Unorm;
			return SubmitAndReadBack(commandBuffer, *image, extent, bRedBlueSwapped);
		}

		const VulkanPainterTarget& GetTarget() const { return Target; }
		VulkanPainter& GetPainterPass() { return PainterPass; }

		// The view of the texture CreateTexture() made last, e.g. to blur it
		vk::ImageView GetLastTextureView() const { return *Textures.back().View; }

		// Blurs `source` into `target`, waits for the GPU, and reads the blurred image back
		PaintedImage Blur(vk::ImageView source, vk::Extent2D sourceExtent, VulkanBlurTarget& target, const BlurOptions& options = BlurOptions{})
		{
			vk::CommandBufferAllocateInfo allocInfo{};
			allocInfo.commandPool = *Context.GetGraphicsCommandPool();
			allocInfo.level = vk::CommandBufferLevel::ePrimary;
			allocInfo.commandBufferCount = 1;
			vk::raii::CommandBuffer commandBuffer = std::move(vk::raii::CommandBuffers(Context.GetDevice(), allocInfo).front());
			commandBuffer.begin(vk::CommandBufferBeginInfo{ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

			BlurPass.Blur(commandBuffer, source, sourceExtent, target, options);

			// The blur leaves its image ready to be sampled; copying needs it as a transfer source.
			vk::ImageMemoryBarrier toTransfer{};
			toTransfer.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
			toTransfer.dstAccessMask = vk::AccessFlagBits::eTransferRead;
			toTransfer.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
			toTransfer.newLayout = vk::ImageLayout::eTransferSrcOptimal;
			toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toTransfer.image = target.GetImage();
			toTransfer.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
			commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, toTransfer);

			return SubmitAndReadBack(commandBuffer, target.GetImage(), target.GetExtent(), false);
		}

		// A texture made from RGBA8 pixels, row by row, with a descriptor set like ImGui's; returns
		// what AddImage() and ImGui::Image() take. Nearest filtering, so tests can tell texels apart.
		ImTextureID CreateTexture(uint32_t width, uint32_t height, const std::vector<uint8_t>& rgba)
		{
			vk::raii::Device& device = Context.GetDevice();
			TestTexture& texture = Textures.emplace_back();

			vk::ImageCreateInfo imageInfo{};
			imageInfo.imageType = vk::ImageType::e2D;
			imageInfo.extent = vk::Extent3D(width, height, 1);
			imageInfo.mipLevels = 1;
			imageInfo.arrayLayers = 1;
			imageInfo.format = vk::Format::eR8G8B8A8Unorm;
			imageInfo.tiling = vk::ImageTiling::eOptimal;
			imageInfo.initialLayout = vk::ImageLayout::eUndefined;
			imageInfo.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
			imageInfo.samples = vk::SampleCountFlagBits::e1;
			texture.Image = vk::raii::Image(device, imageInfo);
			const vk::MemoryRequirements imageRequirements = texture.Image.getMemoryRequirements();
			texture.Memory = vk::raii::DeviceMemory(device, vk::MemoryAllocateInfo(imageRequirements.size, FindMemoryType(*Context.GetPhysicalDevice(), imageRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)));
			texture.Image.bindMemory(*texture.Memory, 0);

			vk::ImageViewCreateInfo viewInfo{};
			viewInfo.image = *texture.Image;
			viewInfo.viewType = vk::ImageViewType::e2D;
			viewInfo.format = vk::Format::eR8G8B8A8Unorm;
			viewInfo.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
			texture.View = vk::raii::ImageView(device, viewInfo);

			vk::SamplerCreateInfo samplerInfo{};
			samplerInfo.magFilter = vk::Filter::eNearest;
			samplerInfo.minFilter = vk::Filter::eNearest;
			samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
			samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
			samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
			texture.Sampler = vk::raii::Sampler(device, samplerInfo);

			// The pixels go through a host-visible buffer.
			vk::BufferCreateInfo bufferInfo{};
			bufferInfo.size = rgba.size();
			bufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
			vk::raii::Buffer staging(device, bufferInfo);
			const vk::MemoryRequirements bufferRequirements = staging.getMemoryRequirements();
			vk::raii::DeviceMemory stagingMemory(device, vk::MemoryAllocateInfo(bufferRequirements.size, FindMemoryType(*Context.GetPhysicalDevice(), bufferRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)));
			staging.bindMemory(*stagingMemory, 0);
			std::memcpy(stagingMemory.mapMemory(0, rgba.size()), rgba.data(), rgba.size());
			stagingMemory.unmapMemory();

			RunOnce([&](vk::raii::CommandBuffer& commandBuffer)
				{
					const vk::ImageSubresourceRange wholeImage(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
					vk::ImageMemoryBarrier toCopy{};
					toCopy.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
					toCopy.oldLayout = vk::ImageLayout::eUndefined;
					toCopy.newLayout = vk::ImageLayout::eTransferDstOptimal;
					toCopy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
					toCopy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
					toCopy.image = *texture.Image;
					toCopy.subresourceRange = wholeImage;
					commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, toCopy);

					vk::BufferImageCopy copy{};
					copy.imageSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
					copy.imageExtent = vk::Extent3D(width, height, 1);
					commandBuffer.copyBufferToImage(*staging, *texture.Image, vk::ImageLayout::eTransferDstOptimal, copy);

					vk::ImageMemoryBarrier toRead = toCopy;
					toRead.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
					toRead.dstAccessMask = vk::AccessFlagBits::eShaderRead;
					toRead.oldLayout = vk::ImageLayout::eTransferDstOptimal;
					toRead.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
					commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, toRead);
				});

			vk::DescriptorSetAllocateInfo setInfo{};
			setInfo.descriptorPool = *TexturePool;
			setInfo.descriptorSetCount = 1;
			setInfo.pSetLayouts = &*PainterPass.GetTextureSetLayout();
			texture.Set = std::move(vk::raii::DescriptorSets(device, setInfo).front());

			vk::DescriptorImageInfo imageDescriptor{ *texture.Sampler, *texture.View, vk::ImageLayout::eShaderReadOnlyOptimal };
			vk::WriteDescriptorSet write{};
			write.dstSet = *texture.Set;
			write.dstBinding = 0;
			write.descriptorCount = 1;
			write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
			write.pImageInfo = &imageDescriptor;
			device.updateDescriptorSets(write, {});

			// What ImGui's Vulkan backend uses as a texture's ImTextureID: its descriptor set
			return static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(static_cast<VkDescriptorSet>(*texture.Set)));
		}

		ImTextureID CreateTexture(const RgbaImage& image)
		{
			return CreateTexture(image.Width, image.Height, image.Pixels);
		}

		// Does what ImGui's backend does each frame: uploads the font atlas with the glyphs baked
		// so far, and gives it its ImTextureID.
		void UploadFontAtlas(ImFontAtlas& atlas)
		{
			ImTextureData& atlasTexture = *atlas.TexData;
			Require(atlasTexture.Format == ImTextureFormat_RGBA32, "The tests expect an RGBA font atlas");
			const uint8_t* pixels = static_cast<const uint8_t*>(atlasTexture.GetPixels());
			const std::vector<uint8_t> rgba(pixels, pixels + atlasTexture.GetSizeInBytes());
			atlasTexture.SetTexID(CreateTexture(static_cast<uint32_t>(atlasTexture.Width), static_cast<uint32_t>(atlasTexture.Height), rgba));
			atlasTexture.SetStatus(ImTextureStatus_OK);
		}

	private:
		struct TestTexture
		{
			vk::raii::Image Image{ nullptr };
			vk::raii::DeviceMemory Memory{ nullptr };
			vk::raii::ImageView View{ nullptr };
			vk::raii::Sampler Sampler{ nullptr };
			vk::raii::DescriptorSet Set{ nullptr };
		};

		// Ends `commandBuffer` with copying `image` (8 bits per channel, in TransferSrcOptimal
		// layout) into a buffer, submits it, waits for the GPU and returns the pixels as RGBA
		PaintedImage SubmitAndReadBack(vk::raii::CommandBuffer& commandBuffer, vk::Image image, vk::Extent2D extent, bool bRedBlueSwapped)
		{
			vk::raii::Device& device = Context.GetDevice();

			PaintedImage painted;
			painted.Width = extent.width;
			painted.Height = extent.height;
			const vk::DeviceSize byteCount = static_cast<vk::DeviceSize>(painted.Width) * painted.Height * 4;

			vk::BufferCreateInfo bufferInfo{};
			bufferInfo.size = byteCount;
			bufferInfo.usage = vk::BufferUsageFlagBits::eTransferDst;
			vk::raii::Buffer readback(device, bufferInfo);
			const vk::MemoryRequirements requirements = readback.getMemoryRequirements();
			vk::MemoryAllocateInfo memoryInfo{};
			memoryInfo.allocationSize = requirements.size;
			memoryInfo.memoryTypeIndex = FindMemoryType(*Context.GetPhysicalDevice(), requirements.memoryTypeBits,
				vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
			vk::raii::DeviceMemory readbackMemory(device, memoryInfo);
			readback.bindMemory(*readbackMemory, 0);

			vk::BufferImageCopy copy{};
			copy.imageSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
			copy.imageExtent = vk::Extent3D(extent.width, extent.height, 1);
			commandBuffer.copyImageToBuffer(image, vk::ImageLayout::eTransferSrcOptimal, *readback, copy);

			commandBuffer.end();
			vk::SubmitInfo submitInfo{};
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &*commandBuffer;
			Context.GetGraphicsQueue().submit(submitInfo);
			Context.GetGraphicsQueue().waitIdle();

			painted.Bytes.resize(static_cast<size_t>(byteCount));
			void* mapped = readbackMemory.mapMemory(0, byteCount);
			std::memcpy(painted.Bytes.data(), mapped, painted.Bytes.size());
			readbackMemory.unmapMemory();

			if (bRedBlueSwapped)
			{
				for (size_t offset = 0; offset < painted.Bytes.size(); offset += 4)
				{
					std::swap(painted.Bytes[offset], painted.Bytes[offset + 2]);
				}
			}
			return painted;
		}

		// Records commands into a command buffer, submits it and waits for the GPU
		template<typename TRecord>
		void RunOnce(TRecord&& record)
		{
			vk::CommandBufferAllocateInfo allocInfo{};
			allocInfo.commandPool = *Context.GetGraphicsCommandPool();
			allocInfo.level = vk::CommandBufferLevel::ePrimary;
			allocInfo.commandBufferCount = 1;
			vk::raii::CommandBuffer commandBuffer = std::move(vk::raii::CommandBuffers(Context.GetDevice(), allocInfo).front());
			commandBuffer.begin(vk::CommandBufferBeginInfo{ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
			record(commandBuffer);
			commandBuffer.end();

			vk::SubmitInfo submitInfo{};
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &*commandBuffer;
			Context.GetGraphicsQueue().submit(submitInfo);
			Context.GetGraphicsQueue().waitIdle();
		}

		vk::raii::DescriptorPool TexturePool{ nullptr };
		std::deque<TestTexture> Textures;

		GLFWwindow* Window = nullptr;
		Nyx::VulkanContext Context;
		VulkanPainter PainterPass;
		VulkanBlur BlurPass;
		VulkanPainterTarget Target;
	};

	Painter StartFrame(uint32_t width = 64, uint32_t height = 64)
	{
		Painter painter;
		painter.BeginFrame(width, height);
		return painter;
	}

	void TestFilledRectAndAntiAliasing(GpuPainter& gpu)
	{
		// The rect's edges run through the middle of pixel column 10 and row 10.
		Painter painter = StartFrame();
		painter.AddRect(PainterRect{ { 10.5f, 10.5f }, { 30.5f, 30.5f } }, PainterStyle::Filled(Orange));
		painter.EndFrame();
		const PaintedImage image = gpu.Paint(painter);

		image.RequirePixel(20, 20, Orange, "Inside a rect");
		image.RequirePixel(5, 5, Transparent, "Outside a rect");
		image.RequirePixel(10, 20, Orange * 0.5f, "A pixel half covered by the edge should be half covered, premultiplied");
		image.RequirePixel(9, 20, Transparent, "A pixel a whole pixel outside the edge");
		image.RequirePixel(40, 40, Transparent, "Outside a rect, in a tile without commands");
	}

	void TestCircleEdge(GpuPainter& gpu)
	{
		Painter painter = StartFrame();
		painter.AddCircle({ 32.5f, 32.5f }, 10.0f, PainterStyle::Filled(White));
		const PaintedImage image = gpu.Paint(painter);

		image.RequirePixel(32, 32, White, "The centre of a circle");
		image.RequireAlpha(42, 32, 0.5f, "A pixel centred on a circle's edge should be half covered");
		image.RequireAlpha(44, 32, 0.0f, "Outside a circle");
		image.RequireAlpha(38, 38, 1.0f, "Inside a circle, near its diagonal edge");
	}

	void TestDrawingOrderAndTransparency(GpuPainter& gpu)
	{
		Painter painter = StartFrame();
		painter.AddRect(PainterRect{ { 0, 0 }, { 40, 40 } }, PainterStyle::Filled(Red));
		painter.AddRect(PainterRect{ { 20, 20 }, { 60, 60 } }, PainterStyle::Filled(glm::vec4(0.0f, 0.0f, 1.0f, 0.5f)));
		const PaintedImage image = gpu.Paint(painter);

		image.RequirePixel(10, 10, Red, "Only the first rect");
		image.RequirePixel(30, 30, glm::vec4(0.5f, 0.0f, 0.5f, 1.0f), "Half transparent blue over red");
		image.RequirePixel(50, 50, glm::vec4(0.0f, 0.0f, 0.5f, 0.5f), "Half transparent blue alone");
	}

	void TestClipRect(GpuPainter& gpu)
	{
		Painter painter = StartFrame();
		painter.PushClipRect(PainterRect{ { 16, 16 }, { 48, 48 } });
		painter.AddRect(PainterRect{ { 0, 0 }, { 64, 64 } }, PainterStyle::Filled(White));
		painter.PopClipRect();
		const PaintedImage image = gpu.Paint(painter);

		image.RequirePixel(20, 20, White, "Inside the clip rect");
		image.RequirePixel(8, 8, Transparent, "Before the clip rect");
		image.RequirePixel(50, 50, Transparent, "Past the clip rect");
		image.RequirePixel(15, 30, Transparent, "Just outside the clip rect's edge");
		image.RequirePixel(16, 30, White, "Just inside the clip rect's edge");
	}

	void TestOutlineAndRoundedCorners(GpuPainter& gpu)
	{
		Painter painter = StartFrame();
		painter.AddRect(PainterRect{ { 10, 10 }, { 50, 50 } }, PainterStyle::Outlined(White, 4.0f));
		const PaintedImage outline = gpu.Paint(painter);
		outline.RequireAlpha(30, 30, 0.0f, "Inside an outline");
		outline.RequireAlpha(10, 30, 1.0f, "On an outline, just inside the edge");
		outline.RequireAlpha(8, 30, 1.0f, "On an outline, just outside the edge");
		outline.RequireAlpha(13, 30, 0.0f, "Past the inner side of a 4 pixel outline");

		painter = StartFrame();
		painter.AddRoundedRect(PainterRect{ { 10, 10 }, { 50, 50 } }, 12.0f, PainterStyle::Filled(White));
		const PaintedImage rounded = gpu.Paint(painter);
		rounded.RequireAlpha(11, 11, 0.0f, "A rounded corner cuts the rect's corner off");
		rounded.RequireAlpha(30, 11, 1.0f, "The top edge between the corners");
		rounded.RequireAlpha(30, 30, 1.0f, "The middle of a rounded rect");
	}

	void TestLines(GpuPainter& gpu)
	{
		Painter painter = StartFrame();
		painter.AddLine({ 10, 10 }, { 50, 50 }, 4.0f, EPainterLineCap::Round, PainterStyle::Filled(White));
		painter.AddLine({ 10.0f, 58.5f }, { 50.0f, 58.5f }, 4.0f, EPainterLineCap::Flat, PainterStyle::Filled(White));
		const PaintedImage image = gpu.Paint(painter);

		image.RequireAlpha(30, 30, 1.0f, "On a diagonal line");
		image.RequireAlpha(30, 10, 0.0f, "Beside a diagonal line");
		image.RequireAlpha(9, 9, 1.0f, "Inside a round cap, past the line's start");

		image.RequireAlpha(11, 58, 1.0f, "On a flat-capped line");
		image.RequireAlpha(9, 58, 0.0f, "Past a flat cap");
		image.RequireAlpha(30, 57, 1.0f, "Inside a 4 pixel wide line, near its side");
		image.RequireAlpha(30, 61, 0.0f, "Beside a 4 pixel wide line");
	}

	void TestTrianglesAndSectors(GpuPainter& gpu)
	{
		Painter painter = StartFrame();
		painter.AddTriangle({ 10, 10 }, { 50, 10 }, { 10, 50 }, PainterStyle::Filled(White));
		const PaintedImage triangle = gpu.Paint(painter);
		triangle.RequireAlpha(15, 15, 1.0f, "Inside a triangle");
		triangle.RequireAlpha(45, 45, 0.0f, "Past a triangle's long edge");

		// A quarter pie from 0 (right) to pi/2 (down, since y points down)
		painter = StartFrame();
		painter.AddCircleSector({ 32, 32 }, 0.0f, 20.0f, 0.0f, 1.5707963f, PainterStyle::Filled(White));
		const PaintedImage pie = gpu.Paint(painter);
		pie.RequireAlpha(40, 40, 1.0f, "Inside a quarter pie");
		pie.RequireAlpha(24, 24, 0.0f, "Opposite a quarter pie");
		pie.RequireAlpha(40, 24, 0.0f, "Before a quarter pie's start angle");
		pie.RequireAlpha(24, 40, 0.0f, "Past a quarter pie's end angle");

		// A ring, and a sector that is almost a full ring: no seam where its angles start.
		painter = StartFrame();
		painter.AddCircleSector({ 32, 32 }, 10.0f, 20.0f, 0.0f, 6.2831853f, PainterStyle::Filled(White));
		const PaintedImage ring = gpu.Paint(painter);
		ring.RequireAlpha(32, 32, 0.0f, "The hole of a ring");
		ring.RequireAlpha(47, 32, 1.0f, "On a ring");

		painter = StartFrame();
		painter.AddCircleSector({ 32, 32 }, 10.0f, 20.0f, 0.0f, 6.0f, PainterStyle::Filled(White));
		const PaintedImage almostRing = gpu.Paint(painter);
		almostRing.RequireAlpha(47, 33, 1.0f, "Just after a nearly full sector's start angle");
		almostRing.RequireAlpha(17, 32, 1.0f, "Halfway round a nearly full sector");
	}

	void TestShadowsAndGlow(GpuPainter& gpu)
	{
		PainterStyle withShadow = PainterStyle::Filled(White);
		withShadow.Effects.Shadow.Color = glm::vec4(0.0f, 0.0f, 0.0f, 0.5f);
		withShadow.Effects.Shadow.Offset = glm::vec2(0.0f, 10.0f);

		Painter painter = StartFrame();
		painter.AddRect(PainterRect{ { 10, 10 }, { 30, 30 } }, withShadow);
		const PaintedImage shadow = gpu.Paint(painter);
		shadow.RequirePixel(20, 20, White, "A shape covers its own shadow");
		shadow.RequirePixel(20, 35, glm::vec4(0.0f, 0.0f, 0.0f, 0.5f), "A hard shadow below its shape");
		shadow.RequirePixel(20, 45, Transparent, "Past the shadow");

		// Two overlapping shapes in a group cast one shadow, not two on top of each other.
		PainterEffects groupShadow;
		groupShadow.Shadow = withShadow.Effects.Shadow;
		groupShadow.Shadow.Offset = glm::vec2(0.0f, 20.0f);
		painter = StartFrame();
		painter.BeginGroup(groupShadow);
		painter.AddRect(PainterRect{ { 10, 10 }, { 30, 30 } }, PainterStyle::Filled(White));
		painter.AddRect(PainterRect{ { 20, 10 }, { 40, 30 } }, PainterStyle::Filled(White));
		painter.EndGroup();
		const PaintedImage grouped = gpu.Paint(painter);
		grouped.RequireAlpha(25, 45, 0.5f, "Where the shadows of a group's shapes overlap");

		PainterStyle glowing = PainterStyle::Filled(White);
		glowing.Effects.Glow.Color = Orange;
		glowing.Effects.Glow.Distance = 10.0f;
		painter = StartFrame();
		painter.AddCircle({ 32.5f, 32.5f }, 8.0f, glowing);
		const PaintedImage glow = gpu.Paint(painter);
		glow.RequirePixel(32, 32, White, "A shape covers its own glow");
		glow.RequirePixel(45, 32, Orange * 0.5f, "Halfway out of a glow");
		glow.RequirePixel(55, 32, Transparent, "Past a glow");
	}

	void TestClearColorAndHeatmap(GpuPainter& gpu)
	{
		PainterDrawOptions clearBlue;
		clearBlue.ClearColor = glm::vec4(0.0f, 0.0f, 1.0f, 0.5f);
		Painter painter = StartFrame();
		const PaintedImage cleared = gpu.Paint(painter, clearBlue);
		cleared.RequirePixel(5, 5, glm::vec4(0.0f, 0.0f, 0.5f, 0.5f), "The clear colour, premultiplied");

		PainterDrawOptions heatmap;
		heatmap.DebugView = EPainterDebugView::Heatmap;
		painter = StartFrame();
		painter.AddRect(PainterRect{ { 0, 0 }, { 6, 6 } }, PainterStyle::Filled(Black));
		const PaintedImage heat = gpu.Paint(painter, heatmap);
		const glm::vec4 tinted = heat.At(2, 2);
		Require(tinted.b > 0.3f, "The heatmap should tint a tile with one command blue, got " + Describe(tinted));
		heat.RequirePixel(40, 40, Transparent, "The heatmap leaves tiles without commands alone");
	}

	// Text and images

	// An ImGui context whose font atlas bakes glyphs on demand, as the editor's does. The font
	// snaps glyphs to whole pixels without oversampling, so one atlas texel is one pixel and the
	// painted text can be compared with the atlas texel by texel.
	class ImGuiFonts
	{
	public:
		ImGuiFonts()
		{
			Context = ImGui::CreateContext();
			ImGuiIO& io = ImGui::GetIO();
			io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
			io.DisplaySize = ImVec2(64.0f, 64.0f);
			io.DeltaTime = 1.0f / 60.0f;
			// No imgui.ini next to wherever the tests run
			io.IniFilename = nullptr;
			ImFontConfig config;
			config.PixelSnapH = true;
			config.OversampleH = 1;
			config.OversampleV = 1;
			Font = io.Fonts->AddFontDefault(&config);
			ImGui::NewFrame();
		}

		~ImGuiFonts()
		{
			ImGui::EndFrame();
			ImGui::DestroyContext(Context);
		}

		ImGuiFonts(const ImGuiFonts&) = delete;
		ImGuiFonts& operator=(const ImGuiFonts&) = delete;

		ImFont* Font = nullptr;

	private:
		ImGuiContext* Context = nullptr;
	};

	void TestTextMatchesTheFontAtlas(GpuPainter& gpu, const ImGuiFonts& fonts)
	{
		Painter painter = StartFrame();
		painter.AddText(fonts.Font, 13.0f, { 10.0f, 10.0f }, White, "H");
		gpu.UploadFontAtlas(*fonts.Font->OwnerAtlas);
		const PaintedImage image = gpu.Paint(painter);

		const auto glyph = GetPayload<PainterTexturedRectPayload>(painter.GetCommands()[0]);
		const glm::vec2 rectMin = glyph.Rect.Min;
		Require(rectMin == glm::floor(rectMin) && glyph.Rect.Max == glm::floor(glyph.Rect.Max), "The glyph should cover whole pixels");

		// Every pixel of the glyph is exactly its texel in the atlas: white, with the texel's alpha.
		ImTextureData& atlas = *fonts.Font->OwnerAtlas->TexData;
		const int texelX0 = static_cast<int>(std::lround(glyph.UVs.Min.x * atlas.Width));
		const int texelY0 = static_cast<int>(std::lround(glyph.UVs.Min.y * atlas.Height));
		float strongest = 0.0f;
		for (uint32_t y = static_cast<uint32_t>(rectMin.y); y < static_cast<uint32_t>(glyph.Rect.Max.y); ++y)
		{
			for (uint32_t x = static_cast<uint32_t>(rectMin.x); x < static_cast<uint32_t>(glyph.Rect.Max.x); ++x)
			{
				const int texelX = texelX0 + static_cast<int>(x - static_cast<uint32_t>(rectMin.x));
				const int texelY = texelY0 + static_cast<int>(y - static_cast<uint32_t>(rectMin.y));
				const float texelAlpha = static_cast<const uint8_t*>(atlas.GetPixelsAt(texelX, texelY))[3] / 255.0f;
				image.RequirePixel(x, y, glm::vec4(texelAlpha), "A glyph pixel should be its atlas texel");
				strongest = std::max(strongest, texelAlpha);
			}
		}
		Require(strongest > 0.9f, "The glyph 'H' should have solid pixels");
		image.RequirePixel(40, 40, Transparent, "Away from the text");
	}

	void TestTextCastsNoBoxShadow(GpuPainter& gpu, const ImGuiFonts& fonts)
	{
		// A rect and a line of text in one group with a shadow far below: only the rect's shadow
		// shows; the glyphs' rects cast none.
		PainterEffects shadow;
		shadow.Shadow.Color = Black;
		shadow.Shadow.Offset = glm::vec2(0.0f, 30.0f);
		Painter painter = StartFrame();
		painter.BeginGroup(shadow);
		painter.AddRect(PainterRect{ { 2, 2 }, { 10, 10 } }, PainterStyle::Filled(White));
		painter.AddText(fonts.Font, 13.0f, { 30.0f, 2.0f }, White, "HH");
		painter.EndGroup();
		gpu.UploadFontAtlas(*fonts.Font->OwnerAtlas);
		const PaintedImage image = gpu.Paint(painter);

		image.RequirePixel(6, 36, Black, "The rect's shadow");
		const auto glyph = GetPayload<PainterTexturedRectPayload>(painter.GetCommands()[1]);
		const glm::vec2 glyphCentre = (glyph.Rect.Min + glyph.Rect.Max) * 0.5f;
		image.RequirePixel(static_cast<uint32_t>(glyphCentre.x), static_cast<uint32_t>(glyphCentre.y + 30.0f), Transparent, "Below a glyph, where a box shadow would be");
	}

	void TestImages(GpuPainter& gpu)
	{
		// 2x2 texels: red, green / blue, white
		const ImTextureID quarters = gpu.CreateTexture(2, 2, { 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255 });

		Painter painter = StartFrame();
		painter.AddImage(quarters, PainterRect{ { 8, 8 }, { 24, 24 } }, PainterStyle::Filled(White));
		painter.AddImage(quarters, PainterRect{ { 32, 8 }, { 48, 24 } }, PainterStyle::Filled(glm::vec4(1.0f, 1.0f, 1.0f, 0.5f)));
		painter.AddImage(quarters, PainterRect{ { 8, 32 }, { 24, 48 } }, PainterStyle::Filled(White), PainterRect{ { 0.5f, 0.0f }, { 1.0f, 0.5f } });
		const PaintedImage image = gpu.Paint(painter);

		image.RequirePixel(10, 10, Red, "The top left texel");
		image.RequirePixel(20, 10, glm::vec4(0.0f, 1.0f, 0.0f, 1.0f), "The top right texel");
		image.RequirePixel(10, 20, glm::vec4(0.0f, 0.0f, 1.0f, 1.0f), "The bottom left texel");
		image.RequirePixel(20, 20, White, "The bottom right texel");
		image.RequirePixel(34, 10, Red * 0.5f, "An image tinted half transparent, premultiplied");
		image.RequirePixel(10, 34, glm::vec4(0.0f, 1.0f, 0.0f, 1.0f), "Part of a texture: its top right texel, stretched");
		image.RequirePixel(22, 46, glm::vec4(0.0f, 1.0f, 0.0f, 1.0f), "Part of a texture, at its far corner");
		image.RequirePixel(28, 28, Transparent, "Between the images");
	}

	void TestTextureNotUploadedYetIsInvisible(GpuPainter& gpu)
	{
		// The font atlas has no ImTextureID before ImGui first uploads it; nothing should show or break.
		Painter painter = StartFrame();
		painter.AddImage(ImTextureRef(ImTextureID_Invalid), PainterRect{ { 8, 8 }, { 24, 24 } }, PainterStyle::Filled(White));
		// A null descriptor set, as an ImTextureID of 0 would be with ImGui's Vulkan backend
		painter.AddImage(ImTextureRef(static_cast<ImTextureID>(0)), PainterRect{ { 8, 40 }, { 24, 56 } }, PainterStyle::Filled(White));
		painter.AddRect(PainterRect{ { 40, 40 }, { 50, 50 } }, PainterStyle::Filled(White));
		const PaintedImage image = gpu.Paint(painter);

		image.RequirePixel(16, 16, Transparent, "An image without a texture yet");
		image.RequirePixel(16, 48, Transparent, "An image with a null texture");
		image.RequirePixel(45, 45, White, "A shape in the same frame");
	}

	// With the environment variable NYX_PAINTER_TEST_OUTPUT set to a folder, painted images are
	// saved there as PNG files, to look at what the GPU painted.
	void SaveForLooking(const PaintedImage& image, const std::string& fileName)
	{
		const char* folder = std::getenv("NYX_PAINTER_TEST_OUTPUT");
		if (folder == nullptr || *folder == '\0')
		{
			return;
		}
		const std::filesystem::path path = std::filesystem::path(folder) / fileName;
		const int stride = static_cast<int>(image.Width) * 4;
		if (stbi_write_png(path.string().c_str(), static_cast<int>(image.Width), static_cast<int>(image.Height), 4, image.Bytes.data(), stride) == 0)
		{
			throw std::runtime_error("Couldn't write " + path.string());
		}
		std::cout << "Saved " << path.string() << "\n";
	}

	void TestImageGlow(GpuPainter& gpu)
	{
		// 2x2 texels: red, green / blue, white
		const ImTextureID quarters = gpu.CreateTexture(2, 2, { 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255 });
		const glm::vec4 green{ 0.0f, 1.0f, 0.0f, 1.0f };
		const glm::vec4 blue{ 0.0f, 0.0f, 1.0f, 1.0f };

		// A rect from 20 to 44 both ways, glowing 18 pixels out: a pixel whose centre is 4.5 pixels
		// out gets 1 - 4.5 / 18 = 0.75 of the colour at the nearest point of the edge, at 13.5
		// pixels 0.25.
		Painter painter = StartFrame();
		painter.AddImageGlow(quarters, PainterRect{ { 20, 20 }, { 44, 44 } }, 18.0f, 1.0f);
		const PaintedImage glow = gpu.Paint(painter);
		SaveForLooking(glow, "ImageGlow.png");

		glow.RequirePixel(25, 25, Red, "Inside the rect, the image itself");
		glow.RequirePixel(15, 26, Red * 0.75f, "Left of the upper half: the top left texel");
		glow.RequirePixel(15, 38, blue * 0.75f, "Left of the lower half: the bottom left texel");
		glow.RequirePixel(48, 26, green * 0.75f, "Right of the upper half: the top right texel");
		glow.RequirePixel(38, 48, White * 0.75f, "Below the right half: the bottom right texel");
		glow.RequirePixel(6, 26, Red * 0.25f, "Further out, fainter");
		glow.RequirePixel(1, 26, Transparent, "Past the reach");
		const float offCorner = 1.0f - std::sqrt(2.0f * 4.5f * 4.5f) / 18.0f;
		glow.RequirePixel(15, 15, Red * offCorner, "Off a corner: the corner's texel, faded by the distance to the corner");

		// The top left texel only, at half strength. The glow right of it is still red: the edge
		// of the part reads its own texel, never the green one next to it.
		painter = StartFrame();
		painter.AddImageGlow(quarters, PainterRect{ { 20, 20 }, { 44, 44 } }, 18.0f, 0.5f, PainterRect{ { 0.0f, 0.0f }, { 0.5f, 0.5f } });
		const PaintedImage part = gpu.Paint(painter);
		part.RequirePixel(30, 30, Red * 0.5f, "Inside, at half strength");
		part.RequirePixel(48, 38, Red * 0.375f, "Right of a part of a texture: the part's own edge texel");
		part.RequirePixel(38, 48, Red * 0.375f, "Below a part of a texture: the part's own edge texel");
	}

	void TestGlass(GpuPainter& gpu)
	{
		// The backdrop: 2x2 texels stretched over the 64x64 target, so each quarter of the
		// target is one colour: red, green / blue, white
		const ImTextureID quarters = gpu.CreateTexture(2, 2, { 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255 });
		const glm::vec4 green{ 0.0f, 1.0f, 0.0f, 1.0f };
		const glm::vec4 blue{ 0.0f, 0.0f, 1.0f, 1.0f };

		// Clear glass over the whole target, without bend or rim: every pixel shows the backdrop
		// at its own place.
		PainterGlass plain;
		plain.Refraction = 0.0f;
		plain.RimStrength = 0.0f;
		Painter painter = StartFrame();
		painter.AddGlass(quarters, PainterRect{ { 0, 0 }, { 64, 64 } }, 0.0f, PainterStyle::Filled(Transparent), plain);
		const PaintedImage clear = gpu.Paint(painter);
		clear.RequirePixel(10, 10, Red, "Clear glass shows the backdrop's top left at the top left");
		clear.RequirePixel(50, 10, green, "Clear glass shows the backdrop's top right at the top right");
		clear.RequirePixel(10, 50, blue, "Clear glass shows the backdrop's bottom left at the bottom left");
		clear.RequirePixel(50, 50, White, "Clear glass shows the backdrop's bottom right at the bottom right");

		// Glass ending just left of where green starts (x = 32), bending 8 pixels over an 8
		// pixel edge, tinted half blue. Deep inside it shows its own place; at its right edge, the
		// green from beyond it.
		PainterGlass bent;
		bent.Refraction = 8.0f;
		bent.EdgeWidth = 8.0f;
		bent.RimStrength = 0.0f;
		painter = StartFrame();
		painter.AddGlass(quarters, PainterRect{ { 8, 8 }, { 30, 56 } }, 0.0f, PainterStyle::Filled(glm::vec4(0.0f, 0.0f, 1.0f, 0.5f)), bent);
		const PaintedImage tinted = gpu.Paint(painter);
		SaveForLooking(tinted, "Glass.png");
		tinted.RequirePixel(18, 20, glm::vec4(0.5f, 0.0f, 0.5f, 1.0f), "Inside, the backdrop at its own place with the tint laid over it at half");
		tinted.RequirePixel(29, 20, glm::vec4(0.0f, 0.5f, 0.5f, 1.0f), "At the right edge, the backdrop from beyond the glass");
		tinted.RequirePixel(40, 20, Transparent, "Outside the glass, nothing");

		// A rim of light over a black backdrop: brighter where the edge faces up and left. A
		// pixel half a pixel in from the edge is (1 - 0.5 / 8)^2 of the way to the edge's full light.
		const ImTextureID black = gpu.CreateTexture(1, 1, { 0, 0, 0, 255 });
		PainterGlass rimmed;
		rimmed.Refraction = 0.0f;
		rimmed.EdgeWidth = 8.0f;
		rimmed.RimStrength = 0.5f;
		painter = StartFrame();
		painter.AddGlass(black, PainterRect{ { 8, 8 }, { 56, 56 } }, 0.0f, PainterStyle::Filled(Transparent), rimmed);
		const PaintedImage rim = gpu.Paint(painter);
		const float nearEdge = std::pow(1.0f - 0.5f / 8.0f, 2.0f);
		const float facingUp = 0.35f + 0.65f * std::sqrt(0.5f);
		const float facingDown = 0.35f;
		rim.RequirePixel(32, 8, glm::vec4(glm::vec3(0.5f * nearEdge * facingUp), 1.0f), "The top edge faces the light");
		rim.RequirePixel(32, 55, glm::vec4(glm::vec3(0.5f * nearEdge * facingDown), 1.0f), "The bottom edge faces away from it, and is dimmer");
		rim.RequirePixel(32, 32, Black, "No rim away from the edge");
	}

	void TestTestSheetPaints(GpuPainter& gpu, const ImGuiFonts& fonts)
	{
		// The whole test sheet, as the Painter Test window shows it at 100%, on an opaque background
		const ImTextureID gradient = gpu.CreateTexture(MakePainterTestGradient());
		Painter painter = StartFrame(static_cast<uint32_t>(PainterTestSheetSize.x), static_cast<uint32_t>(PainterTestSheetSize.y));
		DrawPainterTestSheet(painter, fonts.Font, fonts.Font->OwnerAtlas->TexRef, gradient);
		painter.EndFrame();
		gpu.UploadFontAtlas(*fonts.Font->OwnerAtlas);

		PainterDrawOptions options;
		options.ClearColor = glm::vec4(0.1f, 0.1f, 0.12f, 1.0f);
		const PaintedImage image = gpu.Paint(painter, options);

		Require(gpu.GetTarget().GetTiles().Problems.empty(), "The test sheet should paint without problems");
		size_t paintedPixels = 0;
		for (uint32_t y = 0; y < image.Height; ++y)
		{
			for (uint32_t x = 0; x < image.Width; ++x)
			{
				const glm::vec4 pixel = image.At(x, y);
				Require(pixel.a > 1.0f - Tolerance, "An opaque background should stay opaque everywhere");
				if (glm::length(glm::vec3(pixel) - glm::vec3(0.1f, 0.1f, 0.12f)) > 0.05f)
				{
					++paintedPixels;
				}
			}
		}
		Require(paintedPixels > image.Width * image.Height / 20, "The test sheet should cover a good part of its area, got " + std::to_string(paintedPixels) + " pixels");
		SaveForLooking(image, "PainterTestSheet.png");

		// The same sheet at 400%, its top left, to look at edges up close
		painter = StartFrame(static_cast<uint32_t>(PainterTestSheetSize.x), static_cast<uint32_t>(PainterTestSheetSize.y));
		painter.PushTransform(4.0f, { -560.0f, -40.0f });
		DrawPainterTestSheet(painter, fonts.Font, fonts.Font->OwnerAtlas->TexRef, gradient);
		painter.PopTransform();
		gpu.UploadFontAtlas(*fonts.Font->OwnerAtlas);
		SaveForLooking(gpu.Paint(painter, options), "PainterTestSheet400.png");
	}

	// Segments: drawn into a render pass that already holds other drawing, as ImGui's will

	void TestSegmentsDrawInOrderWithOtherDrawing(GpuPainter& gpu)
	{
		const glm::vec4 gray{ 0.2f, 0.2f, 0.2f, 1.0f };
		const glm::vec4 blue{ 0.0f, 0.0f, 1.0f, 1.0f };
		const glm::vec4 green{ 0.0f, 1.0f, 0.0f, 1.0f };
		const glm::vec4 inShadow{ 0.1f, 0.1f, 0.1f, 1.0f }; // half black over gray
		const ImTextureID magenta = gpu.CreateTexture(1, 1, { 255, 0, 255, 255 });
		const ImTextureID cyan = gpu.CreateTexture(1, 1, { 0, 255, 255, 255 });

		// Two painters, as two windows have, each with a shadow of its own: a segment of the second
		// must find the second's group and texture, not the first's.
		PainterStyle redWithShadow = PainterStyle::Filled(Red);
		redWithShadow.Effects.Shadow.Color = glm::vec4(0.0f, 0.0f, 0.0f, 0.5f);
		redWithShadow.Effects.Shadow.Offset = glm::vec2(0.0f, 32.0f);
		Painter first = StartFrame();
		first.AddRect(PainterRect{ { 4, 4 }, { 28, 28 } }, redWithShadow);
		first.PushClipRect(PainterRect{ { 24, 24 }, { 48, 48 } });
		first.AddRect(PainterRect{ { 24, 24 }, { 56, 56 } }, PainterStyle::Filled(green));
		first.PopClipRect();
		first.AddImage(magenta, PainterRect{ { 30, 4 }, { 40, 12 } }, PainterStyle::Filled(White));
		first.PushClipRect(PainterRect{});
		first.AddRect(PainterRect{ { 0, 0 }, { 64, 64 } }, PainterStyle::Filled(White));
		first.PopClipRect();

		PainterStyle withShadow = PainterStyle::Filled(White);
		withShadow.Effects.Shadow.Color = glm::vec4(0.0f, 0.0f, 0.0f, 0.5f);
		withShadow.Effects.Shadow.Offset = glm::vec2(0.0f, 24.0f);
		Painter second = StartFrame();
		second.AddImage(cyan, PainterRect{ { 44, 4 }, { 60, 20 } }, withShadow);

		VulkanPainterSegments segments;
		const PainterBudget budget;
		const uint32_t redRect = segments.Add(first, PainterSegment{ 0, 1 }, budget);
		const uint32_t greenRectAndImage = segments.Add(first, PainterSegment{ 1, 2 }, budget);
		const uint32_t clippedAway = segments.Add(first, PainterSegment{ 3, 1 }, budget);
		const uint32_t cyanImage = segments.Add(second, PainterSegment{ 0, 1 }, budget);
		VulkanPainter& painterPass = gpu.GetPainterPass();
		painterPass.Upload(segments);

		// The swapchain's usual format, not the canvases': a second pipeline
		const vk::Format format = vk::Format::eB8G8R8A8Unorm;
		const vk::Extent2D extent{ 64, 64 };
		const PaintedImage image = gpu.RenderInto(format, extent, gray, [&](vk::CommandBuffer commandBuffer)
			{
				painterPass.DrawSegment(commandBuffer, segments, redRect, format, extent);

				// Something else drawing between two segments, as ImGui does: a blue square
				const vk::ClearAttachment clearBlue(vk::ImageAspectFlagBits::eColor, 0, vk::ClearColorValue(std::array<float, 4>{ 0.0f, 0.0f, 1.0f, 1.0f }));
				const vk::ClearRect square(vk::Rect2D({ 16, 16 }, { 24, 24 }), 0, 1);
				commandBuffer.clearAttachments(clearBlue, square);

				painterPass.DrawSegment(commandBuffer, segments, greenRectAndImage, format, extent);
				painterPass.DrawSegment(commandBuffer, segments, clippedAway, format, extent);
				painterPass.DrawSegment(commandBuffer, segments, cyanImage, format, extent);
			});
		SaveForLooking(image, "PainterSegments.png");

		image.RequirePixel(8, 8, Red, "The first segment");
		image.RequirePixel(20, 20, blue, "Drawing after a segment covers it");
		image.RequirePixel(36, 36, green, "A segment after other drawing covers it");
		image.RequirePixel(52, 52, gray, "Past a segment's clip rect, what was beneath stays");
		image.RequirePixel(34, 8, glm::vec4(1.0f, 0.0f, 1.0f, 1.0f), "An image in a segment with its painter's texture");
		image.RequirePixel(8, 44, inShadow, "The first painter's shadow, 32 pixels below");
		image.RequirePixel(52, 12, glm::vec4(0.0f, 1.0f, 1.0f, 1.0f), "The second painter's image, with its own texture");
		image.RequirePixel(52, 30, inShadow, "The second painter's shadow, 24 pixels below: its own group");
		image.RequirePixel(2, 62, gray, "Outside every segment's tiles");

		Require(segments.GetSegmentCount() == 4 && segments.GetStats(clippedAway).CulledCommands == 1, "A segment wholly clipped away should be culled");
		Require(segments.GetProblems().empty(), "The segments should bin without problems");

		// What the statistics count as uploaded (stage 3, change 10): both painters' commands and
		// every tile entry at least, and nothing once cleared
		uint32_t entries = 0;
		for (const uint32_t segment : { redRect, greenRectAndImage, clippedAway, cyanImage })
		{
			entries += segments.GetStats(segment).TileEntries;
		}
		const size_t commands = first.GetCommands().size() + second.GetCommands().size();
		Require(entries > 0 && segments.GetUploadBytes() >= commands * sizeof(PainterCommand) + entries * sizeof(uint32_t),
			"The upload should hold the painters' commands and the segments' tile entries");

		// The heat over the whole target (stage 3, change 11): every segment's tiles land where
		// its grid lies, so the heat holds all their entries and tiles, and overlapping segments add up.
		PainterHeatGrid heat;
		heat.Reset(extent.width, extent.height);
		uint32_t tilesInUse = 0;
		for (const uint32_t segment : { redRect, greenRectAndImage, clippedAway, cyanImage })
		{
			segments.AddHeat(segment, heat);
			tilesInUse += segments.GetStats(segment).TilesInUse;
		}
		uint32_t heatCommands = 0;
		uint32_t heatDraws = 0;
		for (size_t index = 0; index < heat.Commands.size(); ++index)
		{
			heatCommands += heat.Commands[index];
			heatDraws += heat.Draws[index];
		}
		Require(heat.TilesX == 8 && heat.TilesY == 8 && heatCommands == entries && heatDraws == tilesInUse,
			"The heat should hold every segment's tile entries and tiles in use");
		// Pixel (8, 8) is under the red rect and its shadow; (60, 2) only under the cyan image's segment.
		Require(heat.Draws[1 * 8 + 1] >= 1 && heat.Commands[1 * 8 + 1] >= 1, "A tile under the first segment should have its heat");
		Require(heat.Draws[0 * 8 + 7] == 1, "A tile under only the second painter's segment should be covered by one draw");
		segments.Clear();
		Require(segments.GetUploadBytes() == 0, "Cleared segments should upload nothing");
	}

	void TestTestSheetAsSegmentsMatchesTheCanvas(GpuPainter& gpu, const ImGuiFonts& fonts)
	{
		const glm::vec4 background{ 0.1f, 0.1f, 0.12f, 1.0f };
		const uint32_t width = static_cast<uint32_t>(PainterTestSheetSize.x);
		const uint32_t height = static_cast<uint32_t>(PainterTestSheetSize.y);

		Painter sheet = StartFrame(width, height);
		DrawPainterTestSheet(sheet, fonts.Font, fonts.Font->OwnerAtlas->TexRef, gpu.CreateTexture(MakePainterTestGradient()));
		sheet.EndFrame();
		gpu.UploadFontAtlas(*fonts.Font->OwnerAtlas);

		PainterDrawOptions options;
		options.ClearColor = background;
		const PaintedImage canvas = gpu.Paint(sheet, options);

		// The sheet in two segments, split where no group goes across, after another painter's
		// commands and group, so that every offset is in use
		const std::vector<PainterCommand>& commands = sheet.GetCommands();
		uint32_t split = static_cast<uint32_t>(commands.size() / 2);
		while (split < commands.size() && commands[split].Group != PainterNoGroup && commands[split].Group == commands[split - 1].Group)
		{
			++split;
		}
		Painter before = StartFrame(width, height);
		before.AddRect(PainterRect{ { 0, 0 }, { 4, 4 } }, PainterStyle::Filled(White));
		PainterStyle glowing = PainterStyle::Filled(White);
		glowing.Effects.Glow.Color = Orange;
		glowing.Effects.Glow.Distance = 4.0f;
		before.AddCircle({ 2, 2 }, 1.0f, glowing);

		VulkanPainterSegments segments;
		const PainterBudget budget;
		segments.Add(before, PainterSegment{ 0, 2 }, budget);
		const uint32_t firstHalf = segments.Add(sheet, PainterSegment{ 0, split }, budget);
		const uint32_t secondHalf = segments.Add(sheet, PainterSegment{ split, static_cast<uint32_t>(commands.size()) - split }, budget);
		VulkanPainter& painterPass = gpu.GetPainterPass();
		painterPass.Upload(segments);

		const vk::Format format = vk::Format::eR8G8B8A8Unorm;
		const vk::Extent2D extent{ width, height };
		const PaintedImage segmented = gpu.RenderInto(format, extent, background, [&](vk::CommandBuffer commandBuffer)
			{
				painterPass.DrawSegment(commandBuffer, segments, firstHalf, format, extent);
				painterPass.DrawSegment(commandBuffer, segments, secondHalf, format, extent);
			});
		SaveForLooking(segmented, "PainterTestSheetSegments.png");

		// Blending the halves one after the other rounds in between, so allow the tolerance.
		size_t differentPixels = 0;
		for (uint32_t y = 0; y < height; ++y)
		{
			for (uint32_t x = 0; x < width; ++x)
			{
				const glm::vec4 difference = glm::abs(canvas.At(x, y) - segmented.At(x, y));
				if (std::max({ difference.r, difference.g, difference.b, difference.a }) > Tolerance)
				{
					++differentPixels;
				}
			}
		}
		Require(differentPixels == 0, "The test sheet drawn as two segments should match the canvas, " + std::to_string(differentPixels) + " pixels differ");
		Require(segments.GetProblems().empty(), "The test sheet's segments should bin without problems");
	}

	// Segments drawn as one run (stage 3, change 8): another painter's segment and the test sheet
	// in two halves, copied into one painter with Painter::AppendSegment() and drawn once, paint
	// what the three segments paint drawn one after the other.
	void TestRunOfSegmentsMatchesItsSegments(GpuPainter& gpu, const ImGuiFonts& fonts)
	{
		const glm::vec4 background{ 0.1f, 0.1f, 0.12f, 1.0f };
		const uint32_t width = static_cast<uint32_t>(PainterTestSheetSize.x);
		const uint32_t height = static_cast<uint32_t>(PainterTestSheetSize.y);

		Painter sheet = StartFrame(width, height);
		DrawPainterTestSheet(sheet, fonts.Font, fonts.Font->OwnerAtlas->TexRef, gpu.CreateTexture(MakePainterTestGradient()));
		sheet.EndFrame();
		gpu.UploadFontAtlas(*fonts.Font->OwnerAtlas);

		// A group before the sheet's, so that the run renumbers groups
		Painter before = StartFrame(width, height);
		PainterStyle glowing = PainterStyle::Filled(White);
		glowing.Effects.Glow.Color = Orange;
		glowing.Effects.Glow.Distance = 4.0f;
		before.AddCircle({ 12, 12 }, 6.0f, glowing);

		const std::vector<PainterCommand>& commands = sheet.GetCommands();
		uint32_t split = static_cast<uint32_t>(commands.size() / 2);
		while (split < commands.size() && commands[split].Group != PainterNoGroup && commands[split].Group == commands[split - 1].Group)
		{
			++split;
		}
		const PainterSegment firstHalf{ 0, split };
		const PainterSegment secondHalf{ split, static_cast<uint32_t>(commands.size()) - split };

		Painter run = StartFrame(width, height);
		Require(run.AppendSegment(before, PainterSegment{ 0, 1 }) && run.AppendSegment(sheet, firstHalf) && run.AppendSegment(sheet, secondHalf),
			"The three segments should fit into one painter");

		VulkanPainterSegments segments;
		const PainterBudget budget;
		const uint32_t beforeSegment = segments.Add(before, PainterSegment{ 0, 1 }, budget);
		const uint32_t firstSegment = segments.Add(sheet, firstHalf, budget);
		const uint32_t secondSegment = segments.Add(sheet, secondHalf, budget);
		const uint32_t runSegment = segments.Add(run, PainterSegment{ 0, static_cast<uint32_t>(run.GetCommands().size()) }, budget);
		VulkanPainter& painterPass = gpu.GetPainterPass();
		painterPass.Upload(segments);

		const vk::Format format = vk::Format::eR8G8B8A8Unorm;
		const vk::Extent2D extent{ width, height };
		const PaintedImage separately = gpu.RenderInto(format, extent, background, [&](vk::CommandBuffer commandBuffer)
			{
				painterPass.DrawSegment(commandBuffer, segments, beforeSegment, format, extent);
				painterPass.DrawSegment(commandBuffer, segments, firstSegment, format, extent);
				painterPass.DrawSegment(commandBuffer, segments, secondSegment, format, extent);
			});
		const PaintedImage asRun = gpu.RenderInto(format, extent, background, [&](vk::CommandBuffer commandBuffer)
			{
				painterPass.DrawSegment(commandBuffer, segments, runSegment, format, extent);
			});
		SaveForLooking(asRun, "SegmentRun.png");

		// The segments drawn one after the other round in between, so allow the tolerance.
		size_t differentPixels = 0;
		for (uint32_t y = 0; y < height; ++y)
		{
			for (uint32_t x = 0; x < width; ++x)
			{
				const glm::vec4 difference = glm::abs(separately.At(x, y) - asRun.At(x, y));
				if (std::max({ difference.r, difference.g, difference.b, difference.a }) > Tolerance)
				{
					++differentPixels;
				}
			}
		}
		Require(differentPixels == 0, "A run should paint what its segments paint, " + std::to_string(differentPixels) + " pixels differ");
		Require(segments.GetProblems().empty(), "The run should bin without problems");
	}

	// The blur chain (VulkanBlur)

	// An RGBA image `width` x `height` with `color(x, y)`, 0 to 255 per channel
	template<typename TColor>
	std::vector<uint8_t> MakePixels(uint32_t width, uint32_t height, TColor&& color)
	{
		std::vector<uint8_t> pixels;
		for (uint32_t y = 0; y < height; ++y)
		{
			for (uint32_t x = 0; x < width; ++x)
			{
				const glm::vec4 value = color(x, y);
				for (int channel = 0; channel < 4; ++channel)
				{
					pixels.push_back(static_cast<uint8_t>(std::lround(value[channel] * 255.0f)));
				}
			}
		}
		return pixels;
	}

	void TestBlurKeepsAFlatImageFlat(GpuPainter& gpu)
	{
		const glm::vec4 gray{ 0.4f, 0.5f, 0.6f, 1.0f };
		gpu.CreateTexture(64, 64, MakePixels(64, 64, [&](uint32_t, uint32_t) { return gray; }));
		VulkanBlurTarget target;
		const PaintedImage blurred = gpu.Blur(gpu.GetLastTextureView(), vk::Extent2D{ 64, 64 }, target);

		Require(blurred.Width == 4 && blurred.Height == 4, "Four halvings should make 64 pixels 4");
		for (uint32_t y = 0; y < blurred.Height; ++y)
		{
			for (uint32_t x = 0; x < blurred.Width; ++x)
			{
				blurred.RequirePixel(x, y, gray, "A flat image should stay flat when blurred");
			}
		}
		target.Release();
	}

	void TestBlurSmoothsAnEdge(GpuPainter& gpu)
	{
		// White on the left half, black on the right
		gpu.CreateTexture(64, 64, MakePixels(64, 64, [](uint32_t x, uint32_t) { return x < 32 ? White : Black; }));
		VulkanBlurTarget target;
		const PaintedImage blurred = gpu.Blur(gpu.GetLastTextureView(), vk::Extent2D{ 64, 64 }, target);
		SaveForLooking(blurred, "Blur.png");

		float sum = 0.0f;
		for (uint32_t y = 0; y < blurred.Height; ++y)
		{
			for (uint32_t x = 0; x < blurred.Width; ++x)
			{
				const float value = blurred.At(x, y).r;
				sum += value;
				Require(std::abs(value - blurred.At(x, 0).r) <= Tolerance, "An edge along y should blur the same in every row");
				if (x > 0)
				{
					Require(value < blurred.At(x - 1, y).r, "Across the edge, each pixel should be darker than the one left of it");
				}
			}
		}
		Require(blurred.At(0, 0).r > 0.6f && blurred.At(3, 0).r < 0.4f, "The edge should be smoothed, not flattened");
		const float mean = sum / static_cast<float>(blurred.Width * blurred.Height);
		Require(std::abs(mean - 0.5f) <= Tolerance, "Blurring shouldn't brighten or darken the image, the mean is " + std::to_string(mean));
		target.Release();
	}

	void TestBlurFollowsTheSourceAndOptions(GpuPainter& gpu)
	{
		gpu.CreateTexture(32, 16, MakePixels(32, 16, [](uint32_t, uint32_t) { return White; }));
		VulkanBlurTarget target;
		const PaintedImage sixteenth = gpu.Blur(gpu.GetLastTextureView(), vk::Extent2D{ 32, 16 }, target);
		Require(sixteenth.Width == 2 && sixteenth.Height == 1, "A 32x16 source should blur to 2x1");

		BlurOptions twoHalvings;
		twoHalvings.Downsamples = 2;
		const PaintedImage quarter = gpu.Blur(gpu.GetLastTextureView(), vk::Extent2D{ 32, 16 }, target, twoHalvings);
		Require(quarter.Width == 8 && quarter.Height == 4, "Two halvings should make the same source 8x4");
		quarter.RequirePixel(7, 3, White, "A white image should stay white");
		target.Release();
	}

	void TestTargetFollowsThePainterSize(GpuPainter& gpu)
	{
		Painter painter = StartFrame(32, 16);
		painter.AddRect(PainterRect{ { 0, 0 }, { 32, 16 } }, PainterStyle::Filled(White));
		const PaintedImage image = gpu.Paint(painter);

		Require(image.Width == 32 && image.Height == 16, "The target should take the painter's size");
		image.RequirePixel(31, 15, White, "The last pixel of a resized target");
		Require(gpu.GetTarget().GetTiles().TilesX == 4 && gpu.GetTarget().GetTiles().TilesY == 2, "The target should keep the frame's tiles");
	}
}

int main()
{
	try
	{
		GpuPainter gpu;
		TestFilledRectAndAntiAliasing(gpu);
		TestCircleEdge(gpu);
		TestDrawingOrderAndTransparency(gpu);
		TestClipRect(gpu);
		TestOutlineAndRoundedCorners(gpu);
		TestLines(gpu);
		TestTrianglesAndSectors(gpu);
		TestShadowsAndGlow(gpu);
		TestClearColorAndHeatmap(gpu);
		TestTargetFollowsThePainterSize(gpu);

		TestImages(gpu);
		TestImageGlow(gpu);
		TestGlass(gpu);
		TestTextureNotUploadedYetIsInvisible(gpu);
		{
			const ImGuiFonts fonts;
			TestTextMatchesTheFontAtlas(gpu, fonts);
			TestTextCastsNoBoxShadow(gpu, fonts);
			TestTestSheetPaints(gpu, fonts);
			TestTestSheetAsSegmentsMatchesTheCanvas(gpu, fonts);
			TestRunOfSegmentsMatchesItsSegments(gpu, fonts);
		}
		TestSegmentsDrawInOrderWithOtherDrawing(gpu);

		TestBlurKeepsAFlatImageFlat(gpu);
		TestBlurSmoothsAnEdge(gpu);
		TestBlurFollowsTheSourceAndOptions(gpu);
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All painter GPU tests passed\n";
	return 0;
}
