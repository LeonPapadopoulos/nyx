#pragma once

#include "VulkanViewportTarget.h"
#include "VulkanBlur.h"
#include "EditorCamera.h"
#include "SceneViewTypes.h"
#include "VulkanDebugLines.h"
#include "VulkanEditorOverlays.h"

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

#include <optional>
#include <vector>

namespace Nyx
{
	class Registry;
}

namespace Nyx
{
	// One draw of a view's scene pass, of every section of an object's mesh with its slot's
	// material: `InstanceCount` instances from `FirstInstance` on in the instance stream, each
	// relative to `Model`. The stream is the object's instances, or for a batch of objects that draw
	// alike, null: this frame's stream of their transforms (VulkanRenderer::FrameInstances), with
	// `Model` the identity.
	struct SceneViewDraw
	{
		// In RenderWorld::Objects; the first of a batch
		uint32_t Object = 0;
		vk::Buffer InstanceBuffer;
		uint32_t FirstInstance = 0;
		uint32_t InstanceCount = 1;
		glm::mat4 Model{ 1.0f };
	};

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
		// Its world's debug drawing (IRenderer::SetWorldDebugDraw()); on unless hidden
		// (IRenderer::SetSceneViewShowDebugDrawing())
		bool bShowDebugDrawing = true;

		// See IRenderer::SetSceneViewExposure()
		float ExposureStops = 0.0f;
		// See IRenderer::SetSceneViewSky() and SetSceneViewDebug()
		ESceneViewSky Sky = ESceneViewSky::Scene;
		float SkyRotation = 0.0f;
		ESceneViewDebug Debug = ESceneViewDebug::Lit;
		// See IRenderer::SetSceneViewCullingDebug(); a frozen view's camera as it was frozen, taken
		// at its first culling after
		ESceneViewCullingDebug CullingDebug = ESceneViewCullingDebug::Off;
		std::optional<glm::mat4> FrozenCullingViewProjection;

		// What its scene pass draws this frame, culled by its camera (VulkanRenderer::CullSceneView()),
		// what was culled, and the bounds culled by as debug lines (CullingDebug)
		std::vector<SceneViewDraw> Draws;
		SceneViewCullingStats Culling;
		DebugLineRange CullingLines;
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
