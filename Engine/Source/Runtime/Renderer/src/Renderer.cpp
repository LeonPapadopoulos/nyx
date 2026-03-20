#include "NyxPCH.h"
#include "Renderer.h"
#include "VulkanRenderer.h"

namespace Nyx
{
	std::unique_ptr<IRenderer> CreateRenderer()
	{
		return std::make_unique<VulkanRenderer>();
	}
}














































///// <summary>
///// //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///// </summary>
//
//namespace Engine
//{
//
//
//} // Engine
//
///// <summary>
///// //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///// </summary>
//
//
//namespace Engine
//{
//    // Comprehensive image layout transition implementation
//    // This function demonstrates proper synchronization and layout management in Vulkan
//    void TransitionImageLayout(vk::raii::CommandBuffer& commandBuffer,
//        vk::Image image,
//        vk::Format format,
//        vk::ImageLayout oldLayout,
//        vk::ImageLayout newLayout)
//    {
//
//        // Configure the basic image memory barrier structure
//        // This barrier will coordinate memory access and layout transitions
//        vk::ImageMemoryBarrier barrier;
//        barrier.setOldLayout(oldLayout)                                        // Current image layout state
//            .setNewLayout(newLayout)                                        // Target layout after transition
//            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)                // No queue family ownership transfer
//            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)                // Same queue family throughout
//            .setImage(image)                                                // Target image for the transition
//            .setSubresourceRange({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }); // Full color image range
//
//
//        // Initialize pipeline stage tracking for synchronization timing
//        // These stages define when operations must complete and when new operations can begin
//        vk::PipelineStageFlags sourceStage;      // When previous operations must finish
//        vk::PipelineStageFlags destinationStage; // When subsequent operations can start
//
//        // Configure synchronization for undefined-to-transfer layout transitions
//        // This pattern is common when preparing images for data uploads
//        if (oldLayout == vk::ImageLayout::eUndefined &&
//            newLayout == vk::ImageLayout::eTransferDstOptimal) {
//
//            // Configure memory access permissions for upload preparation
//            barrier.setSrcAccessMask(vk::AccessFlagBits::eNone)                // No previous access to synchronize
//                .setDstAccessMask(vk::AccessFlagBits::eTransferWrite);      // Enable transfer write operations
//
//            // Set pipeline stage synchronization points for upload workflow
//            sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;               // No previous work to wait for
//            destinationStage = vk::PipelineStageFlagBits::eTransfer;           // Transfer operations can proceed
//
//            // Configure synchronization for transfer-to-shader layout transitions
//            // This pattern prepares uploaded images for shader sampling
//        }
//        else if (oldLayout == vk::ImageLayout::eTransferDstOptimal &&
//            newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
//
//            // Configure memory access transition from writing to reading
//            barrier.setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)       // Previous transfer writes must complete
//                .setDstAccessMask(vk::AccessFlagBits::eShaderRead);         // Enable shader read access
//
//            // Set pipeline stage synchronization for shader usage workflow
//            sourceStage = vk::PipelineStageFlagBits::eTransfer;                // Transfer operations must complete
//            destinationStage = vk::PipelineStageFlagBits::eFragmentShader;     // Fragment shaders can access
//
//        }
//        else {
//            // Handle unsupported transition combinations
//            // Production code would include additional common transition patterns
//            throw std::invalid_argument("Unsupported layout transition!");
//        }
//
//        // Execute the pipeline barrier with configured synchronization
//        // This commits the layout transition and memory synchronization to the command buffer
//        commandBuffer.pipelineBarrier(
//            sourceStage,                                                       // Wait for these operations to complete
//            destinationStage,                                                  // Before allowing these operations to begin
//            vk::DependencyFlagBits::eByRegion,                                 // Enable region-local optimizations
//            0, nullptr,                                                        // No memory barriers needed
//            0, nullptr,                                                        // No buffer barriers needed
//            1, &barrier                                                        // Apply our image memory barrier
//        );
//
//    }
//
//    // Comprehensive frame rendering with proper synchronization
//    // This function demonstrates the complete cycle of frame rendering coordination
//    void RenderFrame(vk::raii::Device& device, vk::Queue graphicsQueue, vk::Queue presentQueue) {
//
//        // Synchronize with previous frame completion
//        // Prevent CPU from submitting work faster than GPU can process it
//        vk::Result result = device.waitForFences(1, &*inFlightFence, VK_TRUE, UINT64_MAX);
//
//        // Reset fence for this frame's completion tracking
//        // Prepare the fence to signal when this frame's GPU work completes
//        device.resetFences(1, &*inFlightFence);
//    
//        // Acquire next available image from the swapchain
//        // This operation coordinates with the presentation engine and display system
//        uint32_t imageIndex;
//        result = device.acquireNextImageKHR(*swapchain,                        // Target swapchain for acquisition
//            UINT64_MAX,                         // Wait indefinitely for image availability
//            *imageAvailableSemaphore,          // Semaphore signaled when image is available
//            nullptr,                           // No fence needed for this operation
//            &imageIndex);                      // Receives index of acquired image
//
//        // Record command buffer for this frame's rendering
//        // Command buffer recording happens here with acquired image as render target
//        // ... (command recording implementation would go here)
//
//        // Configure GPU work submission with comprehensive synchronization
//        // This submission coordinates image availability, rendering, and presentation readiness
//        vk::SubmitInfo submitInfo;
//        vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eColorAttachmentOutput };
//
//        submitInfo.setWaitSemaphoreCount(1)                                    // Wait for one semaphore before execution
//            .setPWaitSemaphores(&*imageAvailableSemaphore)               // Don't start until image is available
//            .setPWaitDstStageMask(waitStages)                            // Specifically wait before color output
//            .setCommandBufferCount(1)                                    // Submit one command buffer
//            .setPCommandBuffers(&*commandBuffer)                        // The recorded rendering commands
//            .setSignalSemaphoreCount(1)                                  // Signal one semaphore when complete
//            .setPSignalSemaphores(&*renderFinishedSemaphore);            // Notify when rendering is finished
//
//        // Submit work to GPU with fence-based completion tracking
//        // The fence allows CPU to know when this frame's GPU work has completed
//        graphicsQueue.submit(1, &submitInfo, *inFlightFence);
//        
//        // Present the rendered image to the display
//        // This operation transfers the completed frame from rendering to display system
//        vk::PresentInfoKHR presentInfo;
//        presentInfo.setWaitSemaphoreCount(1)                                   // Wait for rendering completion
//            .setPWaitSemaphores(&*renderFinishedSemaphore)              // Don't present until rendering finishes
//            .setSwapchainCount(1)                                       // Present to one swapchain
//            .setPSwapchains(&*swapchain)                                // Target swapchain for presentation
//            .setPImageIndices(&imageIndex);                             // Present the image we rendered to
//
//        // Submit presentation request to the presentation engine
//        result = presentQueue.presentKHR(&presentInfo);
//
//    }
//
//    // Comprehensive deferred renderer setup demonstrating rendergraph resource management
//    // This implementation shows how to efficiently organize multi-pass rendering workflows
//    void SetupDeferredRenderer(Rendergraph& graph, uint32_t width, uint32_t height)
//    {
//
//        // Configure position buffer for world-space vertex positions
//        // High precision format preserves positional accuracy for lighting calculations
//        graph.AddResource("GBuffer_Position", vk::Format::eR16G16B16A16Sfloat, { width, height },
//            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eInputAttachment,
//            vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal);
//
//        // Configure normal buffer for surface orientation data
//        // High precision normals enable accurate lighting and reflection calculations
//        graph.AddResource("GBuffer_Normal", vk::Format::eR16G16B16A16Sfloat, { width, height },
//            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eInputAttachment,
//            vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal);
//
//        // Configure albedo buffer for surface color information
//        // Standard 8-bit precision sufficient for color data with gamma encoding
//        graph.AddResource("GBuffer_Albedo", vk::Format::eR8G8B8A8Unorm, { width, height },
//            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eInputAttachment,
//            vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal);
//
//        // Configure depth buffer for occlusion and depth testing
//        // High precision depth enables accurate depth reconstruction in lighting pass
//        graph.AddResource("Depth", vk::Format::eD32Sfloat, { width, height },
//            vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eInputAttachment,
//            vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal);
//
//        // Configure final color buffer for the completed lighting result
//        // Standard color format with transfer capability for presentation or post-processing
//        graph.AddResource("FinalColor", vk::Format::eR8G8B8A8Unorm, { width, height },
//            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc,
//            vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferSrcOptimal);
//
//
//        /////////////////
//
//
//        // Configure geometry pass for G-Buffer population
//        // This pass renders all geometry and stores intermediate data for lighting calculations
//        graph.AddPass("GeometryPass",
//            {},                                                        // No input dependencies - first pass in pipeline
//            { "GBuffer_Position", "GBuffer_Normal", "GBuffer_Albedo", "Depth" },  // Outputs all G-Buffer components
//            [&](vk::raii::CommandBuffer& cmd) {
//
//                // Configure multiple render target attachments for G-Buffer output
//                // Each attachment corresponds to a different geometric property
//                std::array<vk::RenderingAttachmentInfoKHR, 3> colorAttachments;
//
//                // Configure position attachment - world space vertex positions
//                colorAttachments[0].setImageView(/* GBuffer_Position view */)    // Target position buffer
//                    .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)  // Optimal for writes
//                    .setLoadOp(vk::AttachmentLoadOp::eClear)       // Clear to known state
//                    .setStoreOp(vk::AttachmentStoreOp::eStore);    // Preserve for lighting pass
//
//                // Configure normal attachment - surface normals in world space
//                colorAttachments[1].setImageView(/* GBuffer_Normal view */)      // Target normal buffer
//                    .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
//                    .setLoadOp(vk::AttachmentLoadOp::eClear)       // Clear to default normal
//                    .setStoreOp(vk::AttachmentStoreOp::eStore);    // Preserve for lighting
//
//                // Configure albedo attachment - surface color and material properties
//                colorAttachments[2].setImageView(/* GBuffer_Albedo view */)      // Target albedo buffer
//                    .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
//                    .setLoadOp(vk::AttachmentLoadOp::eClear)       // Clear to default color
//                    .setStoreOp(vk::AttachmentStoreOp::eStore);    // Preserve for lighting
//
//                // Configure depth attachment for occlusion culling
//                vk::RenderingAttachmentInfoKHR depthAttachment;
//                depthAttachment.setImageView(/* Depth view */)                   // Target depth buffer
//                    .setImageLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal)  // Optimal for depth ops
//                    .setLoadOp(vk::AttachmentLoadOp::eClear)           // Clear to far plane
//                    .setStoreOp(vk::AttachmentStoreOp::eStore)         // Preserve for lighting pass
//                    .setClearValue({ 1.0f, 0 });                        // Clear to maximum depth
//
//                // Assemble complete rendering configuration
//                vk::RenderingInfoKHR renderingInfo;
//                renderingInfo.setRenderArea({ {0, 0}, {width, height} })          // Full screen rendering
//                    .setLayerCount(1)                                   // Single layer rendering
//                    .setColorAttachmentCount(colorAttachments.size())  // Number of G-Buffer targets
//                    .setPColorAttachments(colorAttachments.data())     // G-Buffer attachment array
//                    .setPDepthAttachment(&depthAttachment);            // Depth testing configuration
//
//                // Execute geometry rendering with dynamic rendering
//                cmd.beginRendering(renderingInfo);                              // Begin G-Buffer population
//
//                // Bind geometry pipeline and render all scene objects
//                // Each draw call populates position, normal, and albedo for visible fragments
//                // ... (geometry rendering implementation would go here)
//
//                cmd.endRendering();                                             // Complete G-Buffer population
//            });
//
//        // Configure lighting pass for screen-space illumination calculations
//        // This pass reads G-Buffer data and computes final lighting for each pixel
//        graph.AddPass("LightingPass",
//            { "GBuffer_Position", "GBuffer_Normal", "GBuffer_Albedo", "Depth" },  // Read all G-Buffer components
//            { "FinalColor" },                                            // Output final lit result
//            [&](vk::raii::CommandBuffer& cmd) {
//
//                // Configure single color output for final lighting result
//                vk::RenderingAttachmentInfoKHR colorAttachment;
//                colorAttachment.setImageView(/* FinalColor view */)             // Target final color buffer
//                    .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)  // Optimal for color writes
//                    .setLoadOp(vk::AttachmentLoadOp::eClear)          // Clear to background color
//                    .setStoreOp(vk::AttachmentStoreOp::eStore)        // Preserve final result
//                    .setClearValue({ 0.0f, 0.0f, 0.0f, 1.0f });        // Clear to black background
//
//                // Configure lighting pass rendering without depth testing
//                // Depth testing unnecessary since we're processing each pixel exactly once
//                vk::RenderingInfoKHR renderingInfo;
//                renderingInfo.setRenderArea({ {0, 0}, {width, height} })          // Full screen processing
//                    .setLayerCount(1)                                   // Single layer output
//                    .setColorAttachmentCount(1)                        // Single color output
//                    .setPColorAttachments(&colorAttachment);           // Final color attachment
//
//                // Execute screen-space lighting calculations
//                cmd.beginRendering(renderingInfo);                              // Begin lighting pass
//
//                // Bind lighting pipeline and draw full-screen quad
//                // Fragment shader reads G-Buffer textures and computes lighting for each pixel
//                // All scene lights are processed in a single screen-space pass
//                // ... (lighting calculation implementation would go here)
//
//                cmd.endRendering();                                             // Complete lighting calculations
//            });
//
//        // Compile the complete rendergraph for execution
//        // This analyzes dependencies and generates optimal execution plan
//        graph.Compile();
//
//    }
//
//} // Engine