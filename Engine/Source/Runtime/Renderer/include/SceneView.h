#pragma once

#include "VulkanViewportTarget.h"
#include "EditorCamera.h"
#include "SceneViewTypes.h"

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

		bool bHovered = false;
		bool bFocused = false;

		bool bResizePending = false;
		uint32_t PendingWidth = 1;
		uint32_t PendingHeight = 1;

		bool bRecreatedThisFrame = false;
	};
}