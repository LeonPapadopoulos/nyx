// Set 1: the view a scene is drawn for. SceneUBO in VulkanRenderer.h; the engine's GLSL shaders
// declare its first fields as SceneUBO too. Its second binding is the view's point lights.

import nyx.bindless;
import nyx.wind;

public struct ViewConstants
{
    public float4x4 ViewProjection;
    public float4x4 InverseViewProjection;
    public float2 ViewportSize;
    // The sky's turn about the up axis: its cosine and sine (nyx::skyDirection())
    public float2 SkyRotation;
    // xyz
    public float4 CameraPosition;
    // xyz: towards the light, normalised
    public float4 LightDirection;
    // rgb: the light's colour; a: the ambient light
    public float4 LightColor;
    // The view's rotation without its position, for the sky
    public float4x4 SkyboxViewProjection;
    // The sky's cube map
    public nyx::Cubemap Environment;
    // The view's scene in HDR, for the tonemap
    public nyx::Texture Scene;
    // What the scene's light is multiplied by before tonemapping: 2 to the exposure in stops
    public float ExposureScale;
    // What the view shows: the lit scene, or one of the Debug* views below (ESceneViewDebug)
    public uint DebugView;
    // The environment's light (VulkanEnvironment; nyx.environment reads it): the specular cube,
    // its mips (roughness 0 to 1), the BRDF table, the irradiance's nine coefficients
    public nyx::Cubemap Specular;
    public uint SpecularLevels;
    public nyx::Texture BrdfLookup;
    // Seconds, for what moves by itself, such as a decal's pulse: the view's world's time. A
    // game's world's is its simulation's, which stands still while paused; the editor's runs on.
    public float Time;
    public float4 Irradiance[9];

    // The primary light's shadows (VulkanShadows; nyx.shadows reads them). Each cascade's light
    // view and projection, from the world to its layer of the shadow map: x and y -1 to 1 over the
    // part of the layer the view drew, depth reversed, 1 nearest the light.
    public float4x4 ShadowViewProjection[4];
    // Where each cascade ends, in metres from the camera along its view
    public float4 ShadowSplits;
    // How wide a texel of each cascade is in the world, in metres
    public float4 ShadowTexelSizes;
    // xyz: the way the camera looks, normalised
    public float4 CameraForward;
    // The shadow map: a 2D texture array, a layer a cascade
    public nyx::TextureArray ShadowMap;
    // How many cascades the view has; 0: no shadows
    public uint ShadowCascadeCount;
    // A texel of the map, as a part of its width; the part of each layer the view drew into
    public float ShadowMapTexel;
    public float ShadowMapScale;
    // The light's settings: texels around a point filtered together (PCF), how far a point is
    // moved towards the light and along its normal before it is looked up, in its cascade's
    // texels, and how far from the camera shadows reach, in metres
    public float ShadowSoftness;
    public float ShadowDepthBias;
    public float ShadowNormalBias;
    public float ShadowDistance;
    // ESceneViewShadowDebug: 1 tints by cascade
    public uint ShadowDebug;
    // To a whole 16 bytes, as std140 and scalar layout agree on
    public uint ShadowPadding0;
    public uint ShadowPadding1;
    public uint ShadowPadding2;

    // The world's air (EnvironmentComponent; nyx.atmosphere reads it). The height fog's own
    // colour, tinted as the sky is (under the air's sky, what the sky's colour is multiplied by for
    // it: the haze's tint and brightness), and its density a metre at its height (a); 0: no fog
    public float4 FogColor;
    // How fast it thins upwards, its height, how far from the camera it starts, its most opacity
    public float4 FogShape;
    // The sun's light it scatters towards the camera, and how much round the sun (a, g)
    public float4 FogSun;
    // What the sky and its light are multiplied by: the sun's hue and the daylight; white without
    // an environment. a: what the air's sky's light on surfaces is multiplied by (1 otherwise)
    public float4 SkyTint;
    // The sky's glow round the sun, and how much it gathers round it (a, g)
    public float4 SunGlow;

    // The sky made by the air's scattering (VulkanSkyAtmosphere; nyx.sky reads it): 1 while the
    // view draws it, else 0 and nothing below is set. Its sky view and transmittance tables; its
    // light, diffuse and specular (a roughness a mip); the planet's and the top of the air's radii
    // in kilometres; the camera's height above the ground the tables were made for, in kilometres
    // (x); the sun's disk's light before the air, and the cosine of its angular radius (a)
    public uint SkyAtmosphere;
    public nyx::Texture SkyViewLut;
    public nyx::Texture TransmittanceLut;
    public nyx::Cubemap SkyIrradiance;
    public nyx::Cubemap SkySpecular;
    public uint SkySpecularLevels;
    public float PlanetRadius;
    public float AtmosphereTopRadius;
    public float4 SkyCamera;
    public float4 SunDisk;

    // The view's bloom (VulkanBloom), exposed light already: its chain's first level, half the
    // view's size, 0 without bloom; and how much of it the tonemap adds
    public nyx::Texture Bloom;
    public float BloomIntensity;
    public uint BloomPadding0;
    public uint BloomPadding1;

    // The view's ambient occlusion (VulkanAmbientOcclusion; nyx.surface, AmbientOcclusion.slang):
    // 1 while the scene pass writes the sky's light apart for it; that light, the scene's depth,
    // the occlusion and its depth in metres at half size
    public uint AmbientOcclusion;
    public nyx::Texture AmbientLight;
    public nyx::Texture SceneDepth;
    public nyx::Texture Occlusion;
    public nyx::Texture OcclusionDepth;
    // How far round a point it looks, in metres; the power its visibility is raised to; the slices
    // round the view and the steps out along each side of one
    public float OcclusionRadius;
    public float OcclusionIntensity;
    public uint OcclusionSlices;
    public uint OcclusionSteps;
    public uint OcclusionPadding0;
    public uint OcclusionPadding1;
    public uint OcclusionPadding2;
    // From the picture to view space: 1 / the projection's [0][0] and [1][1]; and from a depth of
    // the depth buffer to metres along the view (reversed): [3][2] / (depth + [2][2]), z and w here
    public float4 ProjectionInfo;

    // How many of PointLights the view's pixels are lit by (CullPointLights() in PointLights.h)
    public uint PointLightCount;
    public uint PointLightPadding0;
    public uint PointLightPadding1;
    public uint PointLightPadding2;

    // The view's colour grading (VulkanColorGrading): its lookup table, 0 without grading. The
    // tonemapped picture, sRGB-encoded, is looked up in it (Tonemap.slang).
    public nyx::Volume ColorGradingTable;
    public uint ColorGradingPadding0;
    public uint ColorGradingPadding1;
    public uint ColorGradingPadding2;

    // The view's light shafts (VulkanLightShafts; LightShafts.slang, Tonemap.slang): what they
    // gathered at half the view's size, exposed light, 0 without them; the samples along each
    // pixel's line to the sun. The sun's place on the picture (xy, 0 to 1 across it), how far
    // towards it the lines reach (z, 0 to 1 of the way), and from how far away, in metres, what
    // stands before it lets its light through (w). The light the shafts add a unit of what they
    // gathered (rgb), and what each sample keeps of the one before it (a).
    public nyx::Texture LightShafts;
    public uint LightShaftsSamples;
    public uint LightShaftsPadding0;
    public uint LightShaftsPadding1;
    public float4 LightShaftsSun;
    public float4 LightShaftsColor;

    // The view's high cloud layer (VulkanCloudLayer; nyx.clouds, CloudLayer.slang): 1 while it is
    // drawn, else 0 and nothing below is set. Its light along each pixel's ray and the part of the
    // light from behind it that gets through (a), at the view's size; the noise it is made from.
    // Its altitude and thickness in kilometres, the
    // noise's square in kilometres over it, the shadow it throws on itself (w). Its coverage, type,
    // edges' softness and detail. The wind's way in the world's x and z, the warp, the rim. Where
    // the camera is over the pattern, in squares along the wind and across it, its brightness and
    // the sky's light on it. The sun's light at the top of the air. The view's rotation and
    // projection, without its place, inverted: from the picture to the way each pixel looks.
    public uint CloudLayer;
    public nyx::Texture CloudLight;
    public nyx::Texture CloudNoise;
    public uint CloudPadding;
    public float4 CloudShell;
    public float4 CloudShape;
    public float4 CloudWind;
    public float4 CloudOrigin;
    public float4 CloudSun;
    public float4x4 CloudInverseSkyViewProjection;

    // The view's volumetric clouds (VulkanVolumetricClouds; nyx.volumetricclouds,
    // VolumetricClouds.slang): 1 while they are drawn, else 0 and nothing below is set. Their light
    // along each pixel's ray and the part of the light from behind them that gets through (a), at
    // half the view's size, as this frame leaves them, and how far along the ray they are, in
    // kilometres. What this frame marched (x: 1 for every pixel, else one of each 4 x 4; the debug
    // view from the 8th bit up), and what it marched into; the frame before's light and depth; the
    // noises (the shapes, the details, the weather map). The steps along a ray and towards the sun,
    // and which pixel of each 4 x 4 this frame marches. The band's bottom and top, the farthest
    // marched (kilometres) and the extinction a kilometre where they are densest. The coverage, its
    // variation, the type, its variation. The details, billowiness, edges' softness, brightness.
    // 1 / each noise's repeat in kilometres (the weather map's, the shapes', the details') and
    // the sky's light on them. Where the camera is over the weather map and the shapes (in
    // repeats), over the details, and the half size. The silver lining, its spread, the powder,
    // the multiple scattering. The shadow tint. The sun's light at the top of the air. How far the
    // camera moved, less how far the wind carried the clouds, since the frame before (kilometres),
    // and the frame before's rotation and projection without its place.
    public uint VolumetricClouds;
    public nyx::Texture VolumetricCloudLight;
    public nyx::Texture VolumetricCloudDepth;
    public uint VolumetricCloudMode;
    public nyx::Texture VolumetricCloudMarchLight;
    public nyx::Texture VolumetricCloudMarchDepth;
    public nyx::Texture VolumetricCloudPreviousLight;
    public nyx::Texture VolumetricCloudPreviousDepth;
    public nyx::Volume VolumetricCloudShape;
    public nyx::Volume VolumetricCloudDetail;
    public nyx::Texture VolumetricCloudWeather;
    public uint VolumetricCloudSteps;
    public uint VolumetricCloudLightSteps;
    public uint VolumetricCloudUpdateX;
    public uint VolumetricCloudUpdateY;
    public uint VolumetricCloudPadding;
    public float4 VolumetricCloudBand;
    public float4 VolumetricCloudCoverage;
    public float4 VolumetricCloudErosion;
    public float4 VolumetricCloudRepeats;
    public float4 VolumetricCloudOrigins;
    public float4 VolumetricCloudDetailOrigin;
    public float4 VolumetricCloudLighting;
    public float4 VolumetricCloudShadowTint;
    public float4 VolumetricCloudSun;
    public float4 VolumetricCloudMotion;
    public float4x4 VolumetricCloudPreviousSkyViewProjection;

    // The world's wind (nyx.wind; Wind.h), at Time: calm without one. Its texture of a game's local
    // air round the camera (VulkanLocalWind; nyx.worldwind reads both): 0 without one; the corner of
    // its first cell and 1 / a cell's size (w); 1 / its size in metres along each axis.
    public WindState Wind;
    public nyx::Volume LocalWind;
    public uint LocalWindPadding0;
    public uint LocalWindPadding1;
    public uint LocalWindPadding2;
    public float4 LocalWindOrigin;
    public float4 LocalWindScale;
}
[vk::binding(0, 1)] public ConstantBuffer<ViewConstants> View;

// A point light as the view's pixels read it (ViewPointLight in PointLights.h): where it is, 1 / its
// range squared, its colour times its intensity
public struct PointLight
{
    public float3 Position;
    public float InverseRangeSquared;
    public float3 Color;
    public uint Padding;
}
// The point lights that reach the view, View.PointLightCount of them (nyx::lightSurfaceApart())
[vk::binding(1, 1)] public StructuredBuffer<PointLight> PointLights;

namespace nyx
{
    // ViewConstants.DebugView: ESceneViewDebug in SceneViewTypes.h
    public static const uint DebugLit = 0;
    public static const uint DebugBaseColor = 1;
    public static const uint DebugNormal = 2;
    public static const uint DebugRoughness = 3;
    public static const uint DebugMetallic = 4;
    public static const uint DebugOcclusion = 5;
    public static const uint DebugEmission = 6;
    public static const uint DebugOpacity = 7;
    public static const uint DebugUvChecker = 8;
    public static const uint DebugNanInf = 9;
    public static const uint DebugOutOfRange = 10;
    public static const uint DebugBloom = 11;
    public static const uint DebugAmbientOcclusion = 12;

    // Whether the view shows one output of its materials, not light
    public bool isMaterialDebugView()
    {
        return View.DebugView >= DebugBaseColor && View.DebugView <= DebugUvChecker;
    }

    // Where a direction of the world looks in the sky's cube: the sky turned about the up axis
    // by the view's rotation
    public float3 skyDirection(float3 d)
    {
        const float2 r = View.SkyRotation;
        return float3(r.x * d.x - r.y * d.z, d.y, r.y * d.x + r.x * d.z);
    }
}
