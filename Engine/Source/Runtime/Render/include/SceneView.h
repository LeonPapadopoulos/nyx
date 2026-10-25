#pragma once

#include "VulkanViewportTarget.h"
#include "VulkanBlur.h"
#include "VulkanBloom.h"
#include "VulkanAmbientOcclusion.h"
#include "VulkanColorGrading.h"
#include "EditorCamera.h"
#include "PointLights.h"
#include "SceneViewTypes.h"
#include "ShadowCascades.h"
#include "VulkanDebugLines.h"
#include "VulkanEditorOverlays.h"
#include "VulkanParticles.h"

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

#include <array>
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

	// The primary light's shadows as a view draws them this frame
	// (VulkanRenderer::PrepareSceneViewShadows()): its cascades, each with the draws of what casts
	// into it, culled by the cascade's own frustum as the view's are by its camera
	struct SceneViewShadows
	{
		// 0: none this frame (no light that casts, or nothing to cast)
		uint32_t CascadeCount = 0;
		SceneViewShadowSettings Settings;
		std::array<ShadowCascade, MaxShadowCascades> Cascades{};
		std::array<std::vector<SceneViewDraw>, MaxShadowCascades> Draws;
		std::array<SceneViewCullingStats, MaxShadowCascades> Culling{};
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
		// Its shadows this frame, and what it shows of them (IRenderer::SetSceneViewShadowDebug())
		SceneViewShadows Shadows;
		ESceneViewShadowDebug ShadowDebug = ESceneViewShadowDebug::Off;
		// The HDR picture's slot in the bindless texture array, for the tonemap; the ambient light's
		// and the depth's, for the ambient occlusion
		uint32_t HdrTextureSlot = 0;
		uint32_t AmbientTextureSlot = 0;
		uint32_t DepthTextureSlot = 0;
		// Its bloom: the chain's levels, made for its size, and its settings this frame (VulkanBloom)
		SceneViewBloom Bloom;
		// Its ambient occlusion: its targets at half its size, and its settings this frame
		// (VulkanAmbientOcclusion)
		SceneViewAmbientOcclusion AmbientOcclusion;
		// Its colour grading: its lookup table, and its grade this frame (VulkanColorGrading)
		SceneViewColorGrading ColorGrading;
		// Its particles this frame, and their pass's framebuffer (VulkanParticles)
		SceneViewParticles Particles;
		// What its passes took on the GPU, in the last frame drawn; and its
		// place among the views timed this frame (VulkanRenderer::PassTimestamps)
		SceneViewGpuTimes GpuTimes;
		uint32_t TimedViewIndex = 0;
		// What it drew and its GPU times, in the last frame whose GPU work is done
		// (IRenderer::GetSceneViewFrameStats())
		SceneViewFrameStats Stats;

		// The point lights its pixels are lit by this frame (VulkanRenderer::CullSceneView())
		std::vector<ViewPointLight> PointLights;

		// The view's set (set 1): its SceneUBO, and its point lights, room for MaxViewPointLights
		vk::raii::Buffer SceneUniformBuffer{ nullptr };
		vk::raii::DeviceMemory SceneUniformBufferMemory{ nullptr };
		vk::raii::Buffer PointLightBuffer{ nullptr };
		vk::raii::DeviceMemory PointLightBufferMemory{ nullptr };
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
		// Its targets were made, or made again, and nothing was drawn into them since: no picture
		// yet, and what draws into them must make what it keeps of them again
		bool bTargetsNotDrawn = true;

		// Drawn only in the frames it is shown in (IRenderer::SetSceneViewDrawnWhenShown()); whether
		// its picture was asked for since the frame's views were prepared, and whether it is drawn
		// this frame
		bool bDrawnWhenShown = false;
		mutable bool bShownSincePrepared = false;
		bool bDrawnThisFrame = false;
	};
}
