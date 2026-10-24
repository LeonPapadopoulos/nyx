#pragma once

#include <cstdint>
#include <glm/glm.hpp>

namespace Nyx
{
	enum class EViewportCameraMode : uint8_t
	{
		EditorFreeCamera,
		ScenePrimaryCamera
	};

	// The sky a scene view shows and is lit by (IRenderer::SetSceneViewSky())
	enum class ESceneViewSky : uint8_t
	{
		// The scene's sky
		Scene,
		// A grey room with soft lights, for looking at materials
		Studio,
		// Nothing: only the sun lights
		Black,
	};

	// What a scene view shows (IRenderer::SetSceneViewDebug()): the lit scene, one output of its
	// surface materials, or where the light before tonemapping is wrong. The values are
	// nyx/view.slang's Debug* constants.
	enum class ESceneViewDebug : uint8_t
	{
		Lit,
		BaseColor,
		// The normal, 0 to 1 per axis: facing the camera is light blue
		Normal,
		Roughness,
		Metallic,
		Occlusion,
		Emission,
		Opacity,
		// A checker coloured by the UV: red along u, green along v
		UvChecker,
		// Pixels that aren't numbers or are infinite, bright magenta over the scene in grey
		NanInf,
		// Light above 1 before tonemapping in red, below 0 in blue, over the scene in grey
		OutOfRange,
	};

	// What a scene view shows of its culling (IRenderer::SetSceneViewCullingDebug())
	enum class ESceneViewCullingDebug : uint8_t
	{
		Off,
		// The bounds it culls by, over the picture: those drawn in green, those culled in red, an
		// entity drawn instanced by its cells
		Bounds,
		// As Bounds, culled by where its camera was when frozen, whose frustum is drawn in yellow:
		// the camera moves on and shows what was left out
		Frozen,
	};

	// What a scene view shows of its shadows (IRenderer::SetSceneViewShadowDebug())
	enum class ESceneViewShadowDebug : uint8_t
	{
		Off,
		// The picture tinted by the cascade each surface takes its shadow from (red, green, blue,
		// yellow, nearest first), and each cascade's shadow map in the picture's bottom left,
		// white nearest the light, framed in its tint
		Cascades,
	};

	// What a scene view's scene pass drew of its world in a frame, and what it culled
	// (VulkanRenderer::GetSceneViewCullingStats())
	struct SceneViewCullingStats
	{
		// Entities with a mesh to draw, and those wholly out of view
		uint32_t Objects = 0;
		uint32_t ObjectsCulled = 0;
		// The cells of the entities drawn instanced that were in view, and those of them culled
		uint32_t Cells = 0;
		uint32_t CellsCulled = 0;
		// Meshes drawn, each instance one, and the draws they took, each of every section of a mesh:
		// a cell's instances, those of cells that follow on, or entities that draw alike, together
		uint32_t Instances = 0;
		uint32_t Draws = 0;
	};

	// The primary light's shadows as a view draws them (DirectionalLightComponent's settings, in
	// range); none without such a light, or with its Cast Shadows off
	struct SceneViewShadowSettings
	{
		bool bCastShadows = false;
		uint32_t CascadeCount = 4;
		float Distance = 80.0f;
		float SplitBlend = 0.8f;
		float DepthBias = 1.0f;
		float NormalBias = 1.5f;
		float Softness = 1.0f;
		uint32_t Resolution = 2048;
	};

	struct ExtractedSceneGlobals
	{
		glm::mat4 View{ 1.0f };
		glm::mat4 Projection{ 1.0f };
		glm::mat4 ViewProjection{ 1.0f };
		// The camera's near plane, in metres: where its first shadow cascade starts
		float NearPlane = 0.1f;

		glm::vec3 CameraWorldPos{ 0.0f, 0.0f, 3.0f };
		glm::vec3 LightDirectionWS{ glm::normalize(glm::vec3(0.4f, 1.0f, 0.2f)) };
		glm::vec3 LightColor{ 1.0f, 0.98f, 0.95f };
		float Ambient = 0.18f;
		SceneViewShadowSettings Shadows;

		bool bHasCamera = false;
		bool bHasDirectionalLight = false;
	};
}