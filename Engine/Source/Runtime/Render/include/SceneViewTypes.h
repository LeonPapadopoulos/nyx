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
	// surface materials, where the light before tonemapping is wrong, or what a pass after the
	// scene made of it. The values are nyx/view.slang's Debug* constants.
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
		// The bloom alone, as the tonemap adds it (black where nothing blooms, or bloom is off)
		Bloom,
		// The ambient occlusion, white where all of the sky's light reaches, darker where less does
		// (white where it is off)
		AmbientOcclusion,
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
		// The world's point lights, those whose light reaches nowhere in view, and those in view left
		// out past the most a view lights its pixels with (CullPointLights())
		uint32_t PointLights = 0;
		uint32_t PointLightsCulled = 0;
		uint32_t PointLightsDropped = 0;
		// The world's particle emitters with particles to draw, those wholly out of view, and the
		// particles drawn
		uint32_t ParticleEmitters = 0;
		uint32_t ParticleEmittersCulled = 0;
		uint32_t Particles = 0;
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

	// The world's air as a view draws it (EnvironmentComponent, in range, under the view's light:
	// MakeSceneViewAtmosphere()); without an environment, none: no fog, the sky as it is
	struct SceneViewAtmosphere
	{
		// The height fog: none at density 0. Its density a metre at its height, how fast it thins
		// upwards, from how far from the camera on, and how much it hides at most
		float FogDensity = 0.0f;
		float FogHeightFalloff = 0.0f;
		float FogHeight = 0.0f;
		float FogStartDistance = 0.0f;
		float FogMaxOpacity = 1.0f;
		// What it scatters towards the camera: its own colour, tinted as the sky is, and the sun's
		// light, gathered round the sun by its lobe (Henyey-Greenstein's g)
		glm::vec3 FogColor{ 0.0f };
		glm::vec3 FogSunColor{ 0.0f };
		float FogSunLobe = 0.0f;
		// What the sky and its light are multiplied by
		glm::vec3 SkyTint{ 1.0f };
		// The sky's glow round the sun, and its lobe
		glm::vec3 SunGlowColor{ 0.0f };
		float SunGlowLobe = 0.0f;
		// The light's colour where it reaches the ground, through the air
		glm::vec3 SunColor{ 1.0f };
	};

	// The view's bloom (EnvironmentComponent, in range: MakeSceneViewBloomSettings()); none
	// without an environment
	struct SceneViewBloomSettings
	{
		bool bBloom = false;
		float Threshold = 1.0f;
		float Knee = 0.5f;
		float Intensity = 0.6f;
		float Radius = 0.7f;
		uint32_t Levels = 6;
	};

	// What a view's passes after its scene pass took on the GPU in the last frame drawn, in
	// milliseconds (VulkanRenderer::GetSceneViewGpuTimes()): 0 for a pass that didn't run, and
	// without timestamps on the device
	struct SceneViewGpuTimes
	{
		float Bloom = 0.0f;
		float AmbientOcclusion = 0.0f;
		float Particles = 0.0f;
	};

	// The view's ambient occlusion (EnvironmentComponent, in range:
	// MakeSceneViewAmbientOcclusionSettings()); none without an environment. The slices round the
	// view and the steps out along each side a pixel takes, by its quality.
	struct SceneViewAmbientOcclusionSettings
	{
		bool bAmbientOcclusion = false;
		float Radius = 1.0f;
		float Intensity = 1.5f;
		uint32_t Slices = 3;
		uint32_t Steps = 6;
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
		SceneViewAtmosphere Atmosphere;
		SceneViewBloomSettings Bloom;
		SceneViewAmbientOcclusionSettings AmbientOcclusion;

		bool bHasCamera = false;
		bool bHasDirectionalLight = false;
	};
}