#pragma once

#include "VulkanViewportTarget.h"
#include "VulkanBlur.h"
#include "EditorCamera.h"
#include "SceneViewTypes.h"
#include "Entity.h"
#include <optional>

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

		// Grid, debug lines, picking and the selection outline; see IRenderer::SetSceneViewShowEditorOverlays()
		bool bShowEditorOverlays = true;

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

		std::optional<Nyx::Entity> LastPickedEntity;

		uint64_t RenderedWorldRevision = 0;
		uint64_t PickWorldRevision = 0;

		void ResetPicking()
		{
			bPickRequestPending = false;
			bPickReadbackPending = false;
			bPickResultReady = false;
			PendingPickX = 0;
			PendingPickY = 0;
			LastPickedEntity.reset();
			RenderedWorldRevision = 0;
			PickWorldRevision = 0;
		}

		// -------------------------------------------------
		// Selection
		// -------------------------------------------------

		vk::raii::Image VisibleSelectionMaskImage{ nullptr };
		vk::raii::DeviceMemory VisibleSelectionMaskImageMemory{ nullptr };
		vk::raii::ImageView VisibleSelectionMaskImageView{ nullptr };

		vk::raii::Image FullSelectionMaskImage{ nullptr };
		vk::raii::DeviceMemory FullSelectionMaskImageMemory{ nullptr };
		vk::raii::ImageView FullSelectionMaskImageView{ nullptr };

		vk::raii::Image SelectionMaskDepthImage{ nullptr };
		vk::raii::DeviceMemory SelectionMaskDepthImageMemory{ nullptr };
		vk::raii::ImageView SelectionMaskDepthImageView{ nullptr };

		vk::raii::RenderPass SelectionMaskRenderPass{ nullptr };
		vk::raii::Framebuffer VisibleSelectionMaskFramebuffer{ nullptr };
		vk::raii::Framebuffer FullSelectionMaskFramebuffer{ nullptr };

		vk::raii::DescriptorPool OutlineDescriptorPool{ nullptr };
		std::optional<vk::raii::DescriptorSets> OutlineDescriptorSets;

		// What the masks were last drawn from (VulkanRenderer::GetSelectionMaskFingerprint()):
		// they are drawn again only when it changes. Unset while nothing is selected, and when the
		// masks are made anew, so that they are drawn before the outline reads them.
		std::optional<uint64_t> SelectionMaskFingerprint;

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
