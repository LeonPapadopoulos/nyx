// Set 1: the view a scene is drawn for (Design/shader-materials.md, "Binding"). SceneUBO in
// VulkanRenderer.h; the engine's GLSL shaders declare its first fields as SceneUBO too.

import nyx.bindless;

public struct ViewConstants
{
    public float4x4 ViewProjection;
    public float4x4 InverseViewProjection;
    public float2 ViewportSize;
    public float2 _Padding0;
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
    public uint _Padding1;
}
[vk::binding(0, 1)] public ConstantBuffer<ViewConstants> View;
