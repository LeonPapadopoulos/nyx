#include "NyxPCH.h"
#include "Assertions.h"
#include "VulkanRenderer.h"
#include "VulkanImGuiBackend.h"
#include "VulkanContext.h"
#include "VulkanSwapchain.h"
#include "Log.h"
#include "GltfImporter.h"
#include "TransformComponent.h"
#include "MeshRendererComponent.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <ranges>
#include <string>
#include <fstream>

#include "backends/imgui_impl_vulkan.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

#include <stb_image.h>

namespace
{
	bool LoadImageDataRGBA(const std::filesystem::path& path, Nyx::ImageData& outImageData)
	{
		outImageData = {};

		int width = 0;
		int height = 0;
		int channels = 0;

		stbi_uc* pixels = stbi_load(path.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
		if (!pixels)
		{
			return false;
		}

		const size_t imageSize = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;

		outImageData.Width = width;
		outImageData.Height = height;
		outImageData.Channels = 4;
		outImageData.Pixels.resize(imageSize);
		std::memcpy(outImageData.Pixels.data(), pixels, imageSize);

		stbi_image_free(pixels);
		return true;
	}
}

std::filesystem::file_time_type TryGetLastWriteTime(
	const std::filesystem::path& path,
	bool& bSuccess)
{
	std::error_code ec;
	const auto time = std::filesystem::last_write_time(path, ec);
	bSuccess = !ec;
	return time;
}

bool TryGetFileWriteTime(
	const std::filesystem::path& path,
	std::filesystem::file_time_type& outTime)
{
	std::error_code ec;
	const auto time = std::filesystem::last_write_time(path, ec);
	if (ec)
	{
		return false;
	}

	outTime = time;
	return true;
}

namespace Nyx
{
	VulkanRenderer::VulkanRenderer()
	{
	}

	void VulkanRenderer::Initialize(const char* applicationName, GLFWwindow* window)
	{
		ASSERT(window != nullptr);
		Window = window;

		SetupVulkan(applicationName, window);
		ASSERT(Swapchain.GetMinImageCount() >= 2 && "Failed to fulfill VulkanImGuiBackend requirements.");
		ASSERT(Swapchain.GetImageCount() >= Swapchain.GetMinImageCount() && "Failed to fulfill VulkanImGuiBackend requirements.");
		SetupImGui();

		// ------------------------------------------------------------------------------------------------------
		// Shader hot reload Setup
		// ------------------------------------------------------------------------------------------------------
		std::filesystem::create_directories(Nyx::Paths::GetExecutableDir() / "Shaders");
		{
			GridShaderHotReload.VertSourcePath = Nyx::Paths::GetShadersDir() / "Grid.vert";
			GridShaderHotReload.FragSourcePath = Nyx::Paths::GetShadersDir() / "Grid.frag";

			// Compile output goes to the same runtime-relative location CreateGridPipeline() already reads from
			GridShaderHotReload.VertSpvPath = Nyx::Paths::GetExecutableDir() / "Shaders" / "Grid.vert.spv";
			GridShaderHotReload.FragSpvPath = Nyx::Paths::GetExecutableDir() / "Shaders" / "Grid.frag.spv";

			LOG_INFO("Grid vert source: {}", GridShaderHotReload.VertSourcePath.string());
			LOG_INFO("Grid frag source: {}", GridShaderHotReload.FragSourcePath.string());
			LOG_INFO("Grid vert spv: {}", GridShaderHotReload.VertSpvPath.string());
			LOG_INFO("Grid frag spv: {}", GridShaderHotReload.FragSpvPath.string());

			bool bVertOk = TryGetFileWriteTime(GridShaderHotReload.VertSourcePath, GridShaderHotReload.LastVertWriteTime);
			bool bFragOk = TryGetFileWriteTime(GridShaderHotReload.FragSourcePath, GridShaderHotReload.LastFragWriteTime);

			ASSERT(bVertOk && "Failed to stat Grid.vert");
			ASSERT(bFragOk && "Failed to stat Grid.frag");
		}

		// ------------------------------------------------------------------------------------------------------
		// CPU / GPU resources needed by rendering
		// ------------------------------------------------------------------------------------------------------
		CreateTestTextureData();
		CreateTestMeshData();
		CreateTestMeshBuffers();

		// @todo: Move World and population of it out of the renderer!
		{
			auto e0 = World.CreateEntity();
			World.Add<Nyx::Engine::TransformComponent>(e0,
				Nyx::Engine::TransformComponent{
					.Position = glm::vec3(-2.0f, 0.0f, 0.0f)
				}
			);
			World.Add<Nyx::Engine::MeshRendererComponent>(e0,
				Nyx::Engine::MeshRendererComponent{
					.MeshAsset = &CubeMesh,
					.MaterialAsset = &TexturedMaterial
				}
			);

			auto e1 = World.CreateEntity();
			World.Add<Nyx::Engine::TransformComponent>(e1,
				Nyx::Engine::TransformComponent{
					.Position = glm::vec3(0.0f, 0.0f, 0.0f)
				}
			);
			World.Add<Nyx::Engine::MeshRendererComponent>(e1,
				Nyx::Engine::MeshRendererComponent{
					.MeshAsset = &CubeMesh,
					.MaterialAsset = &ReflectiveMaterial
				}
			);

			auto e2 = World.CreateEntity();
			World.Add<Nyx::Engine::TransformComponent>(e2,
				Nyx::Engine::TransformComponent{
					.Position = glm::vec3(2.0f, 0.0f, 0.0f)
				}
			);
			World.Add<Nyx::Engine::MeshRendererComponent>(e2,
				Nyx::Engine::MeshRendererComponent{
					.MeshAsset = &CubeMesh,
					.MaterialAsset = &UntexturedMaterial
				}
			);
		}

		// Render pass / framebuffer target must exist before pipelines
		SceneViewport.Initialize(Context, 1280, 720, vk::Format::eR8G8B8A8Unorm);
		
		// Global scene resources
		CreateSceneUniformBuffer();
	
		// With Skybox reflections being a thing;
		// Skybox cubemap needs to be loaded before updating scene descriptor sets
		CreateSkyboxResources();
		
		// Descriptor(Sets) depend on SceneUniformBuffer, TestTexture & SkyboxCubemap
		CreateSceneDescriptors();
		UpdateSceneDescriptorSets();

		// Pipelines depend on SceneViewport & DescriptorSetLayouts
		CreateScenePipeline();
		CreateGridPipeline();

		// Materials depend on ScenePipeline, ScenePipelineLayout & SceneDescriptorSets
		CreateMaterials();

		// RenderObjects depend on Meshes & Materials
		CreateRenderObjects();

		// @note: Gltf Meshes currently not visible, because the RenderObjects get 
		// cleared and then populated by the world / entity-registry; So manually pushed
		// RenderObjects, as is currently the case with this GltfScene setup, won't be
		// rendered.
		// @todo: Figure out to what extend Gltf scenes should be supported from hereon out.
		//LoadTestGltfScene();
	}

	void VulkanRenderer::Shutdown()
	{
		if (*Context.GetDevice())
		{
			Context.GetDevice().waitIdle();
		}

		SceneViewport.Shutdown(Context);

		if (ImGuiBackend)
		{
			ImGuiBackend.reset();
		}

		Swapchain.Shutdown();

		// destroy command buffers / semaphores / fences / pools
		CommandBuffers.clear();
		RenderFinishedSemaphores.clear();
		ImageAvailableSemaphore = nullptr;
		InFlightFence = nullptr;
		CommandPool = nullptr;

		MeshIndexBuffer = nullptr;
		MeshIndexBufferMemory = nullptr;
		MeshVertexBuffer = nullptr;
		MeshVertexBufferMemory = nullptr;

		Context.Shutdown();
	}

	void VulkanRenderer::BeginFrame()
	{
	}
	
	void VulkanRenderer::EndFrame()
	{
	}

	void VulkanRenderer::OnFramebufferResized()
	{
		bRecreateSwapChain = true;
	}

	void VulkanRenderer::SetupVulkan(const char* applicationName, GLFWwindow* window)
	{
		Context.Initialize(applicationName, window);
		Swapchain.Initialize(Context, window);

		CreateGraphicsPipeline();

		// @todo: check order
		CreateCommandPool();
		CreateCommandBuffers();
		CreateSyncObjects();
	}

	void VulkanRenderer::SetupImGui()
	{
		ImGuiBackend = std::make_unique<VulkanImGuiBackend>(Window, *this);
		ImGuiBackend->Initialize(
			static_cast<float>(Swapchain.GetExtent().width),
			static_cast<float>(Swapchain.GetExtent().height));
	}

	void VulkanRenderer::DrawFrame(const std::function<void()>& buildUI)
	{
		// 1) Wait until previous submitted frame is done
		vk::Result result = Context.GetDevice().waitForFences({ *InFlightFence }, true, UINT64_MAX);

		// 2) Acquire next swapchain image
		const vk::ResultValue<uint32_t> acquireResult =
			Swapchain.GetSwapchain().acquireNextImage(UINT64_MAX, *ImageAvailableSemaphore);

		if (acquireResult.result == vk::Result::eErrorOutOfDateKHR)
		{
			RecreateSwapChain();
			return;
		}

		if (acquireResult.result != vk::Result::eSuccess &&
			acquireResult.result != vk::Result::eSuboptimalKHR)
		{
			throw std::runtime_error("Failed to acquire swapchain image.");
		}

		const uint32_t imageIndex = acquireResult.value;

		// Only reset once we know we will submit work
		Context.GetDevice().resetFences({ *InFlightFence });

		PollGridShaderHotReload();
		if (GridShaderHotReload.bReloadPending)
		{
			GridPipeline = nullptr;
			CreateGridPipeline();
			GridShaderHotReload.bReloadPending = false;
		}

		// 3) Record command buffer for this image
		vk::raii::CommandBuffer& cmd = CommandBuffers[imageIndex];
		cmd.reset();

		vk::CommandBufferBeginInfo beginInfo{};
		cmd.begin(beginInfo);

		// Render Scene
		{
			if (bSceneViewportResizePending)
			{
				SceneViewport.Recreate(Context, PendingSceneViewportWidth, PendingSceneViewportHeight, vk::Format::eR8G8B8A8Unorm);
				GridPipeline = nullptr;
				GridPipelineLayout = nullptr;
				CreateGridPipeline();
				ScenePipeline = nullptr;
				ScenePipelineLayout = nullptr;
				CreateScenePipeline();
				SkyboxPipeline = nullptr;
				SkyboxPipelineLayout = nullptr;
				CreateSkyboxPipeline();
								
				bSceneViewportResizePending = false;
				bSceneViewportRecreatedThisFrame = true;
			}
			else
			{
				bSceneViewportRecreatedThisFrame = false;
			}

			vk::ClearColorValue clear = vk::ClearColorValue(std::array<float, 4>{ 0.2f, 0.05f, 0.35f, 1.0f });

			SceneViewport.BeginRenderPass(cmd, clear);
			{
				// @todo: Introduce a more proper 'Tick'
				const float deltaTime = ComputeDeltaTime();

				// @todo: Move animation-like behavior out of the renderer
				{
					World.Each<Nyx::Engine::TransformComponent>(
						[this, deltaTime](Nyx::Engine::Entity entity, Nyx::Engine::TransformComponent& transform)
						{
							transform.RotationRadians.y += deltaTime;
						}
					);
				}

				TickCameraFromInput(deltaTime);
				UpdateSceneUniforms(deltaTime);
				// @todo: Get rid of functionality relating to the previously hardcoded test meshes
				//UpdateRenderObjects(deltaTime);
				UpdateSkyboxUniforms(deltaTime);

				// 1. Opaque scene objects
				{
					//cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *ScenePipeline);
					//cmd.bindDescriptorSets(
					//	vk::PipelineBindPoint::eGraphics,
					//	*ScenePipelineLayout,
					//	0,
					//	{ *SceneDescriptorSets.front() },
					//	{}
					//);

					//// Draw Cube Mesh
					//vk::DeviceSize offsets[] = { 0 };
					//cmd.bindVertexBuffers(0, { CubeMesh.GetVertexBuffer() }, offsets);
					//cmd.bindIndexBuffer(CubeMesh.GetIndexBuffer(), 0, vk::IndexType::eUint32);
					//cmd.drawIndexed(CubeMesh.GetIndexCount(), 1, 0, 0, 0);
					
					ExtractRenderObjects(World);
					DrawRenderObjects(cmd);
				}

				// 2. Skybox
				{
					DrawSkybox(cmd);
				}

				// 3. Grid
				{
					// Draw Grid after opaque geometry, so depth test can occlude it

					cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *GridPipeline);
					cmd.bindDescriptorSets(
						vk::PipelineBindPoint::eGraphics,
						*GridPipelineLayout,
						0,
						{ *SceneDescriptorSets.front() },
						{}
					);
					cmd.draw(3, 1, 0, 0);
				}
			}
			SceneViewport.EndRenderPass(cmd);
		}

		vk::ClearValue clearValue{};
		clearValue.color = vk::ClearColorValue(std::array<float, 4>{ 0.1f, 0.1f, 0.1f, 1.0f });

		vk::RenderPassBeginInfo renderPassInfo{};
		renderPassInfo.renderPass = *Swapchain.GetRenderPass();
		renderPassInfo.framebuffer = *Swapchain.GetFramebuffers()[imageIndex];
		renderPassInfo.renderArea.offset = vk::Offset2D{ 0, 0 };
		renderPassInfo.renderArea.extent = Swapchain.GetExtent();
		renderPassInfo.clearValueCount = 1;
		renderPassInfo.pClearValues = &clearValue;

		cmd.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

		// This is where ImGuiBackend records into the active frame command buffer.
		{
			ImGuiBackend->BeginFrame();
			buildUI();
			ImGuiBackend->DrawFrame(cmd);
		}

		cmd.endRenderPass();
		cmd.end();

		// 4) Submit
		const vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;

		vk::raii::Semaphore& renderFinishedSemaphore = RenderFinishedSemaphores[imageIndex];

		vk::SubmitInfo submitInfo{};
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &*ImageAvailableSemaphore;
		submitInfo.pWaitDstStageMask = &waitStage;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &*cmd;
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &*renderFinishedSemaphore;

		Context.GetGraphicsQueue().submit(submitInfo, *InFlightFence);

		// 5) Present
		vk::PresentInfoKHR presentInfo{};
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &*renderFinishedSemaphore;
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &*Swapchain.GetSwapchain();
		presentInfo.pImageIndices = &imageIndex;

		const vk::Result presentResult = Context.GetGraphicsQueue().presentKHR(presentInfo);
		if (presentResult == vk::Result::eErrorOutOfDateKHR ||
			presentResult == vk::Result::eSuboptimalKHR ||
			bRecreateSwapChain)
		{
			RecreateSwapChain();
			return;
		}

		if (presentResult != vk::Result::eSuccess)
		{
			throw std::runtime_error("Failed to present swapchain image.");
		}
	}

	void VulkanRenderer::WaitIdle()
	{
		if (*Context.GetDevice())
		{
			Context.GetDevice().waitIdle();
		}
	}

	VulkanContext& VulkanRenderer::GetContext()
	{
		return Context;
	}

	VulkanSwapchain& VulkanRenderer::GetSwapchain()
	{
		return Swapchain;
	}

	ImTextureID VulkanRenderer::GetSceneTextureId() const
	{
		return SceneViewport.GetImGuiTextureId();
	}

	Extent2D VulkanRenderer::GetSceneViewportExtent() const
	{
		const vk::Extent2D extent = SceneViewport.GetExtent();
		return Extent2D{ extent.width, extent.height };
	}

	void VulkanRenderer::EnsureSceneViewportSize(uint32_t width, uint32_t height)
	{
		SceneViewport.EnsureSize(Context, width, height);
	}

	void VulkanRenderer::SetSceneViewportSize(uint32_t width, uint32_t height)
	{
		width = std::max(1u, width);
		height = std::max(1u, height);

		if (width == PendingSceneViewportWidth && height == PendingSceneViewportHeight)
		{
			return;
		}

		PendingSceneViewportWidth = width;
		PendingSceneViewportHeight = height;
		bSceneViewportResizePending = true;
	}

	bool VulkanRenderer::WasSceneViewportRecreatedThisFrame() const
	{
		return bSceneViewportRecreatedThisFrame;
	}

	void VulkanRenderer::TickCameraFromInput(float deltaTime)
	{
		if (!Window)
		{
			return;
		}

		const bool bWantsMouseLook =
			glfwGetMouseButton(Window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

		// Enter / leave mouse-look mode
		if (bWantsMouseLook && !bMouseLookActive)
		{
			bMouseLookActive = true;
			bFirstMouseLookSample = true;
			glfwSetInputMode(Window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		}
		else if (!bWantsMouseLook && bMouseLookActive)
		{
			bMouseLookActive = false;
			bFirstMouseLookSample = true;
			glfwSetInputMode(Window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		}

		if (bMouseLookActive)
		{
			double mouseX = 0.0;
			double mouseY = 0.0;
			glfwGetCursorPos(Window, &mouseX, &mouseY);

			if (bFirstMouseLookSample)
			{
				LastMouseX = mouseX;
				LastMouseY = mouseY;
				bFirstMouseLookSample = false;
			}
			else
			{
				const double deltaX = mouseX - LastMouseX;
				const double deltaY = mouseY - LastMouseY;

				LastMouseX = mouseX;
				LastMouseY = mouseY;

				Camera.YawRadians += static_cast<float>(deltaX) * CameraMouseSensitivity;
				Camera.PitchRadians -= static_cast<float>(deltaY) * CameraMouseSensitivity;

				constexpr float pitchLimit = glm::radians<float>(89.0f);
				Camera.PitchRadians = glm::clamp(Camera.PitchRadians, -pitchLimit, pitchLimit);
			}
		}

		const float moveSpeed = CameraMoveSpeed * deltaTime;

		const glm::vec3 forward = Camera.GetForwardVector();
		const glm::vec3 right = Camera.GetRightVector();
		const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);

		// Flatten movement to the ground plane for WASD
		glm::vec3 flatRight = glm::normalize(glm::vec3(right.x, 0.0f, right.z));

		// avoid issues when looking straight right
		if (glm::length2(flatRight) > 1e-6f)
		{
			flatRight = glm::normalize(flatRight);
		}
		else
		{
			flatRight = glm::vec3(1.0f, 0.0f, 0.0f);
		}

		if (glfwGetKey(Window, GLFW_KEY_W) == GLFW_PRESS) Camera.Position += forward * moveSpeed;
		if (glfwGetKey(Window, GLFW_KEY_S) == GLFW_PRESS) Camera.Position -= forward * moveSpeed;
		if (glfwGetKey(Window, GLFW_KEY_A) == GLFW_PRESS) Camera.Position -= flatRight * moveSpeed;
		if (glfwGetKey(Window, GLFW_KEY_D) == GLFW_PRESS) Camera.Position += flatRight * moveSpeed;

		if (glfwGetKey(Window, GLFW_KEY_Q) == GLFW_PRESS) Camera.Position -= worldUp * moveSpeed;
		if (glfwGetKey(Window, GLFW_KEY_E) == GLFW_PRESS) Camera.Position += worldUp * moveSpeed;
	}

	void VulkanRenderer::CreateGraphicsPipeline()
	{
		// @todo:
	}

	void VulkanRenderer::CreateCommandPool()
	{
		vk::CommandPoolCreateInfo poolInfo{};
		poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
		poolInfo.queueFamilyIndex = Context.GetGraphicsQueueFamily();

		CommandPool = vk::raii::CommandPool(Context.GetDevice(), poolInfo);
	}

	void VulkanRenderer::CreateCommandBuffers()
	{
		CommandBuffers.clear();

		vk::CommandBufferAllocateInfo allocInfo{};
		allocInfo.commandPool = *CommandPool;
		allocInfo.level = vk::CommandBufferLevel::ePrimary;
		allocInfo.commandBufferCount = Swapchain.GetImageCount();

		CommandBuffers = vk::raii::CommandBuffers(Context.GetDevice(), allocInfo);
	}

	void VulkanRenderer::CreateSyncObjects()
	{
		vk::SemaphoreCreateInfo semaphoreInfo{};
		ImageAvailableSemaphore = vk::raii::Semaphore(Context.GetDevice(), semaphoreInfo);

		RenderFinishedSemaphores.clear();
		RenderFinishedSemaphores.reserve(Swapchain.GetImageCount());
		for (size_t i = 0; i < Swapchain.GetImageCount(); ++i)
		{
			RenderFinishedSemaphores.emplace_back(Context.GetDevice(), semaphoreInfo);
		}

		vk::FenceCreateInfo fenceInfo{};
		fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled;
		InFlightFence = vk::raii::Fence(Context.GetDevice(), fenceInfo);
	}

	void VulkanRenderer::RecreateSwapChain()
	{
		WaitForValidFramebufferSize();
		Context.GetDevice().waitIdle();
		Swapchain.Recreate(Context, Window);

		CreateCommandBuffers();
		//ImGuiBackend.OnSwapchainChanged() // @todo:

		bRecreateSwapChain = false;
	}

	void VulkanRenderer::WaitForValidFramebufferSize()
	{
		int width = 0;
		int height = 0;

		while (width == 0 || height == 0)
		{
			glfwGetFramebufferSize(Window, &width, &height);
			glfwWaitEvents();
		}
	}

	void VulkanRenderer::CreateMaterials()
	{
		TexturedMaterial.Pipeline = &ScenePipeline;
		TexturedMaterial.PipelineLayout = &ScenePipelineLayout;
		TexturedMaterial.DescriptorSet = &SceneDescriptorSets.front();
		TexturedMaterial.Reflectivity = 0.0f;
		TexturedMaterial.bUseTexture = true;
		TexturedMaterial.Tint = glm::vec3(1.0f, 1.0f, 1.0f);

		ReflectiveMaterial.Pipeline = &ScenePipeline;
		ReflectiveMaterial.PipelineLayout = &ScenePipelineLayout;
		ReflectiveMaterial.DescriptorSet = &SceneDescriptorSets.front();
		ReflectiveMaterial.Reflectivity = 0.35f;
		ReflectiveMaterial.bUseTexture = true;
		ReflectiveMaterial.Tint = glm::vec3(1.0f, 1.0f, 1.0f);

		UntexturedMaterial.Pipeline = &ScenePipeline;
		UntexturedMaterial.PipelineLayout = &ScenePipelineLayout;
		UntexturedMaterial.DescriptorSet = &SceneDescriptorSets.front();
		UntexturedMaterial.Reflectivity = 0.05f;
		UntexturedMaterial.bUseTexture = false;
		UntexturedMaterial.Tint = glm::vec3(0.2f, 0.8f, 1.0f);
	}

	void VulkanRenderer::CreateRenderObjects()
	{
		RenderObjects.clear();

		{
			RenderObject obj{};
			obj.MeshAsset = &CubeMesh;
			obj.MaterialAsset = &TexturedMaterial;
			obj.WorldTransform = glm::translate(glm::mat4(1.0f), glm::vec3(-2.0f, 0.0f, 0.0f));
			RenderObjects.push_back(obj);
		}

		{
			RenderObject obj{};
			obj.MeshAsset = &CubeMesh;
			obj.MaterialAsset = &ReflectiveMaterial;
			obj.WorldTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f));
			RenderObjects.push_back(obj);
		}

		{
			RenderObject obj{};
			obj.MeshAsset = &CubeMesh;
			obj.MaterialAsset = &UntexturedMaterial;
			obj.WorldTransform = glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, 0.0f));
			RenderObjects.push_back(obj);
		}
	}

	void VulkanRenderer::UpdateRenderObjects(float deltaTime)
	{
		SceneTime += deltaTime;

		if (RenderObjects.size() >= 3)
		{
			RenderObjects[0].WorldTransform =
				glm::translate(glm::mat4(1.0f), glm::vec3(-2.0f, 0.0f, 0.0f)) *
				glm::rotate(glm::mat4(1.0f), SceneTime * 0.7f, glm::vec3(0.0f, 1.0f, 0.0f));

			RenderObjects[1].WorldTransform =
				glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f)) *
				glm::rotate(glm::mat4(1.0f), SceneTime * 1.1f, glm::vec3(0.0f, 1.0f, 0.0f)) *
				glm::rotate(glm::mat4(1.0f), SceneTime * 0.35f, glm::vec3(1.0f, 0.0f, 0.0f));

			RenderObjects[2].WorldTransform =
				glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, 0.0f)) *
				glm::rotate(glm::mat4(1.0f), SceneTime * 0.8f, glm::vec3(0.0f, 0.0f, 1.0f));
		}
	}

	void VulkanRenderer::ExtractRenderObjects(const Nyx::Engine::Registry& registry)
	{
		RenderObjects.clear();

		// Registry::Each<T> iterates one component pool and gives (Entity, Component&).
		// So we iterate MeshRendererComponent and then query TransformComponent for the same entity.
		const_cast<Nyx::Engine::Registry&>(registry).Each<Nyx::Engine::MeshRendererComponent>(
			[this, &registry](Nyx::Engine::Entity entity, Nyx::Engine::MeshRendererComponent& meshRenderer)
			{
				if (!meshRenderer.bVisible)
				{
					return;
				}

				if (!meshRenderer.MeshAsset || !meshRenderer.MaterialAsset)
				{
					return;
				}

				glm::mat4 worldTransform = glm::mat4(1.0f);

				if (registry.Has<Nyx::Engine::TransformComponent>(entity))
				{
					worldTransform = registry.Get<Nyx::Engine::TransformComponent>(entity).ToMatrix();
				}

				RenderObject obj{};
				obj.MeshAsset = meshRenderer.MeshAsset;
				obj.MaterialAsset = meshRenderer.MaterialAsset;
				obj.WorldTransform = worldTransform;

				RenderObjects.push_back(obj);
			}
		);
	}

	void VulkanRenderer::DrawRenderObjects(vk::raii::CommandBuffer& cmd)
	{
		for (const RenderObject& obj : RenderObjects)
		{
			if (!obj.MeshAsset || !obj.MaterialAsset)
			{
				continue;
			}

			cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **obj.MaterialAsset->Pipeline);

			cmd.bindDescriptorSets(
				vk::PipelineBindPoint::eGraphics,
				**obj.MaterialAsset->PipelineLayout,
				0,
				{ **obj.MaterialAsset->DescriptorSet },
				{}
			);

			ObjectPushConstants pushConstants{};
			pushConstants.Model = obj.WorldTransform;
			pushConstants.Params = glm::vec4(
				obj.MaterialAsset->Reflectivity,
				obj.MaterialAsset->bUseTexture ? 1.0f : 0.0f,
				0.0f,
				0.0f
			);
			pushConstants.Tint = glm::vec4(obj.MaterialAsset->Tint, 1.0f);

			cmd.pushConstants<ObjectPushConstants>(
				**obj.MaterialAsset->PipelineLayout,
				vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
				0,
				{ pushConstants }
			);

			vk::DeviceSize offsets[] = { 0 };
			cmd.bindVertexBuffers(0, { obj.MeshAsset->GetVertexBuffer() }, offsets);
			cmd.bindIndexBuffer(obj.MeshAsset->GetIndexBuffer(), 0, vk::IndexType::eUint32);
			cmd.drawIndexed(obj.MeshAsset->GetIndexCount(), 1, 0, 0, 0);
		}
	}

	void VulkanRenderer::CreateScenePipeline()
	{
		// File directory relative to the working directory of the .exe
		// Set up to be that way inside the .exe's CMake
		const std::vector<uint32_t> vertCode = ReadSpirvFile("Shaders/DefaultShader.vert.spv");
		const std::vector<uint32_t> fragCode = ReadSpirvFile("Shaders/DefaultShader.frag.spv");

		vk::raii::ShaderModule vertShaderModule = CreateShaderModule(vertCode);
		vk::raii::ShaderModule fragShaderModule = CreateShaderModule(fragCode);

		vk::PipelineShaderStageCreateInfo vertStageInfo{};
		vertStageInfo.stage = vk::ShaderStageFlagBits::eVertex;
		vertStageInfo.module = *vertShaderModule;
		vertStageInfo.pName = "main";

		vk::PipelineShaderStageCreateInfo fragStageInfo{};
		fragStageInfo.stage = vk::ShaderStageFlagBits::eFragment;
		fragStageInfo.module = *fragShaderModule;
		fragStageInfo.pName = "main";

		vk::PipelineShaderStageCreateInfo shaderStages[] = { vertStageInfo, fragStageInfo };

		const auto bindingDescription = Vertex::GetBindingDescription();
		const auto attributeDescriptions = Vertex::GetAttributeDescriptions();

		vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
		vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
		vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

		vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
		inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		vk::PipelineViewportStateCreateInfo viewportState{};
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;

		vk::PipelineRasterizationStateCreateInfo rasterizer{};
		rasterizer.depthClampEnable = VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode = vk::PolygonMode::eFill;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = vk::CullModeFlagBits::eNone; // @todo: Re-enable Backface Culling vk::CullModeFlagBits::eBack;
		rasterizer.frontFace = vk::FrontFace::eClockwise;
		rasterizer.depthBiasEnable = VK_FALSE;

		vk::PipelineMultisampleStateCreateInfo multisampling{};
		multisampling.sampleShadingEnable = VK_FALSE;
		multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

		vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
		colorBlendAttachment.blendEnable = VK_FALSE;
		colorBlendAttachment.colorWriteMask =
			vk::ColorComponentFlagBits::eR |
			vk::ColorComponentFlagBits::eG |
			vk::ColorComponentFlagBits::eB |
			vk::ColorComponentFlagBits::eA;

		vk::PipelineColorBlendStateCreateInfo colorBlending{};
		colorBlending.logicOpEnable = VK_FALSE;
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &colorBlendAttachment;

		std::array<vk::DynamicState, 2> dynamicStates =
		{
			vk::DynamicState::eViewport,
			vk::DynamicState::eScissor
		};

		vk::PipelineDynamicStateCreateInfo dynamicState{};
		dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
		dynamicState.pDynamicStates = dynamicStates.data();

		vk::PipelineDepthStencilStateCreateInfo depthStencil{};
		depthStencil.depthTestEnable = VK_TRUE;
		depthStencil.depthWriteEnable = VK_TRUE;
		depthStencil.depthCompareOp = vk::CompareOp::eLess;
		depthStencil.depthBoundsTestEnable = VK_FALSE;
		depthStencil.stencilTestEnable = VK_FALSE;

		vk::PushConstantRange pushConstantRange{};
		pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
		pushConstantRange.offset = 0;
		pushConstantRange.size = sizeof(ObjectPushConstants);

		vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
		vk::DescriptorSetLayout setLayouts[] = { *SceneDescriptorSetLayout };
		pipelineLayoutInfo.setLayoutCount = 1;
		pipelineLayoutInfo.pSetLayouts = setLayouts;
		pipelineLayoutInfo.pushConstantRangeCount = 1;
		pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

		ScenePipelineLayout = vk::raii::PipelineLayout(Context.GetDevice(), pipelineLayoutInfo);

		vk::GraphicsPipelineCreateInfo pipelineInfo{};
		pipelineInfo.stageCount = 2;
		pipelineInfo.pStages = shaderStages;
		pipelineInfo.pVertexInputState = &vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState = &multisampling;
		pipelineInfo.pDepthStencilState = &depthStencil;
		pipelineInfo.pColorBlendState = &colorBlending;
		pipelineInfo.pDynamicState = &dynamicState;
		pipelineInfo.layout = *ScenePipelineLayout;
		pipelineInfo.renderPass = *SceneViewport.GetRenderPass();
		pipelineInfo.subpass = 0;

		ScenePipeline = std::move(vk::raii::Pipeline(
			Context.GetDevice(),
			nullptr,
			pipelineInfo
		));
	}

	void VulkanRenderer::CreateGridPipeline()
	{
		const std::vector<uint32_t> vertCode = ReadSpirvFile("Shaders/Grid.vert.spv");
		const std::vector<uint32_t> fragCode = ReadSpirvFile("Shaders/Grid.frag.spv");

		vk::raii::ShaderModule vertShaderModule = CreateShaderModule(vertCode);
		vk::raii::ShaderModule fragShaderModule = CreateShaderModule(fragCode);

		vk::PipelineShaderStageCreateInfo vertStageInfo{};
		vertStageInfo.stage = vk::ShaderStageFlagBits::eVertex;
		vertStageInfo.module = *vertShaderModule;
		vertStageInfo.pName = "main";

		vk::PipelineShaderStageCreateInfo fragStageInfo{};
		fragStageInfo.stage = vk::ShaderStageFlagBits::eFragment;
		fragStageInfo.module = *fragShaderModule;
		fragStageInfo.pName = "main";

		vk::PipelineShaderStageCreateInfo shaderStages[] = { vertStageInfo, fragStageInfo };

		vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};

		vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
		inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

		vk::PipelineViewportStateCreateInfo viewportState{};
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;

		vk::PipelineRasterizationStateCreateInfo rasterizer{};
		rasterizer.polygonMode = vk::PolygonMode::eFill;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = vk::CullModeFlagBits::eNone;
		rasterizer.frontFace = vk::FrontFace::eCounterClockwise;

		vk::PipelineMultisampleStateCreateInfo multisampling{};
		multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

		vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
		colorBlendAttachment.blendEnable = VK_TRUE;

		// Add source color on top of destination, modulated by source alpha
		colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
		colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOne;
		colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;

		// Alpha channel itself does not matter much here
		colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
		colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
		colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;

		colorBlendAttachment.colorWriteMask =
			vk::ColorComponentFlagBits::eR |
			vk::ColorComponentFlagBits::eG |
			vk::ColorComponentFlagBits::eB |
			vk::ColorComponentFlagBits::eA;

		vk::PipelineColorBlendStateCreateInfo colorBlending{};
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &colorBlendAttachment;

		std::array<vk::DynamicState, 2> dynamicStates =
		{
			vk::DynamicState::eViewport,
			vk::DynamicState::eScissor
		};

		vk::PipelineDynamicStateCreateInfo dynamicState{};
		dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
		dynamicState.pDynamicStates = dynamicStates.data();

		vk::DescriptorSetLayout setLayouts[] = { *SceneDescriptorSetLayout };

		// @todo: Is the depthStencil something we want for the grid?
		// Disabled it for now because it was hiding the rendered Quad
		//vk::PipelineDepthStencilStateCreateInfo depthStencil{};
		//depthStencil.depthTestEnable = VK_TRUE;
		//depthStencil.depthWriteEnable = VK_TRUE;
		//depthStencil.depthCompareOp = vk::CompareOp::eLess;
		//depthStencil.depthBoundsTestEnable = VK_FALSE;
		//depthStencil.stencilTestEnable = VK_FALSE;

		vk::PipelineDepthStencilStateCreateInfo depthStencil{};
		depthStencil.depthTestEnable = VK_TRUE;
		depthStencil.depthWriteEnable = VK_FALSE;
		depthStencil.depthCompareOp = vk::CompareOp::eLessOrEqual;
		depthStencil.depthBoundsTestEnable = VK_FALSE;
		depthStencil.stencilTestEnable = VK_FALSE;

		vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
		pipelineLayoutInfo.setLayoutCount = 1;
		pipelineLayoutInfo.pSetLayouts = setLayouts;

		GridPipelineLayout = vk::raii::PipelineLayout(Context.GetDevice(), pipelineLayoutInfo);

		vk::GraphicsPipelineCreateInfo pipelineInfo{};
		pipelineInfo.stageCount = 2;
		pipelineInfo.pStages = shaderStages;
		pipelineInfo.pVertexInputState = &vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState = &multisampling;
		pipelineInfo.pDepthStencilState = &depthStencil;
		pipelineInfo.pColorBlendState = &colorBlending;
		pipelineInfo.pDynamicState = &dynamicState;
		pipelineInfo.layout = *GridPipelineLayout;
		pipelineInfo.renderPass = *SceneViewport.GetRenderPass();
		pipelineInfo.subpass = 0;

		GridPipeline = std::move(vk::raii::Pipeline(Context.GetDevice(), nullptr, pipelineInfo));
	}

	void VulkanRenderer::CreateSkyboxPipeline()
	{
		const std::vector<uint32_t> vertCode = ReadSpirvFile("Shaders/Skybox.vert.spv");
		const std::vector<uint32_t> fragCode = ReadSpirvFile("Shaders/Skybox.frag.spv");

		vk::raii::ShaderModule vertShaderModule = CreateShaderModule(vertCode);
		vk::raii::ShaderModule fragShaderModule = CreateShaderModule(fragCode);

		vk::PipelineShaderStageCreateInfo vertStageInfo{};
		vertStageInfo.stage = vk::ShaderStageFlagBits::eVertex;
		vertStageInfo.module = *vertShaderModule;
		vertStageInfo.pName = "main";

		vk::PipelineShaderStageCreateInfo fragStageInfo{};
		fragStageInfo.stage = vk::ShaderStageFlagBits::eFragment;
		fragStageInfo.module = *fragShaderModule;
		fragStageInfo.pName = "main";

		vk::PipelineShaderStageCreateInfo shaderStages[] = { vertStageInfo, fragStageInfo };

		// Only position is used by the skybox shader.
		vk::VertexInputBindingDescription bindingDescription{};
		bindingDescription.binding = 0;
		bindingDescription.stride = sizeof(Vertex);
		bindingDescription.inputRate = vk::VertexInputRate::eVertex;

		vk::VertexInputAttributeDescription positionAttribute{};
		positionAttribute.binding = 0;
		positionAttribute.location = 0;
		positionAttribute.format = vk::Format::eR32G32B32Sfloat;
		positionAttribute.offset = offsetof(Vertex, Position);

		vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
		vertexInputInfo.vertexAttributeDescriptionCount = 1;
		vertexInputInfo.pVertexAttributeDescriptions = &positionAttribute;

		vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
		inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		vk::PipelineViewportStateCreateInfo viewportState{};
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;

		vk::PipelineRasterizationStateCreateInfo rasterizer{};
		rasterizer.depthClampEnable = VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode = vk::PolygonMode::eFill;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = vk::CullModeFlagBits::eNone;
		rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
		rasterizer.depthBiasEnable = VK_FALSE;

		vk::PipelineMultisampleStateCreateInfo multisampling{};
		multisampling.sampleShadingEnable = VK_FALSE;
		multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

		vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
		colorBlendAttachment.blendEnable = VK_FALSE;
		colorBlendAttachment.colorWriteMask =
			vk::ColorComponentFlagBits::eR |
			vk::ColorComponentFlagBits::eG |
			vk::ColorComponentFlagBits::eB |
			vk::ColorComponentFlagBits::eA;

		vk::PipelineColorBlendStateCreateInfo colorBlending{};
		colorBlending.logicOpEnable = VK_FALSE;
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &colorBlendAttachment;

		std::array<vk::DynamicState, 2> dynamicStates =
		{
			vk::DynamicState::eViewport,
			vk::DynamicState::eScissor
		};

		vk::PipelineDynamicStateCreateInfo dynamicState{};
		dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
		dynamicState.pDynamicStates = dynamicStates.data();

		// Draw after opaque geometry.
		vk::PipelineDepthStencilStateCreateInfo depthStencil{};
		depthStencil.depthTestEnable = VK_TRUE;
		depthStencil.depthWriteEnable = VK_FALSE;
		depthStencil.depthCompareOp = vk::CompareOp::eLessOrEqual;
		depthStencil.depthBoundsTestEnable = VK_FALSE;
		depthStencil.stencilTestEnable = VK_FALSE;

		vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
		vk::DescriptorSetLayout setLayouts[] = { *SkyboxDescriptorSetLayout };
		pipelineLayoutInfo.setLayoutCount = 1;
		pipelineLayoutInfo.pSetLayouts = setLayouts;

		SkyboxPipelineLayout = vk::raii::PipelineLayout(Context.GetDevice(), pipelineLayoutInfo);

		vk::GraphicsPipelineCreateInfo pipelineInfo{};
		pipelineInfo.stageCount = 2;
		pipelineInfo.pStages = shaderStages;
		pipelineInfo.pVertexInputState = &vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState = &multisampling;
		pipelineInfo.pDepthStencilState = &depthStencil;
		pipelineInfo.pColorBlendState = &colorBlending;
		pipelineInfo.pDynamicState = &dynamicState;
		pipelineInfo.layout = *SkyboxPipelineLayout;
		pipelineInfo.renderPass = *SceneViewport.GetRenderPass();
		pipelineInfo.subpass = 0;

		SkyboxPipeline = std::move(vk::raii::Pipeline(
			Context.GetDevice(),
			nullptr,
			pipelineInfo
		));
	}

	void VulkanRenderer::CreateSkyboxResources()
	{
		SkyboxCubemap.SetContext(Context);

		const bool bLoaded = SkyboxCubemap.Load();
		ASSERT(bLoaded && "Failed to load skybox cubemap.");

		CreateSkyboxUniformBuffer();
		CreateSkyboxDescriptorSetLayout();
		CreateSkyboxDescriptorPool();
		AllocateSkyboxDescriptorSets();
		UpdateSkyboxDescriptorSet();
		CreateSkyboxPipeline();
	}

	void VulkanRenderer::CreateSkyboxUniformBuffer()
	{
		CreateBuffer(
			sizeof(SkyboxUBO),
			vk::BufferUsageFlagBits::eUniformBuffer,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
			SkyboxUniformBuffer,
			SkyboxUniformBufferMemory
		);
	}

	void VulkanRenderer::CreateSkyboxDescriptorSetLayout()
	{
		vk::DescriptorSetLayoutBinding uboBinding{};
		uboBinding.binding = 0;
		uboBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
		uboBinding.descriptorCount = 1;
		uboBinding.stageFlags = vk::ShaderStageFlagBits::eVertex;

		vk::DescriptorSetLayoutBinding cubemapBinding{};
		cubemapBinding.binding = 1;
		cubemapBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
		cubemapBinding.descriptorCount = 1;
		cubemapBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

		std::array<vk::DescriptorSetLayoutBinding, 2> bindings =
		{
			uboBinding,
			cubemapBinding
		};

		vk::DescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		layoutInfo.pBindings = bindings.data();

		SkyboxDescriptorSetLayout = vk::raii::DescriptorSetLayout(Context.GetDevice(), layoutInfo);
	}

	void VulkanRenderer::CreateSkyboxDescriptorPool()
	{
		std::array<vk::DescriptorPoolSize, 2> poolSizes{};

		poolSizes[0].type = vk::DescriptorType::eUniformBuffer;
		poolSizes[0].descriptorCount = 1;

		poolSizes[1].type = vk::DescriptorType::eCombinedImageSampler;
		poolSizes[1].descriptorCount = 1;

		vk::DescriptorPoolCreateInfo poolInfo{};
		poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
		poolInfo.maxSets = 1;
		poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		poolInfo.pPoolSizes = poolSizes.data();

		SkyboxDescriptorPool = vk::raii::DescriptorPool(Context.GetDevice(), poolInfo);
	}

	void VulkanRenderer::AllocateSkyboxDescriptorSets()
	{
		vk::DescriptorSetLayout layouts[] = { *SkyboxDescriptorSetLayout };

		vk::DescriptorSetAllocateInfo allocInfo{};
		allocInfo.descriptorPool = *SkyboxDescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = layouts;

		SkyboxDescriptorSets = vk::raii::DescriptorSets(Context.GetDevice(), allocInfo);
	}

	void VulkanRenderer::UpdateSkyboxDescriptorSet()
	{
		vk::DescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = *SkyboxUniformBuffer;
		bufferInfo.offset = 0;
		bufferInfo.range = sizeof(SkyboxUBO);

		vk::DescriptorImageInfo imageInfo{};
		imageInfo.sampler = SkyboxCubemap.GetSampler();
		imageInfo.imageView = SkyboxCubemap.GetImageView();
		imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

		std::array<vk::WriteDescriptorSet, 2> writes{};

		writes[0].dstSet = *SkyboxDescriptorSets.front();
		writes[0].dstBinding = 0;
		writes[0].dstArrayElement = 0;
		writes[0].descriptorType = vk::DescriptorType::eUniformBuffer;
		writes[0].descriptorCount = 1;
		writes[0].pBufferInfo = &bufferInfo;

		writes[1].dstSet = *SkyboxDescriptorSets.front();
		writes[1].dstBinding = 1;
		writes[1].dstArrayElement = 0;
		writes[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
		writes[1].descriptorCount = 1;
		writes[1].pImageInfo = &imageInfo;

		Context.GetDevice().updateDescriptorSets(writes, {});
	}

	void VulkanRenderer::UpdateSkyboxUniforms(float deltaTime)
	{
		SkyboxUBO ubo{};

		// Remove translation so the skybox stays centered on the camera.
		const glm::mat4 skyboxView = glm::mat4(glm::mat3(Camera.GetViewMatrix()));
		const glm::mat4 skyboxProj = Camera.GetProjectionMatrix();

		ubo.ViewProj = skyboxProj * skyboxView;

		void* mapped = SkyboxUniformBufferMemory.mapMemory(0, sizeof(SkyboxUBO));
		std::memcpy(mapped, &ubo, sizeof(SkyboxUBO));
		SkyboxUniformBufferMemory.unmapMemory();
	}

	vk::raii::ShaderModule VulkanRenderer::CreateShaderModule(const std::vector<uint32_t>& spirv)
	{
		vk::ShaderModuleCreateInfo createInfo{};
		createInfo.codeSize = spirv.size() * sizeof(uint32_t);
		createInfo.pCode = spirv.data();

		return vk::raii::ShaderModule(Context.GetDevice(), createInfo);
	}

	std::vector<uint32_t> VulkanRenderer::ReadSpirvFile(const std::string& path)
	{
		std::ifstream file(path, std::ios::ate | std::ios::binary);
		if (!file.is_open())
		{
			throw std::runtime_error("Failed to open SPIR-V file: " + path);
		}

		const std::streamsize fileSize = file.tellg();
		if (fileSize <= 0 || (fileSize % sizeof(uint32_t)) != 0)
		{
			throw std::runtime_error("Invalid SPIR-V file size: " + path);
		}

		std::vector<uint32_t> buffer(static_cast<size_t>(fileSize) / sizeof(uint32_t));

		file.seekg(0);
		file.read(reinterpret_cast<char*>(buffer.data()), fileSize);

		if (!file)
		{
			throw std::runtime_error("Failed to read SPIR-V file: " + path);
		}

		return buffer;
	}

	void VulkanRenderer::CreateSceneDescriptors()
	{
		vk::DescriptorSetLayoutBinding uboBinding{};
		uboBinding.binding = 0;
		uboBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
		uboBinding.descriptorCount = 1;
		uboBinding.stageFlags = vk::ShaderStageFlagBits::eVertex | 
			                          vk::ShaderStageFlagBits::eFragment;
		uboBinding.pImmutableSamplers = nullptr;

		vk::DescriptorSetLayoutBinding textureBinding{};
		textureBinding.binding = 1;
		textureBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
		textureBinding.descriptorCount = 1;
		textureBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

		vk::DescriptorSetLayoutBinding cubemapBinding{};
		cubemapBinding.binding = 2;
		cubemapBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
		cubemapBinding.descriptorCount = 1;
		cubemapBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

		std::array<vk::DescriptorSetLayoutBinding, 3> bindings =
		{
			uboBinding,
			textureBinding,
			cubemapBinding
		};

		vk::DescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		layoutInfo.pBindings = bindings.data();

		SceneDescriptorSetLayout = vk::raii::DescriptorSetLayout(Context.GetDevice(), layoutInfo);

		vk::DescriptorPoolSize poolSizes[3]{};

		poolSizes[0].type = vk::DescriptorType::eUniformBuffer;
		poolSizes[0].descriptorCount = 1; // @todo: FrameCount;

		poolSizes[1].type = vk::DescriptorType::eCombinedImageSampler;
		poolSizes[1].descriptorCount = 1; // @todo: FrameCount;

		poolSizes[2].type = vk::DescriptorType::eCombinedImageSampler;
		poolSizes[2].descriptorCount = 1; // @todo: FrameCount;

		vk::DescriptorPoolCreateInfo poolInfo{};
		poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
		poolInfo.maxSets = 1; // @todo: FrameCount;
		poolInfo.poolSizeCount = poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
		poolInfo.pPoolSizes = poolSizes;

		SceneDescriptorPool = vk::raii::DescriptorPool(Context.GetDevice(), poolInfo);

		vk::DescriptorSetAllocateInfo allocInfo{};
		allocInfo.descriptorPool = *SceneDescriptorPool;
		allocInfo.descriptorSetCount = 1;
		vk::DescriptorSetLayout layout = *SceneDescriptorSetLayout;
		allocInfo.pSetLayouts = &layout;

		SceneDescriptorSets = vk::raii::DescriptorSets(Context.GetDevice(), allocInfo);

		vk::DescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = *SceneUniformBuffer;
		bufferInfo.offset = 0;
		bufferInfo.range = sizeof(SceneUBO);

		vk::DescriptorImageInfo imageInfo{};
		imageInfo.sampler = TestTexture.GetSampler();
		imageInfo.imageView = TestTexture.GetImageView();
		imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

		vk::WriteDescriptorSet writes[2]{};

		writes[0].dstSet = *SceneDescriptorSets[0]; // @todo: i];
		writes[0].dstBinding = 0;
		writes[0].dstArrayElement = 0;
		writes[0].descriptorType = vk::DescriptorType::eUniformBuffer;
		writes[0].descriptorCount = 1;
		writes[0].pBufferInfo = &bufferInfo;

		writes[1].dstSet = *SceneDescriptorSets[0]; // @todo: i];
		writes[1].dstBinding = 1;
		writes[1].dstArrayElement = 0;
		writes[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
		writes[1].descriptorCount = 1;
		writes[1].pImageInfo = &imageInfo;

		Context.GetDevice().updateDescriptorSets(writes, {});
	}

	void VulkanRenderer::UpdateSceneDescriptorSets()
	{
		vk::DescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = *SceneUniformBuffer;
		bufferInfo.offset = 0;
		bufferInfo.range = sizeof(SceneUBO);

		vk::DescriptorImageInfo albedoImageInfo{};
		albedoImageInfo.sampler = TestTexture.GetSampler();
		albedoImageInfo.imageView = TestTexture.GetImageView();
		albedoImageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

		vk::DescriptorImageInfo cubemapImageInfo{};
		cubemapImageInfo.sampler = SkyboxCubemap.GetSampler();
		cubemapImageInfo.imageView = SkyboxCubemap.GetImageView();
		cubemapImageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

		std::array<vk::WriteDescriptorSet, 3> writes{};

		writes[0].dstSet = *SceneDescriptorSets.front();
		writes[0].dstBinding = 0;
		writes[0].dstArrayElement = 0;
		writes[0].descriptorType = vk::DescriptorType::eUniformBuffer;
		writes[0].descriptorCount = 1;
		writes[0].pBufferInfo = &bufferInfo;

		writes[1].dstSet = *SceneDescriptorSets.front();
		writes[1].dstBinding = 1;
		writes[1].dstArrayElement = 0;
		writes[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
		writes[1].descriptorCount = 1;
		writes[1].pImageInfo = &albedoImageInfo;

		writes[2].dstSet = *SceneDescriptorSets.front();
		writes[2].dstBinding = 2;
		writes[2].dstArrayElement = 0;
		writes[2].descriptorType = vk::DescriptorType::eCombinedImageSampler;
		writes[2].descriptorCount = 1;
		writes[2].pImageInfo = &cubemapImageInfo;

		Context.GetDevice().updateDescriptorSets(writes, {});
	}

	void VulkanRenderer::CreateSceneUniformBuffer()
	{
		vk::DeviceSize bufferSize = sizeof(SceneUBO);

		vk::BufferCreateInfo bufferInfo{};
		bufferInfo.size = bufferSize;
		bufferInfo.usage = vk::BufferUsageFlagBits::eUniformBuffer;
		bufferInfo.sharingMode = vk::SharingMode::eExclusive;

		SceneUniformBuffer = vk::raii::Buffer(Context.GetDevice(), bufferInfo);

		vk::MemoryRequirements memRequirements = SceneUniformBuffer.getMemoryRequirements();

		vk::MemoryAllocateInfo allocInfo{};
		allocInfo.allocationSize = memRequirements.size;
		allocInfo.memoryTypeIndex = FindMemoryType(
			*Context.GetPhysicalDevice(),
			memRequirements.memoryTypeBits,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
		);

		SceneUniformBufferMemory = vk::raii::DeviceMemory(Context.GetDevice(), allocInfo);
		SceneUniformBuffer.bindMemory(*SceneUniformBufferMemory, 0);
	}

	void VulkanRenderer::UpdateSceneUniforms(float deltaTime)
	{
		const vk::Extent2D extent = SceneViewport.GetExtent();
		Camera.AspectRatio = extent.height > 0
			? static_cast<float>(extent.width) / static_cast<float>(extent.height)
			: 1.0f;

		SceneUBO ubo{};
		ubo.ViewProj = Camera.GetViewProjectionMatrix();
		ubo.InvViewProj = glm::inverse(ubo.ViewProj);
		ubo.ViewportSize = glm::vec2(static_cast<float>(extent.width), static_cast<float>(extent.height));

		ubo.CameraWorldPos = glm::vec4(Camera.Position, 1.0f);

		// Direction from surface toward the light
		ubo.LightDirectionWS = glm::vec4(glm::normalize(glm::vec3(0.4f, 1.0f, 0.2f)), 0.0f);

		// rgb = light color, a = ambient strength
		ubo.LightColor = glm::vec4(1.0f, 0.98f, 0.95f, 0.18f);

		void* mapped = SceneUniformBufferMemory.mapMemory(0, sizeof(SceneUBO));
		std::memcpy(mapped, &ubo, sizeof(SceneUBO));
		SceneUniformBufferMemory.unmapMemory();
	}

	void VulkanRenderer::CreateTestTextureData()
	{
		TestTexture.SetContext(Context);
		const bool bLoaded = TestTexture.Load();
		ASSERT(bLoaded);

		LOG_INFO("Texture image view valid: {}", static_cast<bool>(static_cast<VkImageView>(TestTexture.GetImageView())));
		LOG_INFO("Texture sampler valid: {}", static_cast<bool>(static_cast<VkSampler>(TestTexture.GetSampler())));
	}

	void VulkanRenderer::CreateTestMeshData()
	{
		// Cube with individual faces (no shared corner vertices)
		// {Position}, {Color}, {UV}, {Normal}
		MeshVertices =
		{
			// Front (+Z)
			{ {-0.5f, -0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}, { 0.0f,  0.0f,  1.0f} },
			{ { 0.5f, -0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}, { 0.0f,  0.0f,  1.0f} },
			{ { 0.5f,  0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}, { 0.0f,  0.0f,  1.0f} },
			{ {-0.5f,  0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}, { 0.0f,  0.0f,  1.0f} },

			// Back (-Z)
			{ { 0.5f, -0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}, { 0.0f,  0.0f, -1.0f} },
			{ {-0.5f, -0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}, { 0.0f,  0.0f, -1.0f} },
			{ {-0.5f,  0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}, { 0.0f,  0.0f, -1.0f} },
			{ { 0.5f,  0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}, { 0.0f,  0.0f, -1.0f} },

			// Left (-X)
			{ {-0.5f, -0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}, {-1.0f,  0.0f,  0.0f} },
			{ {-0.5f, -0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}, {-1.0f,  0.0f,  0.0f} },
			{ {-0.5f,  0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}, {-1.0f,  0.0f,  0.0f} },
			{ {-0.5f,  0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}, {-1.0f,  0.0f,  0.0f} },

			// Right (+X)
			{ { 0.5f, -0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}, { 1.0f,  0.0f,  0.0f} },
			{ { 0.5f, -0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}, { 1.0f,  0.0f,  0.0f} },
			{ { 0.5f,  0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}, { 1.0f,  0.0f,  0.0f} },
			{ { 0.5f,  0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}, { 1.0f,  0.0f,  0.0f} },

			// Top (+Y)
			{ {-0.5f,  0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}, { 0.0f,  1.0f,  0.0f} },
			{ { 0.5f,  0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}, { 0.0f,  1.0f,  0.0f} },
			{ { 0.5f,  0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}, { 0.0f,  1.0f,  0.0f} },
			{ {-0.5f,  0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}, { 0.0f,  1.0f,  0.0f} },

			// Bottom (-Y)
			{ {-0.5f, -0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}, { 0.0f, -1.0f,  0.0f} },
			{ { 0.5f, -0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}, { 0.0f, -1.0f,  0.0f} },
			{ { 0.5f, -0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}, { 0.0f, -1.0f,  0.0f} },
			{ {-0.5f, -0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}, { 0.0f, -1.0f,  0.0f} }
		};

		MeshIndices =
		{
			// Front
			0, 1, 2,  2, 3, 0,

			// Back
			4, 5, 6,  6, 7, 4,

			// Left
			8, 9, 10,  10, 11, 8,

			// Right
			12, 13, 14,  14, 15, 12,

			// Top
			16, 17, 18,  18, 19, 16,

			// Bottom
			20, 21, 22,  22, 23, 20
		};

		// @todo: Move Cube Mesh Management to a more generalized place for mesh handling
		{
			CubeMesh.Release();

			Nyx::MeshData cubeData;
			cubeData.Vertices = MeshVertices;
			cubeData.Indices = MeshIndices;

			CubeMesh.Upload(Context, cubeData);
		}
	}

	void VulkanRenderer::CreateTestMeshBuffers()
	{
		const vk::DeviceSize vertexBufferSize = sizeof(Vertex) * MeshVertices.size();
		const vk::DeviceSize indexBufferSize = sizeof(uint32_t) * MeshIndices.size();

		CreateBuffer(
			vertexBufferSize,
			vk::BufferUsageFlagBits::eVertexBuffer,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
			MeshVertexBuffer,
			MeshVertexBufferMemory
		);

		CreateBuffer(
			indexBufferSize,
			vk::BufferUsageFlagBits::eIndexBuffer,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
			MeshIndexBuffer,
			MeshIndexBufferMemory
		);

		{
			void* mapped = MeshVertexBufferMemory.mapMemory(0, vertexBufferSize);
			std::memcpy(mapped, MeshVertices.data(), static_cast<size_t>(vertexBufferSize));
			MeshVertexBufferMemory.unmapMemory();
		}

		{
			void* mapped = MeshIndexBufferMemory.mapMemory(0, indexBufferSize);
			std::memcpy(mapped, MeshIndices.data(), static_cast<size_t>(indexBufferSize));
			MeshIndexBufferMemory.unmapMemory();
		}
	}

	void VulkanRenderer::CreateBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties, vk::raii::Buffer& outBuffer, vk::raii::DeviceMemory& outMemory)
	{
		vk::BufferCreateInfo bufferInfo{};
		bufferInfo.size = size;
		bufferInfo.usage = usage;
		bufferInfo.sharingMode = vk::SharingMode::eExclusive;

		outBuffer = vk::raii::Buffer(Context.GetDevice(), bufferInfo);

		const vk::MemoryRequirements memRequirements = outBuffer.getMemoryRequirements();

		vk::MemoryAllocateInfo allocInfo{};
		allocInfo.allocationSize = memRequirements.size;
		allocInfo.memoryTypeIndex = FindMemoryType(
			*Context.GetPhysicalDevice(),
			memRequirements.memoryTypeBits,
			properties
		);

		outMemory = vk::raii::DeviceMemory(Context.GetDevice(), allocInfo);
		outBuffer.bindMemory(*outMemory, 0);
	}

	uint32_t VulkanRenderer::FindMemoryType(vk::PhysicalDevice physicalDevice, uint32_t typeFilter, vk::MemoryPropertyFlags properties)
	{
		const vk::PhysicalDeviceMemoryProperties memProperties = physicalDevice.getMemoryProperties();

		for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i)
		{
			if ((typeFilter & (1 << i)) &&
				(memProperties.memoryTypes[i].propertyFlags & properties) == properties)
			{
				return i;
			}
		}

		throw std::runtime_error("Failed to find suitable memory type.");
	}

	float VulkanRenderer::ComputeDeltaTime()
	{
		static double lastTime = glfwGetTime();
		const double currentTime = glfwGetTime();
		const float deltaTime = static_cast<float>(currentTime - lastTime);
		lastTime = currentTime;
		return deltaTime;
	}

	bool VulkanRenderer::RecompileGridShaders()
	{
		const std::string vertCmd =
			"glslc \"" + GridShaderHotReload.VertSourcePath.string() +
			"\" -o \"" + GridShaderHotReload.VertSpvPath.string() + "\"";

		const std::string fragCmd =
			"glslc \"" + GridShaderHotReload.FragSourcePath.string() +
			"\" -o \"" + GridShaderHotReload.FragSpvPath.string() + "\"";

		const int vertResult = std::system(vertCmd.c_str());
		const int fragResult = std::system(fragCmd.c_str());

		return vertResult == 0 && fragResult == 0;
	}

	void VulkanRenderer::PollGridShaderHotReload()
	{
		std::filesystem::file_time_type newVertTime{};
		std::filesystem::file_time_type newFragTime{};

		const bool bVertOk = TryGetFileWriteTime(GridShaderHotReload.VertSourcePath, newVertTime);
		const bool bFragOk = TryGetFileWriteTime(GridShaderHotReload.FragSourcePath, newFragTime);

		// During save, files can be temporarily missing/locked. Just try again next frame.
		if (!bVertOk || !bFragOk)
		{
			return;
		}

		const bool bChanged =
			newVertTime != GridShaderHotReload.LastVertWriteTime ||
			newFragTime != GridShaderHotReload.LastFragWriteTime;

		if (!bChanged)
		{
			return;
		}

		GridShaderHotReload.LastVertWriteTime = newVertTime;
		GridShaderHotReload.LastFragWriteTime = newFragTime;

		const bool bCompileOk = RecompileGridShaders();
		if (bCompileOk)
		{
			GridShaderHotReload.bReloadPending = true;
		}
	}

	void VulkanRenderer::LoadTestGltfScene()
	{
		ImportedScene imported{};
		const bool bLoaded = Nyx::LoadStaticGltfScene(
			(Nyx::Paths::GetAssetsDir() / "Models" / "Debug" / "teapot.gltf").string(),
			imported
		);
		ASSERT(bLoaded && "Failed to load test glTF scene.");

		// Create mesh resources
		std::vector<Nyx::Mesh*> importedMeshPtrs;
		importedMeshPtrs.reserve(imported.Primitives.size());

		for (size_t i = 0; i < imported.Primitives.size(); ++i)
		{
			auto mesh = std::make_unique<Nyx::Mesh>("ImportedPrimitive_" + std::to_string(i));
			const bool bUploaded = mesh->Upload(Context, imported.Primitives[i].Mesh);
			ASSERT(bUploaded && "Failed to upload imported mesh.");

			importedMeshPtrs.push_back(mesh.get());
			LoadedMeshes.push_back(std::move(mesh));
		}

		// Very minimal material choice:
		// if imported material has a base color texture, use textured material,
		// otherwise use untextured one.
		for (const ImportedNodeInstance& instance : imported.Instances)
		{
			if (instance.PrimitiveIndex < 0 || instance.PrimitiveIndex >= static_cast<int>(imported.Primitives.size()))
			{
				continue;
			}

			const ImportedPrimitive& importedPrim = imported.Primitives[instance.PrimitiveIndex];

			RenderObject obj{};
			obj.MeshAsset = importedMeshPtrs[instance.PrimitiveIndex];
			obj.WorldTransform = instance.WorldTransform;

			if (importedPrim.MaterialIndex >= 0 &&
				importedPrim.MaterialIndex < static_cast<int>(imported.Materials.size()) &&
				!imported.Materials[importedPrim.MaterialIndex].BaseColorTexturePath.empty())
			{
				obj.MaterialAsset = &TexturedMaterial;
			}
			else
			{
				obj.MaterialAsset = &UntexturedMaterial;
			}

			RenderObjects.push_back(obj);
		}
	}

	void VulkanRenderer::DrawSkybox(vk::raii::CommandBuffer& cmd)
	{
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *SkyboxPipeline);

		cmd.bindDescriptorSets(
			vk::PipelineBindPoint::eGraphics,
			*SkyboxPipelineLayout,
			0,
			{ *SkyboxDescriptorSets.front() },
			{}
		);

		vk::DeviceSize offsets[] = { 0 };
		cmd.bindVertexBuffers(0, { CubeMesh.GetVertexBuffer() }, offsets);
		cmd.bindIndexBuffer(CubeMesh.GetIndexBuffer(), 0, vk::IndexType::eUint32);
		cmd.drawIndexed(CubeMesh.GetIndexCount(), 1, 0, 0, 0);
	}

	void VulkanRenderer::OnMouseWheelScrolled(double yOffset)
	{
		// Don't change anything if the mouse interacting with UI
		if (ImGui::GetIO().WantCaptureMouse && !bSceneViewportHovered)
		{
			return;
		}

		if (yOffset > 0.0)
		{
			CameraMoveSpeed *= CameraSpeedStep;
		}
		else if (yOffset < 0.0)
		{
			CameraMoveSpeed /= CameraSpeedStep;
		}

		CameraMoveSpeed = glm::clamp(CameraMoveSpeed, CameraSpeedMin, CameraSpeedMax);
	}

	void VulkanRenderer::SetSceneWindowHovered(bool hovered)
	{
		bSceneViewportHovered = hovered;
	}
}