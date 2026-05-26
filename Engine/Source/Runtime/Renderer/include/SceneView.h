#pragma once

#include "VulkanViewportTarget.h"
#include "EditorCamera.h"
#include "SceneViewTypes.h"
#include "Entity.h"
#include <optional>

namespace Nyx
{
	struct SceneViewInstance
	{
		uint64_t Id = 0;

		VulkanViewportTarget RenderTarget;

		EViewportCameraMode CameraMode = EViewportCameraMode::EditorFreeCamera;
		EditorCamera EditorCam;
		ExtractedSceneGlobals SceneGlobals;

		vk::raii::Buffer SceneUniformBuffer{ nullptr };
		vk::raii::DeviceMemory SceneUniformBufferMemory{ nullptr };
		vk::raii::DescriptorPool SceneDescriptorPool{ nullptr };
		vk::raii::DescriptorSets SceneDescriptorSets{ nullptr };

		vk::raii::Buffer SkyboxUniformBuffer{ nullptr };
		vk::raii::DeviceMemory SkyboxUniformBufferMemory{ nullptr };
		vk::raii::DescriptorPool SkyboxDescriptorPool{ nullptr };
		vk::raii::DescriptorSets SkyboxDescriptorSets{ nullptr };

		// -------------------------------------------------
		// Picking
		// -------------------------------------------------
		vk::raii::Image PickingImage{ nullptr };
		vk::raii::DeviceMemory PickingImageMemory{ nullptr };
		vk::raii::ImageView PickingImageView{ nullptr };

		vk::raii::Image PickingDepthImage{ nullptr };
		vk::raii::DeviceMemory PickingDepthImageMemory{ nullptr };
		vk::raii::ImageView PickingDepthImageView{ nullptr };

		vk::raii::RenderPass PickingRenderPass{ nullptr };
		vk::raii::Framebuffer PickingFramebuffer{ nullptr };

		vk::raii::Buffer PickingReadbackBuffer{ nullptr };
		vk::raii::DeviceMemory PickingReadbackBufferMemory{ nullptr };

		bool bPickRequestPending = false;
		bool bPickReadbackPending = false;
		bool bPickResultReady = false;
		
		uint32_t PendingPickX = 0;
		uint32_t PendingPickY = 0;

		std::optional<Nyx::Engine::Entity> LastPickedEntity;

		// -------------------------------------------------
		// Selection
		// -------------------------------------------------

		vk::raii::Image SelectionMaskImage{ nullptr };
		vk::raii::DeviceMemory SelectionMaskImageMemory{ nullptr };
		vk::raii::ImageView SelectionMaskImageView{ nullptr };

		vk::raii::Image SelectionMaskDepthImage{ nullptr };
		vk::raii::DeviceMemory SelectionMaskDepthImageMemory{ nullptr };
		vk::raii::ImageView SelectionMaskDepthImageView{ nullptr };

		vk::raii::RenderPass SelectionMaskRenderPass{ nullptr };
		vk::raii::Framebuffer SelectionMaskFramebuffer{ nullptr };

		vk::raii::DescriptorPool OutlineDescriptorPool{ nullptr };
		std::optional<vk::raii::DescriptorSets> OutlineDescriptorSets;

		// -------------------------------------------------
		// --
		// -------------------------------------------------

		bool bHovered = false;
		bool bFocused = false;

		bool bResizePending = false;
		uint32_t PendingWidth = 1;
		uint32_t PendingHeight = 1;

		bool bRecreatedThisFrame = false;
	};
}