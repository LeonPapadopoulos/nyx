//#pragma once
//
//#include <vulkan/vulkan_raii.hpp>
//#include "ResourceManager.h"
//
///// <summary>
///// //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///// </summary>
//
//namespace Engine
//{
//
//    // Forward declarations
//    class RenderPass;
//    class RenderTarget;
//
//    // Render pass manager
//    class RenderPassManager {
//    private:
//        std::unordered_map<std::string, std::unique_ptr<RenderPass>> renderPasses;
//        std::vector<RenderPass*> sortedPasses;
//        bool dirty = true;
//
//    public:
//        template<typename T, typename... Args>
//        T* AddRenderPass(const std::string& name, Args&&... args) {
//            static_assert(std::is_base_of<RenderPass, T>::value, "T must derive from RenderPass");
//
//            auto it = renderPasses.find(name);
//            if (it != renderPasses.end()) {
//                return dynamic_cast<T*>(it->second.get());
//            }
//
//            auto pass = std::make_unique<T>(std::forward<Args>(args)...);
//            T* passPtr = pass.get();
//            renderPasses[name] = std::move(pass);
//            dirty = true;
//
//            return passPtr;
//        }
//
//        RenderPass* GetRenderPass(const std::string& name) {
//            auto it = renderPasses.find(name);
//            if (it != renderPasses.end()) {
//                return it->second.get();
//            }
//            return nullptr;
//        }
//
//        void RemoveRenderPass(const std::string& name) {
//            auto it = renderPasses.find(name);
//            if (it != renderPasses.end()) {
//                renderPasses.erase(it);
//                dirty = true;
//            }
//        }
//
//        void Execute(vk::raii::CommandBuffer& commandBuffer) {
//            if (dirty) {
//                SortPasses();
//                dirty = false;
//            }
//
//            for (auto pass : sortedPasses) {
//                pass->Execute(commandBuffer);
//            }
//        }
//
//    private:
//        void SortPasses() {
//            // Topologically sort render passes based on dependencies
//            sortedPasses.clear();
//
//            // Create a copy of render passes for sorting
//            std::unordered_map<std::string, RenderPass*> passMap;
//            for (const auto& [name, pass] : renderPasses) {
//                passMap[name] = pass.get();
//            }
//
//            // Perform topological sort
//            std::unordered_set<std::string> visited;
//            std::unordered_set<std::string> visiting;
//
//            for (const auto& [name, pass] : passMap) {
//                if (visited.find(name) == visited.end()) {
//                    TopologicalSort(name, passMap, visited, visiting);
//                }
//            }
//        }
//
//        void TopologicalSort(const std::string& name,
//            const std::unordered_map<std::string, RenderPass*>& passMap,
//            std::unordered_set<std::string>& visited,
//            std::unordered_set<std::string>& visiting) {
//            visiting.insert(name);
//
//            auto pass = passMap.at(name);
//            for (const auto& dep : pass->GetDependencies()) {
//                if (visited.find(dep) == visited.end()) {
//                    if (visiting.find(dep) != visiting.end()) {
//                        // Circular dependency detected
//                        throw std::runtime_error("Circular dependency detected in render passes");
//                    }
//                    TopologicalSort(dep, passMap, visited, visiting);
//                }
//            }
//
//            visiting.erase(name);
//            visited.insert(name);
//            sortedPasses.push_back(pass);
//        }
//    };
//
//    // Base render pass class
//    class RenderPass {
//    private:
//        std::string name;
//        std::vector<std::string> dependencies;
//        RenderTarget* target = nullptr;
//        bool enabled = true;
//
//    public:
//        explicit RenderPass(const std::string& passName) : name(passName) {}
//        virtual ~RenderPass() = default;
//
//        const std::string& GetName() const { return name; }
//
//        void AddDependency(const std::string& dependency) {
//            dependencies.push_back(dependency);
//        }
//
//        const std::vector<std::string>& GetDependencies() const {
//            return dependencies;
//        }
//
//        void SetRenderTarget(RenderTarget* renderTarget) {
//            target = renderTarget;
//        }
//
//        RenderTarget* GetRenderTarget() const {
//            return target;
//        }
//
//        void SetEnabled(bool isEnabled) {
//            enabled = isEnabled;
//        }
//
//        bool IsEnabled() const {
//            return enabled;
//        }
//
//        virtual void Execute(vk::raii::CommandBuffer& commandBuffer) {
//            if (!enabled) return;
//
//            BeginPass(commandBuffer);
//            Render(commandBuffer);
//            EndPass(commandBuffer);
//        }
//
//    protected:
//        // With dynamic rendering, BeginPass typically calls vkCmdBeginRendering
//        // instead of vkCmdBeginRenderPass
//        virtual void BeginPass(vk::raii::CommandBuffer& commandBuffer) = 0;
//        virtual void Render(vk::raii::CommandBuffer& commandBuffer) = 0;
//        // With dynamic rendering, EndPass typically calls vkCmdEndRendering
//        // instead of vkCmdEndRenderPass
//        virtual void EndPass(vk::raii::CommandBuffer& commandBuffer) = 0;
//    };
//
//    // Render target class
//    class RenderTarget {
//    private:
//        vk::raii::Image colorImage = nullptr;
//        vk::raii::DeviceMemory colorMemory = nullptr;
//        vk::raii::ImageView colorImageView = nullptr;
//
//        vk::raii::Image depthImage = nullptr;
//        vk::raii::DeviceMemory depthMemory = nullptr;
//        vk::raii::ImageView depthImageView = nullptr;
//
//        uint32_t width;
//        uint32_t height;
//
//    public:
//        RenderTarget(uint32_t w, uint32_t h) : width(w), height(h) {
//            // Create color and depth images
//            CreateColorResources();
//            CreateDepthResources();
//
//            // Note: With dynamic rendering, we don't need to create VkRenderPass
//            // or VkFramebuffer objects. Instead, we just create the images and
//            // image views that will be used directly with vkCmdBeginRendering.
//        }
//
//        // No need for explicit destructor with RAII objects
//
//        vk::ImageView GetColorImageView() const { return *colorImageView; }
//        vk::ImageView GetDepthImageView() const { return *depthImageView; }
//
//        uint32_t GetWidth() const { return width; }
//        uint32_t GetHeight() const { return height; }
//
//    private:
//        void CreateColorResources() {
//            // Implementation to create color image, memory, and view
//            // With dynamic rendering, we just need to create the image and image view
//            // that will be used with vkCmdBeginRendering
//            // ...
//        }
//
//        void CreateDepthResources() {
//            // Implementation to create depth image, memory, and view
//            // With dynamic rendering, we just need to create the image and image view
//            // that will be used with vkCmdBeginRendering
//            // ...
//        }
//
//        vk::raii::Device& GetDevice() {
//            // Get device from somewhere (e.g., singleton or parameter)
//            // ...
//            static vk::raii::Device device = nullptr; // Placeholder
//            return device;
//        }
//    };
//
//} // Engine
//
///// <summary>
///// //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///// </summary>
//
//namespace Engine
//{
//    // Geometry pass for deferred rendering
//    class GeometryPass : public RenderPass {
//    private:
//        CullingSystem* cullingSystem;
//
//        // G-buffer textures
//        RenderTarget* gBuffer;
//
//    public:
//        GeometryPass(const std::string& name, CullingSystem* culling)
//            : RenderPass(name), cullingSystem(culling) {
//            // Create G-buffer render target
//            gBuffer = new RenderTarget(1920, 1080); // Example resolution
//            SetRenderTarget(gBuffer);
//        }
//
//        ~GeometryPass() override {
//            delete gBuffer;
//        }
//
//    protected:
//        void BeginPass(vk::raii::CommandBuffer& commandBuffer) override {
//            // Begin rendering with dynamic rendering
//            vk::RenderingInfoKHR renderingInfo;
//
//            // Set up color attachment
//            vk::RenderingAttachmentInfoKHR colorAttachment;
//            colorAttachment.setImageView(gBuffer->GetColorImageView())
//                .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
//                .setLoadOp(vk::AttachmentLoadOp::eClear)
//                .setStoreOp(vk::AttachmentStoreOp::eStore)
//                .setClearValue(vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}));
//
//            // Set up depth attachment
//            vk::RenderingAttachmentInfoKHR depthAttachment;
//            depthAttachment.setImageView(gBuffer->GetDepthImageView())
//                .setImageLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal)
//                .setLoadOp(vk::AttachmentLoadOp::eClear)
//                .setStoreOp(vk::AttachmentStoreOp::eStore)
//                .setClearValue(vk::ClearDepthStencilValue(1.0f, 0));
//
//            // Configure rendering info
//            renderingInfo.setRenderArea(vk::Rect2D({ 0, 0 }, { gBuffer->GetWidth(), gBuffer->GetHeight() }))
//                .setLayerCount(1)
//                .setColorAttachmentCount(1)
//                .setPColorAttachments(&colorAttachment)
//                .setPDepthAttachment(&depthAttachment);
//
//            // Begin dynamic rendering
//            commandBuffer.beginRendering(renderingInfo);
//        }
//
//        void Render(vk::raii::CommandBuffer& commandBuffer) override {
//            // Get visible entities
//            const auto& visibleEntities = cullingSystem->GetVisibleEntities();
//
//            // Render each entity to G-buffer
//            for (auto entity : visibleEntities) {
//                auto meshComponent = entity->GetComponent<MeshComponent>();
//                auto transformComponent = entity->GetComponent<TransformComponent>();
//
//                if (meshComponent && transformComponent) {
//                    // Bind pipeline for G-buffer rendering
//                    // ...
//
//                    // Set model matrix
//                    // ...
//
//                    // Draw mesh
//                    // ...
//                }
//            }
//        }
//
//        void EndPass(vk::raii::CommandBuffer& commandBuffer) override {
//            // End dynamic rendering
//            commandBuffer.endRendering();
//        }
//    };
//
//    // Lighting pass for deferred rendering
//    class LightingPass : public RenderPass {
//    private:
//        GeometryPass* geometryPass;
//        std::vector<Light*> lights;
//
//    public:
//        LightingPass(const std::string& name, GeometryPass* gPass)
//            : RenderPass(name), geometryPass(gPass) {
//            // Add dependency on geometry pass
//            AddDependency(gPass->GetName());
//        }
//
//        void AddLight(Light* light) {
//            lights.push_back(light);
//        }
//
//        void RemoveLight(Light* light) {
//            auto it = std::find(lights.begin(), lights.end(), light);
//            if (it != lights.end()) {
//                lights.erase(it);
//            }
//        }
//
//    protected:
//        void BeginPass(vk::raii::CommandBuffer& commandBuffer) override {
//            // Begin rendering with dynamic rendering
//            vk::RenderingInfoKHR renderingInfo;
//
//            // Set up color attachment for the lighting pass
//            vk::RenderingAttachmentInfoKHR colorAttachment;
//            colorAttachment.setImageView(GetRenderTarget()->GetColorImageView())
//                .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
//                .setLoadOp(vk::AttachmentLoadOp::eClear)
//                .setStoreOp(vk::AttachmentStoreOp::eStore)
//                .setClearValue(vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}));
//
//            // Configure rendering info
//            renderingInfo.setRenderArea(vk::Rect2D({ 0, 0 }, { GetRenderTarget()->GetWidth(), GetRenderTarget()->GetHeight() }))
//                .setLayerCount(1)
//                .setColorAttachmentCount(1)
//                .setPColorAttachments(&colorAttachment);
//
//            // Begin dynamic rendering
//            commandBuffer.beginRendering(renderingInfo);
//        }
//
//        void Render(vk::raii::CommandBuffer& commandBuffer) override {
//            // Bind G-buffer textures from the geometry pass
//            auto gBuffer = geometryPass->GetRenderTarget();
//
//            // Set up descriptor sets for G-buffer textures
//            // With dynamic rendering, we access the G-buffer textures directly as shader resources
//            // rather than as subpass inputs
//
//            // Render full-screen quad with lighting shader
//            // ...
//
//            // For each light
//            for (auto light : lights) {
//                // Set light properties
//                // ...
//
//                // Draw light volume
//                // ...
//            }
//        }
//
//        void EndPass(vk::raii::CommandBuffer& commandBuffer) override {
//            // End dynamic rendering
//            commandBuffer.endRendering();
//        }
//    };
//
//    // Post-process effect base class
//    class PostProcessEffect {
//    public:
//        virtual ~PostProcessEffect() = default;
//        virtual void Apply(vk::raii::CommandBuffer& commandBuffer) = 0;
//    };
//
//    // Post-processing pass
//    class PostProcessPass : public RenderPass {
//    private:
//        LightingPass* lightingPass;
//        std::vector<PostProcessEffect*> effects;
//
//    public:
//        PostProcessPass(const std::string& name, LightingPass* lPass)
//            : RenderPass(name), lightingPass(lPass) {
//            // Add dependency on lighting pass
//            AddDependency(lPass->GetName());
//        }
//
//        void AddEffect(PostProcessEffect* effect) {
//            effects.push_back(effect);
//        }
//
//        void RemoveEffect(PostProcessEffect* effect) {
//            auto it = std::find(effects.begin(), effects.end(), effect);
//            if (it != effects.end()) {
//                effects.erase(it);
//            }
//        }
//
//    protected:
//        void BeginPass(vk::raii::CommandBuffer& commandBuffer) override {
//            // Begin rendering with dynamic rendering
//            vk::RenderingInfoKHR renderingInfo;
//
//            // Set up color attachment for the post-processing pass
//            vk::RenderingAttachmentInfoKHR colorAttachment;
//            colorAttachment.setImageView(GetRenderTarget()->GetColorImageView())
//                .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
//                .setLoadOp(vk::AttachmentLoadOp::eClear)
//                .setStoreOp(vk::AttachmentStoreOp::eStore)
//                .setClearValue(vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}));
//
//            // Configure rendering info
//            renderingInfo.setRenderArea(vk::Rect2D({ 0, 0 }, { GetRenderTarget()->GetWidth(), GetRenderTarget()->GetHeight() }))
//                .setLayerCount(1)
//                .setColorAttachmentCount(1)
//                .setPColorAttachments(&colorAttachment);
//
//            // Begin dynamic rendering
//            commandBuffer.beginRendering(renderingInfo);
//        }
//
//        void Render(vk::raii::CommandBuffer& commandBuffer) override {
//            // With dynamic rendering, each effect can set up its own rendering state
//            // and access input textures directly as shader resources
//
//            // Apply each post-process effect
//            for (auto effect : effects) {
//                effect->Apply(commandBuffer);
//            }
//        }
//
//        void EndPass(vk::raii::CommandBuffer& commandBuffer) override {
//            // End dynamic rendering
//            commandBuffer.endRendering();
//        }
//    };
//
//} // Engine
//
//
///// <summary>
///// //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///// </summary>
//
//
//namespace Engine
//{
//    class Renderer {
//    private:
//        vk::raii::Device device = nullptr;
//        vk::Queue graphicsQueue;
//        vk::raii::CommandPool commandPool = nullptr;
//
//        RenderPassManager renderPassManager;
//        CullingSystem cullingSystem;
//
//        // Current frame resources
//        vk::raii::CommandBuffer commandBuffer = nullptr;
//        vk::raii::Fence fence = nullptr;
//        vk::raii::Semaphore imageAvailableSemaphore = nullptr;
//        vk::raii::Semaphore renderFinishedSemaphore = nullptr;
//
//    public:
//        Renderer(vk::raii::Device& dev, vk::Queue queue) : device(dev), graphicsQueue(queue) {
//            // Create command pool
//            // ...
//
//            // Create synchronization objects
//            // ...
//
//            // Set up render passes
//            SetupRenderPasses();
//        }
//
//        // No need for explicit destructor with RAII objects
//
//        void SetCamera(Camera* camera) {
//            cullingSystem.SetCamera(camera);
//        }
//
//        void Render(const std::vector<Entity*>& entities) {
//            // Wait for previous frame to finish
//            fence.wait(UINT64_MAX);
//            fence.reset();
//
//            // Reset command buffer
//            commandBuffer.reset();
//
//            // Perform culling
//            cullingSystem.CullScene(entities);
//
//            // Record commands
//            vk::CommandBufferBeginInfo beginInfo;
//            commandBuffer.begin(beginInfo);
//
//            // Execute render passes
//            renderPassManager.Execute(commandBuffer);
//
//            commandBuffer.end();
//
//            // Submit command buffer
//            vk::SubmitInfo submitInfo;
//
//            // With vk::raii, we need to dereference the command buffer
//            vk::CommandBuffer rawCommandBuffer = *commandBuffer;
//            submitInfo.setCommandBufferCount(1);
//            submitInfo.setPCommandBuffers(&rawCommandBuffer);
//
//            // Set up wait and signal semaphores
//            vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eColorAttachmentOutput };
//
//            // With vk::raii, we need to dereference the semaphores
//            vk::Semaphore rawImageAvailableSemaphore = *imageAvailableSemaphore;
//            vk::Semaphore rawRenderFinishedSemaphore = *renderFinishedSemaphore;
//
//            submitInfo.setWaitSemaphoreCount(1);
//            submitInfo.setPWaitSemaphores(&rawImageAvailableSemaphore);
//            submitInfo.setPWaitDstStageMask(waitStages);
//            submitInfo.setSignalSemaphoreCount(1);
//            submitInfo.setPSignalSemaphores(&rawRenderFinishedSemaphore);
//
//            // With vk::raii, we need to dereference the fence
//            vk::Fence rawFence = *fence;
//            graphicsQueue.submit(1, &submitInfo, rawFence);
//        }
//
//    private:
//        // @todo: LP Is this mapping of Geometry->cullingsystem etc really correct !?
//        void SetupRenderPasses() {
//            // Create geometry pass
//            auto geometryPass = renderPassManager.AddRenderPass<GeometryPass>("GeometryPass", &cullingSystem);
//
//            // Create lighting pass
//            auto lightingPass = renderPassManager.AddRenderPass<LightingPass>("LightingPass", geometryPass);
//
//            // Create post-process pass
//            auto postProcessPass = renderPassManager.AddRenderPass<PostProcessPass>("PostProcessPass", lightingPass);
//
//            // Add post-process effects
//            // ...
//        }
//    };
//
//
//} // Engine
//
//
///// <summary>
///// //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///// </summary>
//
//namespace Engine
//{
//
//    // A comprehensive rendergraph implementation for automated dependency management
//    class Rendergraph {
//    private:
//        // Resource description and management structure
//        // Represents Image resource used during rendering (textures)
//        struct ImageResource {
//            std::string name;                     // Human-readable identifier for debugging and referencing
//            vk::Format format;                    // Pixel format (RGBA8, Depth24Stencil8, etc.)
//            vk::Extent2D extent;                  // Dimensions in pixels for 2D resources
//            vk::ImageUsageFlags usage;            // How this resource will be used (color attachment, texture, etc.)
//            vk::ImageLayout initialLayout;        // Expected layout when the frame begins
//            vk::ImageLayout finalLayout;          // Required layout when the frame ends
//
//            // Actual GPU resources - populated during compilation
//            vk::raii::Image image = nullptr;      // The GPU image object
//            vk::raii::DeviceMemory memory = nullptr;  // Backing memory allocation
//            vk::raii::ImageView view = nullptr;   // Shader-accessible view of the image
//        };
//
//        // Render pass representation within the graph structure
//        // Each pass represents a distinct rendering operation with defined inputs and outputs
//        struct Pass {
//            std::string name;                     // Descriptive name for debugging and profiling
//            std::vector<std::string> inputs;      // Resources this pass reads from (dependencies)
//            std::vector<std::string> outputs;     // Resources this pass writes to (products)
//            std::function<void(vk::raii::CommandBuffer&)> executeFunc;  // The actual rendering code
//        };
//
//        // Core data storage for the rendergraph system
//        std::unordered_map<std::string, Resource> resources;  // All resources referenced in the graph
//        std::vector<Pass> passes;                             // All rendering passes in definition order
//        std::vector<size_t> executionOrder;                   // Computed optimal execution sequence
//
//        // Automatic synchronization management
//        // These objects ensure correct GPU execution order without manual barriers
//        std::vector<vk::raii::Semaphore> semaphores;          // GPU synchronization primitives
//        std::vector<std::pair<size_t, size_t>> semaphoreSignalWaitPairs;  // (signaling pass, waiting pass)
//
//        vk::raii::Device& device;  // Vulkan device for resource creation
//
//    public:
//        explicit Rendergraph(vk::raii::Device& dev) : device(dev) {}
//
//        // Resource registration interface for declaring all resources used during rendering
//        // This method establishes resource metadata without creating actual GPU resources
//        void AddResource(const std::string& name, vk::Format format, vk::Extent2D extent,
//            vk::ImageUsageFlags usage, vk::ImageLayout initialLayout,
//            vk::ImageLayout finalLayout) {
//            Resource resource;
//            resource.name = name;                    // Store human-readable identifier
//            resource.format = format;                // Define pixel format and bit depth
//            resource.extent = extent;                // Set resource dimensions
//            resource.usage = usage;                  // Specify intended usage patterns
//            resource.initialLayout = initialLayout; // Define starting layout state
//            resource.finalLayout = finalLayout;     // Define required ending state
//
//            resources[name] = resource;              // Register in the global resource map
//        }
//
//        // Pass registration interface for defining rendering operations and their dependencies
//        // This method establishes the logical structure of rendering without immediate execution
//        void AddPass(const std::string& name,
//            const std::vector<std::string>& inputs,
//            const std::vector<std::string>& outputs,
//            std::function<void(vk::raii::CommandBuffer&)> executeFunc) {
//            Pass pass;
//            pass.name = name;                        // Assign descriptive identifier
//            pass.inputs = inputs;                    // List all resources this pass reads
//            pass.outputs = outputs;                  // List all resources this pass writes
//            pass.executeFunc = executeFunc;          // Store the actual rendering implementation
//
//            passes.push_back(pass);                  // Add to the ordered pass list
//        }
//
//        // Rendergraph compilation - transforms declarative descriptions into executable pipeline
//        // This method performs dependency analysis, resource allocation, and execution planning
//        void Compile() {
//            // Dependency Graph Construction
//            // Build bidirectional dependency relationships between passes
//            std::vector<std::vector<size_t>> dependencies(passes.size());  // What each pass depends on
//            std::vector<std::vector<size_t>> dependents(passes.size());    // What depends on each pass
//
//            // Track which pass produces each resource (write-after-write dependencies)
//            std::unordered_map<std::string, size_t> resourceWriters;
//
//            // Dependency Discovery Through Resource Usage Analysis
//            // Analyze each pass to determine data flow relationships
//            for (size_t i = 0; i < passes.size(); ++i) {
//                const auto& pass = passes[i];
//
//                // Process input dependencies - this pass must wait for producers
//                for (const auto& input : pass.inputs) {
//                    auto it = resourceWriters.find(input);
//                    if (it != resourceWriters.end()) {
//                        // Found the pass that produces this input - create dependency link
//                        dependencies[i].push_back(it->second);      // This pass depends on the producer
//                        dependents[it->second].push_back(i);        // Producer has this as dependent
//                    }
//                }
//
//                // Register output production - subsequent passes may depend on these
//                for (const auto& output : pass.outputs) {
//                    resourceWriters[output] = i;                    // Record this pass as producer
//                }
//            }
//
//            // Topological Sort for Optimal Execution Order
//            // Use depth-first search to compute valid execution sequence while detecting cycles
//            std::vector<bool> visited(passes.size(), false);       // Track completed nodes
//            std::vector<bool> inStack(passes.size(), false);       // Track current recursion path
//
//            std::function<void(size_t)> visit = [&](size_t node) {
//                if (inStack[node]) {
//                    // Cycle detection - circular dependency found
//                    throw std::runtime_error("Cycle detected in rendergraph");
//                }
//
//                if (visited[node]) {
//                    return;  // Already processed this node and its dependencies
//                }
//
//                inStack[node] = true;   // Mark as currently being processed
//
//                // Recursively process all dependent passes first (post-order traversal)
//                for (auto dependent : dependents[node]) {
//                    visit(dependent);
//                }
//
//                inStack[node] = false;  // Remove from current path
//                visited[node] = true;   // Mark as completely processed
//                executionOrder.push_back(node);  // Add to execution sequence
//                };
//
//            // Process all unvisited nodes to handle disconnected graph components
//            for (size_t i = 0; i < passes.size(); ++i) {
//                if (!visited[i]) {
//                    visit(i);
//                }
//            }
//
//
//            // Automatic Synchronization Object Creation
//// Generate semaphores for all dependencies identified during analysis
//            for (size_t i = 0; i < passes.size(); ++i) {
//                for (auto dep : dependencies[i]) {
//                    // Create a GPU semaphore for this dependency relationship
//                    // The dependent pass will wait on this semaphore before executing
//                    semaphores.emplace_back(device.createSemaphore({}));
//                    semaphoreSignalWaitPairs.emplace_back(dep, i);    // (producer, consumer) pair
//                }
//            }
//
//            // Physical Resource Allocation and Creation
//            // Transform resource descriptions into actual GPU objects
//            for (auto& [name, resource] : resources) {
//                // Configure image creation parameters based on resource description
//                vk::ImageCreateInfo imageInfo;
//                imageInfo.setImageType(vk::ImageType::e2D)                    // 2D texture/render target
//                    .setFormat(resource.format)                          // Pixel format from description
//                    .setExtent({ resource.extent.width, resource.extent.height, 1 })  // Dimensions
//                    .setMipLevels(1)                                      // Single mip level for simplicity
//                    .setArrayLayers(1)                                    // Single layer (not array texture)
//                    .setSamples(vk::SampleCountFlagBits::e1)              // No multisampling
//                    .setTiling(vk::ImageTiling::eOptimal)                 // GPU-optimal memory layout
//                    .setUsage(resource.usage)                             // Usage flags from registration
//                    .setSharingMode(vk::SharingMode::eExclusive)          // Single queue family access
//                    .setInitialLayout(vk::ImageLayout::eUndefined);       // Initial layout (will be transitioned)
//
//                resource.image = device.createImage(imageInfo);               // Create the GPU image object
//
//                // Allocate backing memory for the image
//                vk::MemoryRequirements memRequirements = resource.image.getMemoryRequirements();
//
//                vk::MemoryAllocateInfo allocInfo;
//                allocInfo.setAllocationSize(memRequirements.size)             // Required memory size
//                    .setMemoryTypeIndex(FindMemoryType(memRequirements.memoryTypeBits,
//                        vk::MemoryPropertyFlagBits::eDeviceLocal));  // GPU-local memory
//
//                resource.memory = device.allocateMemory(allocInfo);           // Allocate GPU memory
//                resource.image.bindMemory(*resource.memory, 0);               // Bind memory to image
//
//                // Create image view for shader access
//                vk::ImageViewCreateInfo viewInfo;
//                viewInfo.setImage(*resource.image)                            // Reference the created image
//                    .setViewType(vk::ImageViewType::e2D)                   // 2D view type
//                    .setFormat(resource.format)                            // Match image format
//                    .setSubresourceRange({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 });  // Full image access
//
//                resource.view = device.createImageView(viewInfo);             // Create shader-accessible view
//            }
//        }
//
//        // Resource access interface for retrieving compiled resources
//        Resource* GetResource(const std::string& name) 
//        {
//            auto it = resources.find(name);
//            return (it != resources.end()) ? &it->second : nullptr;
//        }
//
//        // Rendergraph execution engine - coordinates pass execution with automatic synchronization
//        // This method transforms the compiled rendergraph into actual GPU work
//        void Execute(vk::raii::CommandBuffer& commandBuffer, vk::Queue queue) {
//            // Execution state management for dynamic synchronization
//            std::vector<vk::CommandBuffer> cmdBuffers;           // Command buffer storage
//            std::vector<vk::Semaphore> waitSemaphores;           // Synchronization dependencies for current pass
//            std::vector<vk::PipelineStageFlags> waitStages;      // Pipeline stages to wait on
//            std::vector<vk::Semaphore> signalSemaphores;         // Semaphores to signal after current pass
//
//            // Ordered Pass Execution with Automatic Dependency Management
//            // Execute each pass in the computed dependency-safe order
//            for (auto passIdx : executionOrder) {
//                const auto& pass = passes[passIdx];
//
//                // Synchronization Setup - Collect Dependencies for Current Pass
//                // Determine what this pass must wait for before executing
//                waitSemaphores.clear();
//                waitStages.clear();
//
//                for (size_t i = 0; i < semaphoreSignalWaitPairs.size(); ++i) {
//                    if (semaphoreSignalWaitPairs[i].second == passIdx) {
//                        // This pass depends on the completion of another pass
//                        waitSemaphores.push_back(*semaphores[i]);                           // Wait for dependency completion
//                        waitStages.push_back(vk::PipelineStageFlagBits::eColorAttachmentOutput);  // Wait at output stage
//                    }
//                }
//
//                // Collect semaphores that this pass will signal for dependent passes
//                signalSemaphores.clear();
//                for (size_t i = 0; i < semaphoreSignalWaitPairs.size(); ++i) {
//                    if (semaphoreSignalWaitPairs[i].first == passIdx) {
//                        // Other passes depend on this pass's completion
//                        signalSemaphores.push_back(*semaphores[i]);                         // Signal completion for dependents
//                    }
//                }
//
//                // Command Buffer Preparation and Resource Layout Transitions
//                // Set up command recording and transition resources to appropriate layouts
//                commandBuffer.begin({});                                                   // Begin command recording
//
//                // Transition input resources to shader-readable layouts
//                for (const auto& input : pass.inputs) {
//                    auto& resource = resources[input];
//
//                    vk::ImageMemoryBarrier barrier;
//                    barrier.setOldLayout(resource.initialLayout)                           // Current resource layout
//                        .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)          // Target layout for reading
//                        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)                // No queue family transfer
//                        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
//                        .setImage(*resource.image)                                      // Target image
//                        .setSubresourceRange({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 })  // Full image range
//                        .setSrcAccessMask(vk::AccessFlagBits::eMemoryWrite)             // Previous write access
//                        .setDstAccessMask(vk::AccessFlagBits::eShaderRead);             // Required read access
//
//                    // Insert pipeline barrier for safe layout transition
//                    commandBuffer.pipelineBarrier(
//                        vk::PipelineStageFlagBits::eAllCommands,                           // Wait for all previous work
//                        vk::PipelineStageFlagBits::eFragmentShader,                        // Enable fragment shader access
//                        vk::DependencyFlagBits::eByRegion,                                 // Region-local dependency
//                        0, nullptr, 0, nullptr, 1, &barrier                               // Image barrier only
//                    );
//                }
//
//                // Transition output resources to render target layouts
//                for (const auto& output : pass.outputs) {
//                    auto& resource = resources[output];
//
//                    vk::ImageMemoryBarrier barrier;
//                    barrier.setOldLayout(resource.initialLayout)                           // Current layout state
//                        .setNewLayout(vk::ImageLayout::eColorAttachmentOptimal)         // Optimal for color output
//                        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
//                        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
//                        .setImage(*resource.image)
//                        .setSubresourceRange({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 })
//                        .setSrcAccessMask(vk::AccessFlagBits::eMemoryRead)              // Previous read access
//                        .setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite);   // Required write access
//
//                    // Insert barrier for safe transition to writable state
//                    commandBuffer.pipelineBarrier(
//                        vk::PipelineStageFlagBits::eAllCommands,
//                        vk::PipelineStageFlagBits::eColorAttachmentOutput,                 // Enable color attachment writes
//                        vk::DependencyFlagBits::eByRegion,
//                        0, nullptr, 0, nullptr, 1, &barrier
//                    );
//                }
//
//                // Pass Execution - Execute the Actual Rendering Logic
//                // Call the user-provided rendering function with prepared command buffer
//                pass.executeFunc(commandBuffer);                                           // Execute pass-specific rendering
//
//                // Final Layout Transitions - Prepare Resources for Subsequent Use
//                // Transition output resources to their final required layouts
//                for (const auto& output : pass.outputs) {
//                    auto& resource = resources[output];
//
//                    vk::ImageMemoryBarrier barrier;
//                    barrier.setOldLayout(vk::ImageLayout::eColorAttachmentOptimal)         // Current writable layout
//                        .setNewLayout(resource.finalLayout)                             // Required final layout
//                        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
//                        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
//                        .setImage(*resource.image)
//                        .setSubresourceRange({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 })
//                        .setSrcAccessMask(vk::AccessFlagBits::eColorAttachmentWrite)    // Previous write operations
//                        .setDstAccessMask(vk::AccessFlagBits::eMemoryRead);             // Enable subsequent reads
//
//                    // Insert final barrier for layout transition
//                    commandBuffer.pipelineBarrier(
//                        vk::PipelineStageFlagBits::eColorAttachmentOutput,                 // After color writes complete
//                        vk::PipelineStageFlagBits::eAllCommands,                           // Before any subsequent work
//                        vk::DependencyFlagBits::eByRegion,
//                        0, nullptr, 0, nullptr, 1, &barrier
//                    );
//                }
//
//                // Command Submission with Synchronization
//                // Submit command buffer with appropriate dependency and signaling semaphores
//                commandBuffer.end();                                                       // Finalize command recording
//
//                vk::SubmitInfo submitInfo;
//                submitInfo.setWaitSemaphoreCount(static_cast<uint32_t>(waitSemaphores.size()))      // Dependencies to wait for
//                    .setPWaitSemaphores(waitSemaphores.data())                                 // Dependency semaphores
//                    .setPWaitDstStageMask(waitStages.data())                                   // Pipeline stages to wait at
//                    .setCommandBufferCount(1)                                                  // Single command buffer
//                    .setPCommandBuffers(&*commandBuffer)                                      // Command buffer to execute
//                    .setSignalSemaphoreCount(static_cast<uint32_t>(signalSemaphores.size()))  // Semaphores to signal
//                    .setPSignalSemaphores(signalSemaphores.data());                           // Signal semaphores
//
//                queue.submit(1, &submitInfo, nullptr);                                              // Submit to GPU queue
//            }
//        }
//
//    private:
//        uint32_t FindMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) {
//            // Implementation to find suitable memory type
//            // ...
//            return 0; // Placeholder
//        }
//
//    };
//}// Engine