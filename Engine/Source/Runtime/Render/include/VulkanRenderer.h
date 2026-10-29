#pragma once
#include "EditorRenderer.h"
#include <vulkan/vulkan_raii.hpp>
#include "VulkanContext.h"
#include "VulkanSwapchain.h"
#include "OffscreenRenderTarget.h"
#include "VulkanViewportTarget.h"
#include "Paths.h"
#include "ShaderPrograms.h"
#include "VulkanBlur.h"
#include "VulkanBindless.h"
#include "VulkanMaterials.h"
#include "VulkanEnvironment.h"
#include "VulkanUIBackend.h"
#include "VulkanDebugLines.h"
#include "VulkanEditorOverlays.h"
#include "VulkanShadows.h"
#include "VulkanBloom.h"
#include "VulkanAmbientOcclusion.h"
#include "VulkanLightShafts.h"
#include "VulkanCloudLayer.h"
#include "VulkanVolumetricClouds.h"
#include "VulkanColorGrading.h"
#include "VulkanLightning.h"
#include "VulkanParticles.h"
#include "VulkanSkyAtmosphere.h"
#include "Mesh.h"
#include "RuntimeMeshCache.h"
#include "MeshInstanceCache.h"
#include "SceneCulling.h"
#include "Texture.h"
#include "CubemapTexture.h"
#include "Material.h"
#include "Multisampling.h"
#include "SurfaceDecals.h"
#include "PointLights.h"
#include "Lightning.h"
#include "Particles.h"
#include "WindComponent.h"
#include "AssetResolver.h"
#include "GltfImporter.h"
#include <imgui.h>
#include <chrono>
#include <map>
#include <memory>
#include <span>
#include <unordered_map>

#include "Entity.h"
#include "TransformComponent.h"
#include "MeshRendererComponent.h"
#include "CameraComponent.h"
#include "CameraProjection.h"
#include "DirectionalLightComponent.h"
#include "EditorCamera.h"
#include "SceneViewTypes.h"
#include "SceneView.h"
#include "Extent2D.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Nyx::Tests
{
	struct SceneReplacementTestAccess;
}

namespace Nyx
{
	struct SceneUBO
	{
		glm::mat4 ViewProj;
		glm::mat4 InvViewProj;

		glm::vec2 ViewportSize;
		// The sky's turn about the up axis: its cosine and sine
		glm::vec2 SkyRotation{ 1.0f, 0.0f };

		glm::vec4 CameraWorldPos;   // xyz used
		glm::vec4 LightDirectionWS; // xyz used, normalized
		glm::vec4 LightColor;       // rgb = light color, a = ambient strength

		// The view's rotation without its position, for the sky
		glm::mat4 SkyboxViewProj{ 1.0f };
		// The sky's cube map in the bindless texture array
		uint32_t EnvironmentTexture = 0;
		// The view's scene in HDR, in the bindless texture array, for the tonemap
		uint32_t SceneTexture = 0;
		// What the scene's light is multiplied by before tonemapping
		float ExposureScale = 1.0f;
		// ESceneViewDebug
		uint32_t DebugView = 0;

		// The environment's light (VulkanEnvironment): the specular cube and its mips, the BRDF
		// table, the irradiance's nine coefficients
		uint32_t SpecularTexture = 0;
		uint32_t SpecularLevels = 0;
		uint32_t BrdfLookup = 0;
		// Seconds, for what moves by itself, such as a decal's pulse: the view's world's time
		// (RenderWorld::Time)
		float Time = 0.0f;
		glm::vec4 Irradiance[9] = {};

		// The primary light's shadows (VulkanShadows, nyx.shadows): each cascade's light view and
		// projection, from the world to its layer of the shadow map
		glm::mat4 ShadowViewProjection[MaxShadowCascades] = {};
		// Where each cascade ends, in metres from the camera along its view
		glm::vec4 ShadowSplits{ 0.0f };
		// How wide a texel of each cascade is in the world, in metres
		glm::vec4 ShadowTexelSizes{ 0.0f };
		// xyz: the way the camera looks
		glm::vec4 CameraForward{ 0.0f, 0.0f, -1.0f, 0.0f };
		// The shadow map, a 2D texture array in the bindless array; 0 cascades: no shadows
		uint32_t ShadowMap = 0;
		uint32_t ShadowCascadeCount = 0;
		// A texel of the map as a part of its width, and the part of each layer the view drew into
		float ShadowMapTexel = 0.0f;
		float ShadowMapScale = 1.0f;
		// The light's settings: PCF reach and the biases in texels, the shadow distance in metres
		float ShadowSoftness = 0.0f;
		float ShadowDepthBias = 0.0f;
		float ShadowNormalBias = 0.0f;
		float ShadowDistance = 0.0f;
		// ESceneViewShadowDebug
		uint32_t ShadowDebug = 0;
		uint32_t ShadowPadding[3] = {};

		// The world's air (SceneViewAtmosphere, nyx.atmosphere): the height fog's colour and its
		// density (0: no fog); its falloff, height, start distance and most opacity; the sun's light
		// it scatters and its lobe; the sky's tint; the sky's glow round the sun and its lobe
		glm::vec4 FogColor{ 0.0f };
		glm::vec4 FogShape{ 0.0f, 0.0f, 0.0f, 1.0f };
		glm::vec4 FogSun{ 0.0f };
		glm::vec4 SkyTint{ 1.0f, 1.0f, 1.0f, 0.0f };
		glm::vec4 SunGlow{ 0.0f };

		// The sky made by the air's scattering (VulkanSkyAtmosphere, nyx.sky), while the view draws
		// it: 1; its sky view and transmittance tables, its light's diffuse and specular cubes and
		// the latter's mips, in the bindless array; the planet's and the top of the air's radii in
		// kilometres; the camera's height above the ground the tables were made for, in kilometres
		// (x); the sun's disk's light before the air, and the cosine of its angular radius
		uint32_t SkyAtmosphere = 0;
		uint32_t SkyViewLut = 0;
		uint32_t TransmittanceLut = 0;
		uint32_t SkyIrradiance = 0;
		uint32_t SkySpecular = 0;
		uint32_t SkySpecularLevels = 0;
		float PlanetRadius = 0.0f;
		float AtmosphereTopRadius = 0.0f;
		glm::vec4 SkyCamera{ 0.0f };
		glm::vec4 SunDisk{ 0.0f, 0.0f, 0.0f, 1.0f };

		// The view's bloom (VulkanBloom): its first level in the bindless array, 0 without bloom,
		// and how much of it the tonemap adds
		uint32_t BloomTexture = 0;
		float BloomIntensity = 0.0f;
		uint32_t BloomPadding[2] = {};

		// The view's ambient occlusion (VulkanAmbientOcclusion): 1 while the scene pass writes the
		// sky's light apart for it; that light, the scene's depth, the occlusion and its depth in
		// metres at half size, in the bindless array; its radius, intensity, slices and steps
		uint32_t AmbientOcclusion = 0;
		uint32_t AmbientLightTexture = 0;
		uint32_t SceneDepthTexture = 0;
		uint32_t OcclusionTexture = 0;
		uint32_t OcclusionDepthTexture = 0;
		float OcclusionRadius = 0.0f;
		float OcclusionIntensity = 1.0f;
		uint32_t OcclusionSlices = 0;
		uint32_t OcclusionSteps = 0;
		uint32_t OcclusionPadding[3] = {};
		// 1 / the projection's [0][0] and [1][1], its [2][2] and [3][2]: from the picture and the
		// depth buffer to view space
		glm::vec4 ProjectionInfo{ 0.0f };

		// How many of the view's point lights (its set's second binding) its pixels are lit by
		uint32_t PointLightCount = 0;
		uint32_t PointLightPadding[3] = {};

		// The view's colour grading (VulkanColorGrading): its lookup table in the bindless array, 0
		// without grading
		uint32_t ColorGradingTable = 0;
		uint32_t ColorGradingPadding[3] = {};

		// The view's light shafts (VulkanLightShafts): what they gathered in the bindless array, 0
		// without them, and the samples along each line; the sun's place on the picture, how far
		// towards it the lines reach and the depth threshold; the light they add and the decay
		uint32_t LightShaftsTexture = 0;
		uint32_t LightShaftsSamples = 0;
		uint32_t LightShaftsPadding[2] = {};
		glm::vec4 LightShaftsSun{ 0.0f };
		glm::vec4 LightShaftsColor{ 0.0f };

		// The view's cloud layer (VulkanCloudLayer, nyx.clouds), while it is drawn: 1; its light
		// and what gets through it at the view's size, and its noise, in the bindless array; its
		// shell, shape, wind, where the camera is over
		// its pattern, the sun's light at the top of the air; the view's rotation and projection,
		// inverted
		uint32_t CloudLayer = 0;
		uint32_t CloudLightTexture = 0;
		uint32_t CloudNoiseTexture = 0;
		uint32_t CloudPadding = 0;
		glm::vec4 CloudShell{ 0.0f };
		glm::vec4 CloudShape{ 0.0f };
		glm::vec4 CloudWind{ 0.0f };
		glm::vec4 CloudOrigin{ 0.0f };
		glm::vec4 CloudSun{ 0.0f };
		glm::mat4 CloudInverseSkyViewProjection{ 1.0f };

		// The view's volumetric clouds (VulkanVolumetricClouds, nyx.volumetricclouds), while they are
		// drawn: 1; their light and transmittance and their depths as this frame leaves them, in the
		// bindless array; what this frame marches (bit 0: every pixel; bits 1 to 3 the squares' side)
		// and the debug view (from bit 8); what it marches into, the frame before's, the noises; the
		// steps, the pixel of each square marched, the curl noise; the band, coverage, erosion, the
		// noises' repeats and where the camera is over them, the lighting, the sun's light at the top
		// of the air; how far the camera moved less the wind since the frame before; the sunlit tint
		// and wispiness, the cellularity; its rotation and projection the frame before
		uint32_t VolumetricClouds = 0;
		uint32_t VolumetricCloudLightTexture = 0;
		uint32_t VolumetricCloudDepthTexture = 0;
		uint32_t VolumetricCloudMode = 0;
		uint32_t VolumetricCloudMarchLightTexture = 0;
		uint32_t VolumetricCloudMarchDepthTexture = 0;
		uint32_t VolumetricCloudPreviousLightTexture = 0;
		uint32_t VolumetricCloudPreviousDepthTexture = 0;
		uint32_t VolumetricCloudShapeTexture = 0;
		uint32_t VolumetricCloudDetailTexture = 0;
		uint32_t VolumetricCloudWeatherTexture = 0;
		uint32_t VolumetricCloudSteps = 0;
		uint32_t VolumetricCloudLightSteps = 0;
		uint32_t VolumetricCloudUpdateX = 0;
		uint32_t VolumetricCloudUpdateY = 0;
		uint32_t VolumetricCloudCurlTexture = 0;
		glm::vec4 VolumetricCloudBand{ 0.0f };
		glm::vec4 VolumetricCloudCoverage{ 0.0f };
		glm::vec4 VolumetricCloudErosion{ 0.0f };
		glm::vec4 VolumetricCloudRepeats{ 0.0f };
		glm::vec4 VolumetricCloudOrigins{ 0.0f };
		glm::vec4 VolumetricCloudDetailOrigin{ 0.0f };
		glm::vec4 VolumetricCloudLighting{ 0.0f };
		glm::vec4 VolumetricCloudShadowTint{ 0.0f };
		glm::vec4 VolumetricCloudSun{ 0.0f };
		glm::vec4 VolumetricCloudMotion{ 0.0f };
		glm::vec4 VolumetricCloudSunlitTint{ 0.0f };
		glm::vec4 VolumetricCloudShaping{ 0.0f };
		glm::mat4 VolumetricCloudPreviousSkyViewProjection{ 1.0f };

		// The world's wind (Wind.h), at the view's Time; and its texture of local air round its
		// camera (VulkanLocalWind): its slot, 0 without one; the corner of its first cell, 1 / a
		// cell's size (w); 1 / its size in metres along each axis, so a point's place in it is
		// (point - corner) × this
		Nyx::WindState Wind;
		uint32_t LocalWindTexture = 0;
		uint32_t LocalWindPadding0 = 0;
		uint32_t LocalWindPadding1 = 0;
		uint32_t LocalWindPadding2 = 0;
		glm::vec4 LocalWindOrigin{ 0.0f };
		glm::vec4 LocalWindScale{ 0.0f };
	};

	// The view's set (set 1) as nyx/view.slang reads it, in scalar layout; to whole 16 bytes, as
	// std140 would be too
	static_assert(sizeof(SceneUBO) == 1760, "SceneUBO must match ViewConstants in nyx/view.slang");

	// One draw's push constants, the same for every scene pipeline: ObjectConstants in
	// nyx/bindless.slang
	struct ObjectConstants
	{
		glm::mat4 Model{ 1.0f };
		// Where the material's values start in the material buffer
		uint32_t MaterialOffset = 0;
		// The picking pass writes it
		uint32_t EntityId = 0;
		// Two more values set per object by code: a decal's reveal and pulse
		glm::vec2 CustomData2{ 0.0f };
		// Set per object by code; the selection mask passes keep the mask's value in x, a decal its
		// colour and intensity
		glm::vec4 CustomData{ 0.0f };
		// Added to the instance stream's custom numbers: an entity drawn once from the identity
		// buffer (the editor's passes) has its MeshRendererComponent::Custom here; else zero
		glm::vec4 InstanceCustom{ 0.0f };
	};

	// Within the 128 bytes of push constants every Vulkan device has
	static_assert(sizeof(ObjectConstants) == 112, "ObjectConstants must match nyx/bindless.slang");

	struct Transform
	{
		glm::vec3 Position{ 0.0f, 0.0f, 0.0f };
		glm::vec3 RotationRadians{ 0.0f, 0.0f, 0.0f };
		glm::vec3 Scale{ 1.0f, 1.0f, 1.0f };

		glm::mat4 ToMatrix() const
		{
			glm::mat4 m = glm::translate(glm::mat4(1.0f), Position);
			m = glm::rotate(m, RotationRadians.x, glm::vec3(1.0f, 0.0f, 0.0f));
			m = glm::rotate(m, RotationRadians.y, glm::vec3(0.0f, 1.0f, 0.0f));
			m = glm::rotate(m, RotationRadians.z, glm::vec3(0.0f, 0.0f, 1.0f));
			m = glm::scale(m, Scale);
			return m;
		}
	};

	// A run of an object's mesh drawn with one material (MeshSection)
	struct RenderSection
	{
		uint32_t FirstIndex = 0;
		uint32_t IndexCount = 0;
		Material* MaterialAsset = nullptr;
	};

	struct RenderObject
	{
		Nyx::Entity SourceEntity{};

		// The mesh loaded by its path, or the one made at run time (RuntimeMeshComponent)
		Nyx::Mesh* MeshAsset = nullptr;
		// The first slot's material; the editor's passes draw the whole mesh with it
		Material* MaterialAsset = nullptr;
		// What the scene pass draws: RenderWorld::Sections from FirstSection on, each with its slot's
		// material
		uint32_t FirstSection = 0;
		uint32_t SectionCount = 0;
		// Where the entity is, for picking and the editor's overlays
		glm::mat4 WorldTransform{ 1.0f };
		// Where the scene pass draws it: between its last two steps in a world drawn so
		// (IRenderer::SetWorldInterpolation()), else where it is
		glm::mat4 DrawTransform{ 1.0f };
		// Its transform mirrors it (a negative determinant, e.g. one scale negative): its triangles
		// wind the other way round on the screen (SetMeshFaces())
		bool bMirrored = false;
		// Whether it casts the primary light's shadows: its Mesh Renderer's Cast Shadows
		// (MeshRendererComponent::bCastShadows) and one of its sections' materials' at least
		// (Material::bCastShadows); the cascades draw only the sections whose materials cast
		bool bCastShadows = true;
		// Drawn once, its instance's custom numbers in the stream (MeshRendererComponent::Custom);
		// drawn instanced, each instance has its own
		glm::vec4 Custom{ 0.0f };

		// Its mesh's bounds, in the entity's space; drawn instanced, around all of its instances
		glm::vec3 LocalBoundsMin{ -0.5f, -0.5f, -0.5f };
		glm::vec3 LocalBoundsMax{ 0.5f, 0.5f, 0.5f };
		// Around all of it where it is drawn, in the world: what the views cull it by, widened by
		// the most its materials' vertex functions move a vertex (Material::MaxVertexOffset), as
		// its cells are
		BoundingBox WorldBounds;

		// Its instances (MeshInstancesComponent), the second vertex stream, relative to the entity's
		// transform: their buffer (MeshInstanceCache), or the identity for an entity drawn once
		vk::Buffer InstanceBuffer;
		// New with each upload of its instances; 0 drawn once
		uint64_t InstanceUploadId = 0;
		// Its instances' cells, culled one by one: RenderWorld::Cells from FirstCell on; none for
		// an entity drawn once
		uint32_t FirstCell = 0;
		uint32_t CellCount = 0;

		uint32_t PickingId = 0;
	};

	// A world that scene views show (IRenderer::SetWorld(), SetSceneViewWorld()), as this frame
	// draws it
	struct RenderWorld
	{
		const Nyx::Registry* Registry = nullptr;
		// New each time the world is set, also at the same address, and never the same for two
		// worlds; picks of another revision are dropped. 0 is none.
		uint64_t Revision = 0;

		std::vector<RenderObject> Objects;
		std::vector<RenderSection> Sections;
		// The cells of the objects drawn instanced, in the world where they are drawn
		// (RenderObject::FirstCell)
		std::vector<InstanceCullCell> Cells;
		// Around every object that casts shadows, where it is drawn; none without one. The shadows'
		// cascades reach back towards the light as far as these do.
		std::optional<BoundingBox> CasterBounds;
		// Its surface decals, drawn over the objects and the sky, and their materials by path
		std::vector<RenderDecal> Decals;
		DecalMaterialCache DecalMaterials;
		// Its point lights, which each view culls for its own pixels
		std::vector<RenderPointLight> PointLights;
		// Its wind this frame, at its time (ExtractWind()): its game's where a game turns it
		// (WindDriveComponent), else turned here, from frame to frame, towards its WindComponent; and
		// that component's debug view and local air's texture
		Nyx::WindState Wind;
		Nyx::WindDrive WindDrive;
		// Whether it has a WindComponent, and how far its wind has carried what it carries at its
		// reference height by its time (GetWindTravel()), for its clouds
		bool bHasWind = false;
		glm::dvec2 WindTravel{ 0.0 };
		Nyx::EWindDebug WindDebug = Nyx::EWindDebug::Off;
		Nyx::WindComponent WindSettings;
		// Its particles, moved on each frame by its time, and the meshes its emitters name by path
		RenderParticles Particles;
		std::unordered_map<std::string, Nyx::Mesh*> ParticleMeshes;
		// Its lightning entities at its time, with the bolts on (ExtractLightning()); their lights are
		// in PointLights after the entities' own
		std::vector<RenderLightningBolt> Lightning;
		// Dense picking IDs: ID i is the entity at i - 1, 0 is nothing
		std::vector<Nyx::Entity> PickingIdToEntity;
		// This frame's time of what moves by itself in the world, such as a decal's pulse, in
		// seconds: the world's own where it is drawn between steps (a game's, paused or stepped
		// with it), else the renderer's
		float Time = 0.0f;
		// This frame's debug lines of the world: the editor overlays' (its objects' bounds and axes)
		// and its debug drawing's (IRenderer::SetWorldDebugDraw())
		DebugLineRange OverlayLines;
		DebugLineRange DebugDrawLines;
	};

	struct SceneCamera
	{
		float AspectRatio = 16.0f / 9.0f;

		glm::vec3 Position = glm::vec3(0.0f, 3.0f, 6.0f);

		float YawRadians = -glm::half_pi<float>(); // looking along -Z
		float PitchRadians = glm::radians(-10.0f);

		glm::vec3 GetForwardVector() const
		{
			glm::vec3 forward;
			forward.x = cos(YawRadians) * cos(PitchRadians);
			forward.y = sin(PitchRadians);
			forward.z = sin(YawRadians) * cos(PitchRadians);
			return glm::normalize(forward);
		}

		glm::vec3 GetRightVector() const
		{
			return glm::normalize(glm::cross(GetForwardVector(), glm::vec3(0.0f, 1.0f, 0.0f)));
		}

		glm::vec3 GetUpVector() const
		{
			return glm::normalize(glm::cross(GetRightVector(), GetForwardVector()));
		}

		glm::mat4 GetViewMatrix() const
		{
			return glm::lookAt(
				Position,
				Position + GetForwardVector(),
				glm::vec3(0.0f, 1.0f, 0.0f));
		}

		glm::mat4 GetProjectionMatrix() const
		{
			return Nyx::MakePerspectiveProjection(glm::radians(60.0f), AspectRatio, 0.1f, 100.0f);
		}

		glm::mat4 GetViewProjectionMatrix() const
		{
			return GetProjectionMatrix() * GetViewMatrix();
		}
	};
}

namespace Nyx
{
	class VulkanRenderer : public IEditorRenderer
	{
		friend struct Nyx::Tests::SceneReplacementTestAccess;

	public:
		VulkanRenderer();
		virtual ~VulkanRenderer() = default;

		virtual void Initialize(const char* applicationName, GLFWwindow* window);
		virtual void Shutdown();

		virtual void OnFramebufferResized();
		void SetVSync(bool bVSync) override;
		bool IsVSync() const override;
		void SetMsaaSamples(uint32_t samples) override { MsaaSamplesWanted = samples; }
		uint32_t GetMsaaSamples() const override { return static_cast<uint32_t>(MsaaSamples); }
		const char* GetApiName() const override { return "Vulkan"; }

		virtual void DrawFrame(const std::function<void()>& buildUI);

		void SetSelectedEntity(std::optional<Nyx::Entity> entity) override;

		virtual void WaitIdle();
		bool IsDeviceLost() const override;

		virtual void SetSceneViewCameraMode(uint64_t id, EViewportCameraMode mode);
		virtual void SetSceneViewEditorCameraTransform(uint64_t id, const glm::vec3& pos, const glm::vec3& rot);
		bool GetSceneViewEditorCameraTransform(uint64_t id, glm::vec3& outPos, glm::vec3& outRot) const override;
		std::optional<BoundingBox> GetEntityDrawnBounds(const Nyx::Registry& world, Nyx::Entity entity) const override;
		void SetSceneViewShowEditorOverlays(uint64_t id, bool bShow) override;
		void SetSceneViewExposure(uint64_t id, float stops) override;
		void SetSceneViewSky(uint64_t id, ESceneViewSky sky, float rotationRadians) override;
		void SetSceneViewDebug(uint64_t id, ESceneViewDebug view) override;
		void SetSceneViewCullingDebug(uint64_t id, ESceneViewCullingDebug mode) override;
		// What the view drew and culled in the last frame; null for an unknown view. For tests.
		const SceneViewCullingStats* GetSceneViewCullingStats(uint64_t id) const;
		void SetSceneViewShadowDebug(uint64_t id, ESceneViewShadowDebug mode) override;
		// The view's shadows in the last frame: its cascades and what each drew; null for an
		// unknown view. For tests.
		const SceneViewShadows* GetSceneViewShadows(uint64_t id) const;
		// For tests: the view's texture of local air (VulkanLocalWind)
		const SceneViewLocalWind* GetSceneViewLocalWind(uint64_t id) const;
		// The view's bloom in the last frame: its chain's levels and its settings; null for an
		// unknown view. For tests.
		const SceneViewBloom* GetSceneViewBloom(uint64_t id) const;
		// The view's light shafts in the last frame: their targets' size and their settings; null
		// for an unknown view. For tests.
		const SceneViewLightShafts* GetSceneViewLightShafts(uint64_t id) const;
		// The view's cloud layer in the last frame: its noise, its target and its settings; null for
		// an unknown view. For tests.
		const SceneViewCloudLayer* GetSceneViewCloudLayer(uint64_t id) const;
		// The view's volumetric clouds in the last frame: their noises, pictures and settings, what
		// the frame marched; null for an unknown view. For tests.
		const SceneViewVolumetricClouds* GetSceneViewVolumetricClouds(uint64_t id) const;
		// The view's colour grading in the last frame: its grade, what its table was baked from and
		// how often; null for an unknown view. For tests.
		const SceneViewColorGrading* GetSceneViewColorGrading(uint64_t id) const;
		// The view's grading table as it is on the GPU, x fastest, then y, then z; empty without one.
		// Between frames; waits for the GPU. For tests.
		std::vector<glm::vec4> ReadSceneViewColorGradingTable(uint64_t id);
		// The view's sky tables (VulkanSkyAtmosphere), and the pass that makes them; null for an
		// unknown view. For tests.
		SceneViewSkyTables* GetSceneViewSkyTables(uint64_t id);
		Nyx::VulkanSkyAtmosphere& GetSkyAtmosphere() { return SkyAtmosphere; }
		SkyTablePictures GetSceneViewSkyTablePictures(uint64_t id, float exposure) override;
		// What the view's passes after its scene pass took on the GPU in the last frame drawn; null
		// for an unknown view. For tests and the benchmark.
		const SceneViewGpuTimes* GetSceneViewGpuTimes(uint64_t id) const;
		const RenderFrameStats& GetFrameStats() const override { return FrameStats; }
		const SceneViewFrameStats* GetSceneViewFrameStats(uint64_t id) const override;

		bool GetSceneViewCameraData(uint64_t sceneViewId, Nyx::SceneViewCameraData& outData) const override;

		void SetWorld(const Nyx::Registry* world) override;
		void SetSceneViewWorld(uint64_t id, const Nyx::Registry* world) override;
		void SetWorldDebugDraw(const Nyx::Registry* world, const Nyx::DebugDraw* drawing) override;
		void SetSceneViewShowDebugDrawing(uint64_t id, bool bShow) override;
		void SetWorldInterpolation(const Nyx::Registry* world, const Nyx::FixedStepClock* clock) override;
		void SetWorldWindField(const Nyx::Registry* world, const Nyx::IWindField* field) override;

		Nyx::Mesh* GetCubeMesh() override;
		Nyx::Mesh* GetSphereMesh() override;
		Nyx::Mesh* GetCapsuleMesh() override;
		Nyx::Mesh* GetCrossedQuadsMesh() override;
		Nyx::Mesh* GetPlaneMesh() override;
		Nyx::Material* GetMaterial(const std::string& path) override;
		bool IsCompilingShaders() const override;
		const Nyx::ShaderLog& GetShaderLog() const override { return ShaderCompiles; }
		void SetShaderCompiledListener(ShaderCompiledListener listener) override { OnShaderCompiled = std::move(listener); }
		void FollowEditorShaders() override { Programs.FollowEditor(); }
		void ApplyCompiledShader(const Nyx::ShaderCompileRequest& request, const Nyx::ShaderCompileResult& result) override { Programs.Inject(request, result); }
		MaterialView ViewMaterial(const std::string& path) override;
		void SetMaterialValues(const std::string& path, const Nyx::MaterialAsset& asset) override;

	public:
		VulkanContext& GetContext();
		VulkanSwapchain& GetSwapchain();

		// The light of the sky, for tests
		Nyx::VulkanEnvironment& GetEnvironment() { return Environment; }
		Nyx::VulkanBindless& GetBindless() { return Bindless; }
		// The sky drawn behind the scene and lighting it: a cube of the bindless array, e.g. a
		// test's uniform one; its light is made again (waits for the GPU)
		void SetSky(uint32_t cubeSlot);
		uint32_t GetSky() const { return SkyboxTextureIndex; }

		// Draws 2D shapes recorded with a Painter into a VulkanPainterTarget
		Nyx::VulkanPainter& GetPainterPass() { return UI.GetPainterPass(); }

		// The UI's, from the UI backend
		uint64_t CreatePainterCanvas() override { return UI.CreatePainterCanvas(); }
		void DestroyPainterCanvas(uint64_t id) override { UI.DestroyPainterCanvas(id); }
		void SubmitPainterCanvas(uint64_t id, const Nyx::UI::Painter& painter, const Nyx::UI::PainterDrawOptions& options) override { UI.SubmitPainterCanvas(id, painter, options); }
		ImTextureID GetPainterCanvasTextureId(uint64_t id) const override { return UI.GetPainterCanvasTextureId(id); }
		std::optional<Nyx::UI::PainterTiles> GetPainterCanvasTiles(uint64_t id) const override { return UI.GetPainterCanvasTiles(id); }
		const Nyx::UI::PainterWindowStats& GetWindowPainterStats() const override { return UI.GetWindowPainterStats(); }

		uint64_t CreateUIImage(const Nyx::RgbaImage& image) override { return UI.CreateUIImage(image); }
		void DestroyUIImage(uint64_t id) override { UI.DestroyUIImage(id); }
		ImTextureID GetUIImageTextureId(uint64_t id) const override { return UI.GetUIImageTextureId(id); }

		void SaveScreenshot(const std::filesystem::path& path, ImVec2 min, ImVec2 max) override { UI.SaveScreenshot(path, min, max); }
		const std::string& GetScreenshotMessage() const override { return UI.GetScreenshotMessage(); }

		// The meshes made at run time, and the instances, on the GPU, for tests
		const Nyx::RuntimeMeshCache& GetRuntimeMeshes() const { return RuntimeMeshes; }
		const Nyx::MeshInstanceCache& GetMeshInstances() const { return MeshInstances; }

		ImTextureID GetSceneViewTextureId(uint64_t id) const override;
		// What a scene view rendered last, as RGBA; waits for the GPU. False for an unknown view, or
		// one not drawn since its targets were made. For tests.
		bool ReadSceneViewPixels(uint64_t id, Nyx::RgbaImage& outImage);
		// The memory of a scene view's targets, in bytes; 0 for an unknown view. For tests and the
		// benchmark.
		uint64_t GetSceneViewTargetMemory(uint64_t id) const;
		Extent2D GetSceneViewExtent(uint64_t id) const override;


	private:
		void SetupVulkan(const char* applicationName, GLFWwindow* window);

		void CreateGraphicsPipeline();
		void CreateCommandPool();
		void CreateCommandBuffers();
		void CreateSyncObjects();

	private:
		void RecreateSwapChain();
		// False while the window is minimized
		bool HasValidFramebufferSize() const;

		// The world's state, once per frame: a new revision when it is set
		void BeginWorldRevision(const Nyx::Registry* registry);
		RenderWorld* FindRenderWorld(const Nyx::Registry* registry);
		const RenderWorld* FindRenderWorld(const Nyx::Registry* registry) const;
		// The world the view shows; null without one
		const RenderWorld* FindShownWorld(const SceneViewInstance& view) const;
		// The revision of the world the view shows; 0 without one
		uint64_t GetShownWorldRevision(const SceneViewInstance& view) const;
		// Every shown world's objects and debug lines; forgets worlds no view shows any more
		void ExtractRenderWorlds();
		// How far into its next step the world is drawn this frame (SetWorldInterpolation()), for its
		// meshes and decals alike: none for a world drawn as it is; paused, 1, the last step's poses
		std::optional<float> GetWorldInterpolation(const Nyx::Registry* registry) const;
		// The world's time this frame (RenderWorld::Time): its clock's at the moment its poses are
		// drawn at, for a world drawn between steps; else SceneTime
		float GetWorldTime(const Nyx::Registry* registry) const;
		void ExtractRenderObjects(RenderWorld& world, std::optional<float> interpolation);
		// Per view, once the worlds are extracted: a size asked for made; whether it is drawn this
		// frame (SetSceneViewDrawnWhenShown()); if so its camera, and what it draws of its world
		// (CullSceneView()); then this frame's instance stream goes up
		void PrepareSceneViews();
		// What the view's camera sees of the world, as its draws (SceneViewInstance::Draws): the
		// entities whose bounds are in view, those drawn instanced cell by cell, and those that draw
		// alike (the same mesh, each section with the same material) as one draw of their
		// transforms in this frame's instance stream; with the view's culling debug, its bounds
		void CullSceneView(SceneViewInstance& view, const RenderWorld& world);
		// The draws of what of the world is inside `frustum`, as CullSceneView() makes them, appended
		// to `outDraws`, with what was culled in `stats`; with `bCastersOnly`, of the objects that
		// cast shadows alone
		void CullDraws(const RenderWorld& world, const Frustum& frustum, bool bCastersOnly, std::vector<SceneViewDraw>& outDraws, SceneViewCullingStats& stats);
		// The view's shadow cascades this frame, if its world's primary light casts shadows and
		// something casts them: each fitted round its slice of the view, snapped to its texels, and
		// what casts into it culled by its own frustum (SceneViewInstance::Shadows)
		void PrepareSceneViewShadows(SceneViewInstance& view, const RenderWorld& world);
		// Each of the view's cascades into its layer of the shadow map, before the view's scene pass
		void DrawShadowCascades(SceneViewInstance& view, const RenderWorld& world, vk::raii::CommandBuffer& cmd);
		// The view's particle draws (SceneViewInstance::Particles): the emitters whose particles are
		// in view, the alpha blended ones farthest first and their particles back to front, then the
		// additive ones, appended to this frame's particle stream; none in a debug view of the
		// materials
		void CullSceneViewParticles(SceneViewInstance& view, const RenderWorld& world, const Frustum& frustum);
		// The view's lightning draws (SceneViewInstance::Lightning): the bolts on and in view, their
		// strips facing its camera appended to this frame's lightning stream; none in a debug view of
		// the materials
		void CullSceneViewLightning(SceneViewInstance& view, const RenderWorld& world, const Frustum& frustum);
		// The world's particles moved on to its time, carried by its air, with each emitter's mesh
		// The world's wind at its time this frame (RenderWorld::Wind)
		void ExtractWind(RenderWorld& world);
		// The view's wind debug lines and its texture of local air, round its camera
		void PrepareSceneViewWind(SceneViewInstance& view, const RenderWorld& world);
		void ExtractParticles(RenderWorld& world, std::optional<float> interpolation);
		// The mesh an emitter's particles are drawn as: its path's, resolved once a world, else the cube
		Nyx::Mesh* FindParticleMesh(RenderWorld& world, const std::string& path);
		// The bounds culled by, as the view's culling debug shows them
		void AddCullingLines(SceneViewInstance& view, const RenderWorld& world, const Frustum& frustum, const glm::mat4& cullingViewProjection);
		// FrameInstances into their buffer, made bigger when they don't fit
		void UploadFrameInstances();
		void DrawRenderObjects(SceneViewInstance& view, const RenderWorld& world, vk::raii::CommandBuffer& cmd);
		// The world's decals, blended over what the scene pass drew: the plane mesh with each one's
		// material's decal pipeline
		void DrawRenderDecals(SceneViewInstance& view, const RenderWorld& world, vk::raii::CommandBuffer& cmd);

		// Set 1, the view's: its SceneUBO, and its point lights
		void CreateSceneDescriptorSetLayout();
		// The layout of every scene pipeline: the global set (Bindless), the view's set, and
		// ObjectConstants
		void CreateScenePipelineLayout();
		// Binds both sets for the scene pipelines' draws into the view
		void BindSceneSets(SceneViewInstance& view, vk::raii::CommandBuffer& cmd);

		// The scene views' scene pass (HDR), for what draws the scene: materials, the sky
		vk::RenderPass GetSceneRenderPass() const;
		// Its samples a pixel, which those pipelines are made for
		vk::SampleCountFlagBits GetSceneSamples() const;
		// The samples MsaaSamplesWanted comes to on this device
		vk::SampleCountFlagBits ChooseSceneSamples() const;
		// What draws into the scene pass, made again for its new sample count: the sky's pipeline
		// and the materials' (waits for the GPU)
		void RebuildScenePipelines();
		// Their picking pass (an id a pixel), for the decals' picking pipelines
		vk::RenderPass GetPickingRenderPass() const;
		// Their picture pass (8-bit, after the tonemap), for what draws over the picture
		vk::RenderPass GetPictureRenderPass() const;
		// The view's HDR picture, its ambient light and its depth into their bindless slots, after
		// the view's targets were made
		void RegisterHdrPicture(SceneViewInstance& view);
		void CreateTonemapPipeline();
		void DrawTonemap(SceneViewInstance& view, vk::raii::CommandBuffer& cmd);

		void LoadSkyboxCubemap();

		void CreateSkyboxPipeline();
		void DrawSkybox(SceneViewInstance& view, vk::raii::CommandBuffer& cmd);

		// A shader's SPIR-V by file name (e.g. "Grid.vert.spv"), from Programs: the last good
		// compile, at first the build's
		std::vector<uint32_t> ReadSpirvFile(const std::string& fileName);

		void CreateSceneResourcesForView(SceneViewInstance& view);
		// The view's set, for the world it shows, or for none: every drawn view's each frame, so that
		// none draws with a slot of the bindless array from a frame before, which may be gone (a
		// bloom chain)
		void UpdateSceneUniforms(SceneViewInstance& view, const RenderWorld* world);

		void CreateTestMeshData();

		// Live reload of the engine's shaders: turned on where the shader sources and
		// NyxShaderCompiler.exe are found
		void StartLiveShaderReload();
		// Once per frame, before recording: logs what compiled, and makes the pipelines of changed
		// shaders again
		void ApplyShaderUpdates();
		// One compile into the Shader Log (ShaderCompiles)
		void RecordCompile(const Nyx::ShaderProgramUpdate& update);

		// The cube of a view's sky, with its light, made at the first ask
		uint32_t GetSkySlot(ESceneViewSky sky);

		void ExtractSceneGlobalsFromEditorCamera(SceneViewInstance& view);
		void ExtractSceneGlobalsFromWorldCamera(SceneViewInstance& view, const Nyx::Registry& registry);
		// The world's primary directional light, and its shadows, whichever camera the view has; the
		// default sun, casting none, without one
		void ExtractSceneLight(SceneViewInstance& view, const Nyx::Registry& registry);
		// The world's first EnvironmentComponent, under the view's light, after it: its fog and
		// sky, and the light as the air leaves it; none without one
		void ExtractSceneEnvironment(SceneViewInstance& view, const Nyx::Registry& registry);
		// The world's first ColorGradingComponent, its grade by the height of the view's light; none
		// without one
		void ExtractSceneColorGrading(SceneViewInstance& view, const Nyx::Registry& registry);
		// The world's first CloudLayerComponent, for the view's camera at the world's time; none
		// without one
		void ExtractSceneCloudLayer(SceneViewInstance& view, const Nyx::Registry& registry);
		// The world's first VolumetricCloudsComponent, for the view's camera at the world's time; none
		// without one
		void ExtractSceneVolumetricClouds(SceneViewInstance& view, const Nyx::Registry& registry);
		void UpdateViewportSceneGlobals(SceneViewInstance& view, const Nyx::Registry& registry);

		// While a frame is recorded (e.g. as the UI is built), a view made shows from its first
		// drawn frame on (WasSceneViewRecreatedThisFrame() says so), and a view let go of is
		// destroyed at the next frame's start: the frame's commands may draw it already, and
		// destroying what they use while they are recorded would break them.
		uint64_t CreateSceneView();
		void DestroySceneView(uint64_t id);
		void DestroySceneViewNow(uint64_t id);
		void SetSceneViewDrawnWhenShown(uint64_t id, bool bWhenShown) override;

		void SetSceneViewSize(uint64_t id, uint32_t width, uint32_t height) override;

		bool WasSceneViewRecreatedThisFrame(uint64_t id) const override;

		ImTextureID GetSceneViewBlurTextureId(uint64_t id) const override;
		Extent2D GetSceneViewBlurExtent(uint64_t id) const override;
		float GetBlurMilliseconds() const override { return UI.GetBlurMilliseconds(); }
		std::vector<uint64_t> GetSceneViewIds() const override;

		SceneViewInstance* FindSceneView(uint64_t id);
		const SceneViewInstance* FindSceneView(uint64_t id) const;

		void EnsureSceneViewSize(SceneViewInstance& view, uint32_t width, uint32_t height);
		void RenderSceneView(SceneViewInstance& view, vk::raii::CommandBuffer& cmd);

		void RequestPick(uint64_t sceneViewId, uint32_t pixelX, uint32_t pixelY);
		Nyx::IEditorRenderer::PickResult ConsumeLastPickResult(uint64_t sceneViewId);

		// After the last frame's fence: every view's pick that frame copied
		void ReadBackPickResults();

	private:
	private:
		GLFWwindow* Window = nullptr;

		VulkanContext Context;
		VulkanSwapchain Swapchain;

		// The engine's shaders by name; every pipeline is made from them. What live reload compiles
		// is kept in Saved/ShaderCache/Engine, so the next start begins from it.
		Nyx::ShaderPrograms Programs{ Nyx::Paths::GetCompiledShadersDir(), Nyx::Paths::GetShadersDir(), Nyx::Paths::GetShaderCacheDir() / "Engine" };

		// ImGui, the painter, glass and blur, UI images, screenshots, and the frame's GPU times
		VulkanUIBackend UI{ Context, Swapchain, Programs };
		// Every view's debug lines, the worlds' debug drawing and the overlays' alike
		VulkanDebugLines DebugLines{ Context, Programs };
		// The grid, debug lines, picking and the selection outline, of the views that show them
		VulkanEditorOverlays Overlays{ Context, Programs, DebugLines };
		// The primary light's shadow map, which every view's cascades are drawn into in turn
		VulkanShadows Shadows{ Context, Programs };
		// Each view's bloom, between its scene pass and its tonemap
		VulkanBloom Bloom{ Context, Programs };
		// Each view's ambient occlusion, right after its scene pass
		VulkanAmbientOcclusion AmbientOcclusion{ Context, Programs };
		// Each view's colour grading table, baked before its picture pass when its grade changed
		VulkanColorGrading ColorGrading{ Context, Programs };
		VulkanLocalWind LocalWind{ Context };
		// Each view's lightning, after its ambient occlusion and before its particles
		VulkanLightning Lightning{ Context, Programs };
		// Each view's particles, after its lightning
		VulkanParticles Particles{ Context, Programs };
		// Each view's light shafts, after its particles, for its tonemap to add
		VulkanLightShafts LightShafts{ Context, Programs };
		// Each view's sky tables, made before its passes when what they depend on changes
		VulkanSkyAtmosphere SkyAtmosphere{ Context, Programs };
		// Each view's high cloud layer, after its sky tables and before its scene pass
		VulkanCloudLayer CloudLayer{ Context, Programs };
		// Each view's volumetric clouds, after its cloud layer and before its scene pass
		VulkanVolumetricClouds VolumetricClouds{ Context, Programs };
		// This frame's particles of every view, in the order they are drawn
		std::vector<ParticleInstanceData> FrameParticles;
		// This frame's lightning strips of every view, in the order they are drawn
		std::vector<LightningVertex> FrameLightning;

		// The GPU times of each view's passes (SceneViewGpuTimes), for the first MaxTimedViews views:
		// a pair of timestamps a pass a view, reset at the frame's start and read after its fence.
		// None without timestamps on the device.
		enum class ETimedPass : uint32_t
		{
			Sky,
			Clouds,
			VolumetricClouds,
			Shadows,
			Scene,
			AmbientOcclusion,
			Lightning,
			Particles,
			LightShafts,
			Bloom,
			Picture,
			ColorGrading,
			Count
		};
		static constexpr uint32_t MaxTimedViews = 8;
		vk::raii::QueryPool PassTimestamps{ nullptr };
		struct TimedPass
		{
			uint64_t ViewId = 0;
			ETimedPass Pass = ETimedPass::Bloom;
			uint32_t FirstQuery = 0;
		};
		std::vector<TimedPass> TimedPasses;
		void CreatePassTimestamps();
		// A timestamp before (bEnd false) or after a pass of the view, if it is one timed
		void WritePassTimestamp(vk::raii::CommandBuffer& cmd, const SceneViewInstance& view, ETimedPass pass, bool bEnd);
		// After the last frame's fence: its passes' times into their views
		void ReadPassTimestamps();

		// The last frame whose GPU work is done (IRenderer::GetFrameStats()), and its views'
		// (SceneViewInstance::Stats). A frame's CPU times for the scene are kept until then, and
		// whether a frame was submitted since.
		RenderFrameStats FrameStats;
		struct SceneTimes
		{
			float Extract = 0.0f;
			float Cull = 0.0f;
			float Record = 0.0f;
		};
		SceneTimes PendingSceneTimes;
		bool bFrameStatsPending = false;
		// After the last frame's fence and its timestamps: its stats made from its CPU times, its
		// views' draws and its GPU times, if it was submitted
		void CompleteFrameStats();

		// When the last frame's DrawFrame() ended, for the time spent outside it
		std::chrono::steady_clock::time_point LastFrameEnd;

		std::vector<std::unique_ptr<Nyx::Mesh>> LoadedMeshes;
		// The worlds' meshes made at run time (RuntimeMeshComponent), uploaded as the worlds are
		// extracted
		Nyx::RuntimeMeshCache RuntimeMeshes;
		// The worlds' instances (MeshInstancesComponent), uploaded so too
		Nyx::MeshInstanceCache MeshInstances;
		// This frame's instance stream of the entities drawn together (CullSceneView()), every view's:
		// their transforms and custom numbers, written after the last frame's fence, while no frame
		// reads it (one frame in flight); the buffers it outgrew are kept until the next fence
		std::vector<MeshInstanceData> FrameInstances;
		vk::raii::Buffer FrameInstanceBuffer{ nullptr };
		vk::raii::DeviceMemory FrameInstanceMemory{ nullptr };
		size_t FrameInstanceCapacity = 0;
		std::vector<std::pair<vk::raii::Buffer, vk::raii::DeviceMemory>> RetiredFrameInstanceBuffers;
		// What a view's culling debug draws (AddCullingLines()), view after view
		Nyx::DebugDraw CullingDrawing;
		std::vector<std::unique_ptr<Nyx::Texture>> LoadedTextures;

		std::vector<SceneViewInstance> SceneViews;
		// The renderer's own view, the first, made as it starts: the scene's pipelines are made from
		// its render passes, for every view. Nobody shows it, so it is never drawn; GetSceneViewIds()
		// leaves it out.
		uint64_t PipelineViewId = 0;
		// Multisample anti-aliasing (SetMsaaSamples()): the samples asked for; those every view
		// draws with now, taken from what was asked as the frame's views are prepared
		// (PrepareSceneViews()); and the counts the device can do for colour and depth alike, one
		// bit each (vk::SampleCountFlags)
		uint32_t MsaaSamplesWanted = Nyx::DefaultMsaaSamples;
		vk::SampleCountFlagBits MsaaSamples = vk::SampleCountFlagBits::e1;
		uint32_t SupportedMsaaSamples = 1;
		// Views let go of while a frame was recorded, destroyed at the next frame's start
		std::vector<uint64_t> SceneViewsToDestroy;
		// Between waiting for the last frame and the end of DrawFrame(): the frame's commands are recorded
		bool bRecordingFrame = false;
		uint64_t NextSceneViewId = 1;

		// Set 0 of every scene pipeline: every texture, the fixed samplers, the material buffer
		Nyx::VulkanBindless Bindless;

		vk::raii::DescriptorSetLayout SceneDescriptorSetLayout{ nullptr };
		// Shared by the scene, sky, grid, debug line, picking and selection mask pipelines
		vk::raii::PipelineLayout ScenePipelineLayout{ nullptr };

		// The meshes' materials, from their .nyxmat files
		Nyx::VulkanMaterials Materials;
		// Every shader's last compile (GetShaderLog())
		Nyx::ShaderLog ShaderCompiles;
		ShaderCompiledListener OnShaderCompiled;
		// Values set for materials (SetMaterialValues()), written once the UI is built: the frame's
		// draws are recorded by then, but run after
		std::map<std::string, Nyx::MaterialAsset> PendingMaterialValues;
		// The sky's light, for physically based materials
		Nyx::VulkanEnvironment Environment;

		// The renderer's world (SetWorld()), owned by the editor / scene document
		const Nyx::Registry* World = nullptr;
		// What the worlds' decals' material paths are resolved by (ExtractSurfaceDecals())
		AssetResolver DecalAssets{ *this };
		// Every world a scene view shows, the renderer's world among them; owned here so that
		// their addresses stay while worlds come and go
		std::vector<std::unique_ptr<RenderWorld>> RenderWorlds;
		uint64_t LastWorldRevision = 0;
		// The worlds' debug drawing (SetWorldDebugDraw()), by world; kept apart from RenderWorlds,
		// whose entries come and go with the views
		std::unordered_map<const Nyx::Registry*, const Nyx::DebugDraw*> WorldDebugDraws;
		// The clocks of the worlds drawn between steps (SetWorldInterpolation()), by world, kept so
		// too; they give those worlds their time as well (GetWorldTime())
		std::unordered_map<const Nyx::Registry*, const Nyx::FixedStepClock*> WorldInterpolationClocks;
		// The air the worlds' games keep on top of their wind (SetWorldWindField()), by world, kept so too
		// The wind's debug view as lines, made again for each view that shows it
		Nyx::DebugDraw WindDrawing;
		std::unordered_map<const Nyx::Registry*, const Nyx::IWindField*> WorldWindFields;

		// Seconds since the renderer was made, this frame: the time of worlds without a clock
		const std::chrono::steady_clock::time_point StartTime = std::chrono::steady_clock::now();
		float SceneTime = 0.0f;

		// HDR to the picture
		vk::raii::Pipeline TonemapPipeline{ nullptr };

		// Skybox
		CubemapTexture SkyboxCubemap{ "Skybox01" };
		uint32_t SkyboxTextureIndex = 0;
		// The studio's and black's skies, for views that show them (SetSceneViewSky()); 0 until then
		uint32_t StudioSkySlot = 0;
		uint32_t BlackSkySlot = 0;

		vk::raii::Pipeline SkyboxPipeline{ nullptr };

		// Mesh
		Mesh CubeMesh = Mesh("Cube");
		Mesh SphereMesh = Mesh("Sphere");
		Mesh CapsuleMesh = Mesh("Capsule");
		Mesh PlaneMesh = Mesh("Plane");
		Mesh CrossedQuadsMesh = Mesh("CrossedQuads");

		// Texture

		//vk::PhysicalDeviceFeatures DeviceFeatures;

		vk::raii::CommandPool CommandPool{ nullptr };
		std::vector<vk::raii::CommandBuffer> CommandBuffers;

		vk::raii::Semaphore ImageAvailableSemaphore{ nullptr };
		std::vector<vk::raii::Semaphore> RenderFinishedSemaphores;
		// Signalled when all of a frame's GPU work is done, including ImGui's OS windows.
		vk::raii::Fence InFlightFence{ nullptr };

		bool bRecreateSwapChain = false;

		// DrawFrame() without its handling of a lost device
		void DrawFrameOnDevice(const std::function<void()>& buildUI);

		// The frame's start, and the times of its steps before the UI, for the painter's statistics
		struct FrameClock
		{
			std::chrono::steady_clock::time_point Start;
			Nyx::UI::PainterWindowStats::Times Times;
		};
		FrameClock StartFrameClock() const;

		// DrawFrameOnDevice()'s steps, in their order
		// Until the last frame's GPU work is done; then the views let go of during it are destroyed
		void WaitForLastFrame(FrameClock& clock);
		// The swapchain image to draw into; none when the swapchain had to be made again
		std::optional<uint32_t> AcquireImage(FrameClock& clock);
		vk::raii::CommandBuffer& BeginCommands(uint32_t imageIndex);
		// Every shown world's state, what each scene view draws of it, the views' editor passes, the
		// views, and their blur
		void RenderScene(vk::raii::CommandBuffer& cmd, FrameClock& clock);
		// Per view drawn this frame: its uniforms, and the editor's own passes: picking, and the
		// selection masks
		void DrawEditorPasses(vk::raii::CommandBuffer& cmd);
		// Values set for materials while the UI was built (SetMaterialValues())
		void ApplyPendingMaterialValues();
		// The frame's command buffer, ImGui's OS windows, and the fence that covers them all
		void Submit(vk::raii::CommandBuffer& cmd, uint32_t imageIndex);
		void Present(uint32_t imageIndex, const FrameClock& clock);
		// Set when the device is lost; nothing is drawn after that (IsDeviceLost())
		bool bDeviceLost = false;
	};
}
