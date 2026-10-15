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
    public uint _Padding2;
    public float4 Irradiance[9];
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
