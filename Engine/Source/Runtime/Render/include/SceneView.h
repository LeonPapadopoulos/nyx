#pragma once

#include "VulkanViewportTarget.h"
#include "VulkanBlur.h"
#include "EditorCamera.h"
#include "SceneViewTypes.h"
#include "VulkanEditorOverlays.h"

namespace Nyx
{
	class Registry;
}

namespace Nyx
{
	struct SceneViewInstance
	{
		uint64_t Id = 0;

		VulkanViewportTarget RenderTarget;

		// The world it shows, when not the renderer's (IRenderer::SetSceneViewWorld())
		const Nyx::Registry* OwnWorld = nullptr;

		EViewportCameraMode CameraMode = EViewportCameraMode::EditorFreeCamera;
		EditorCamera EditorCam;
		ExtractedSceneGlobals SceneGlobals;

		// Grid, debug lines, picking and the selection outline; off until the editor turns them on
		// (IEditorRenderer::SetSceneViewShowEditorOverlays())
		bool bShowEditorOverlays = false;

		// See IRenderer::SetSceneViewExposure()
		float ExposureStops = 0.0f;
		// See IRenderer::SetSceneViewSky() and SetSceneViewDebug()
		ESceneViewSky Sky = ESceneViewSky::Scene;
		float SkyRotation = 0.0f;
		ESceneViewDebug Debug = ESceneViewDebug::Lit;
		// The HDR picture's slot in the bindless texture array, for the tonemap
		uint32_t HdrTextureSlot = 0;

		// The view's set (set 1): its SceneUBO
		vk::raii::Buffer SceneUniformBuffer{ nullptr };
		vk::raii::DeviceMemory SceneUniformBufferMemory{ nullptr };
		vk::raii::DescriptorPool SceneDescriptorPool{ nullptr };
		vk::raii::DescriptorSets SceneDescriptorSets{ nullptr };

		// The editor overlays': picking, and the selection masks (VulkanEditorOverlays)
		SceneViewOverlays Overlays;

		// -------------------------------------------------
		// Blur: the picture blurred small, for glows and glass (VulkanBlur)
		// -------------------------------------------------

		Nyx::VulkanBlurTarget Blur;
		// How ImGui shows it: a descriptor set made by ImGui's backend, pointed at the new image
		// when the blur's images are made anew (its generation changes), so its ImTextureID
		// never changes
		VkDescriptorSet BlurImGuiTexture = VK_NULL_HANDLE;
		uint64_t BlurImGuiTextureGeneration = 0;

		// -------------------------------------------------
		// --
		// -------------------------------------------------

		bool bResizePending = false;
		uint32_t PendingWidth = 1;
		uint32_t PendingHeight = 1;

		bool bRecreatedThisFrame = false;
	};
}
