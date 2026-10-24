// Set 1: the view a scene is drawn for. SceneUBO in VulkanRenderer.h; the engine's GLSL shaders
// declare its first fields as SceneUBO too.

import nyx.bindless;

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
    // colour, tinted as the sky is, and its density a metre at its height (a); 0: no fog
    public float4 FogColor;
    // How fast it thins upwards, its height, how far from the camera it starts, its most opacity
    public float4 FogShape;
    // The sun's light it scatters towards the camera, and how much round the sun (a, g)
    public float4 FogSun;
    // What the sky and its light are multiplied by: the sun's hue and the daylight; white without
    // an environment
    public float4 SkyTint;
    // The sky's glow round the sun, and how much it gathers round it (a, g)
    public float4 SunGlow;

    // The view's bloom (VulkanBloom), exposed light already: its chain's first level, half the
    // view's size, 0 without bloom; and how much of it the tonemap adds
    public nyx::Texture Bloom;
    public float BloomIntensity;
    public uint BloomPadding0;
    public uint BloomPadding1;
}
[vk::binding(0, 1)] public ConstantBuffer<ViewConstants> View;

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
