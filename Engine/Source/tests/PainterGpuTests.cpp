// The painter on a real GPU: paints small frames with VulkanPainter, reads the pixels back and
// checks them. Needs a Vulkan device; the window it creates for that stays hidden.
#include "Painter.h"
#include "VulkanContext.h"
#include "VulkanPainter.h"

#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
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
		}

		~GpuPainter()
		{
			Context.GetDevice().waitIdle();
			Target.Release();
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

			PaintedImage image;
			image.Width = Target.GetExtent().width;
			image.Height = Target.GetExtent().height;
			const vk::DeviceSize byteCount = static_cast<vk::DeviceSize>(image.Width) * image.Height * 4;

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

			vk::BufferImageCopy copy{};
			copy.imageSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
			copy.imageExtent = vk::Extent3D(image.Width, image.Height, 1);
			commandBuffer.copyImageToBuffer(Target.GetImage(), vk::ImageLayout::eTransferSrcOptimal, *readback, copy);

			commandBuffer.end();
			vk::SubmitInfo submitInfo{};
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &*commandBuffer;
			Context.GetGraphicsQueue().submit(submitInfo);
			Context.GetGraphicsQueue().waitIdle();

			image.Bytes.resize(static_cast<size_t>(byteCount));
			void* mapped = readbackMemory.mapMemory(0, byteCount);
			std::memcpy(image.Bytes.data(), mapped, image.Bytes.size());
			readbackMemory.unmapMemory();
			return image;
		}

		const VulkanPainterTarget& GetTarget() const { return Target; }

	private:
		GLFWwindow* Window = nullptr;
		Nyx::VulkanContext Context;
		VulkanPainter PainterPass;
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
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All painter GPU tests passed\n";
	return 0;
}
