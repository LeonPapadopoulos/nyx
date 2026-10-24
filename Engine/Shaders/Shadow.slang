// The primary light's shadow maps (VulkanShadows): what casts shadows, as the light sees it,
// into the map's layer of a cascade, depth only. The cascade is the draw's CustomData2.x; its
// light view and projection are the view's (ViewConstants.ShadowViewProjection). The shadow debug
// view shows each layer in a square of the picture.

import nyx.surface;

[shader("vertex")]
float4 shadowVertex(VertexInput input) : SV_Position
{
    // The draw's transform after the instance's, as transformSurfaceVertex() places a mesh
    const float4x4 instance = transpose(float4x4(input.Instance0, input.Instance1, input.Instance2, input.Instance3));
    const float4x4 model = mul(Object.Model, instance);
    const uint cascade = min(uint(Object.CustomData2.x), nyx::MaxShadowCascades - 1);
    return mul(View.ShadowViewProjection[cascade], mul(model, float4(input.Position, 1.0)));
}

struct DebugVertexOutput
{
    float4 Position : SV_Position;
    float2 UV : TEXCOORD0;
}

// Two triangles over a square of the picture, from CustomData's xy to its zw in clip space
static const float2 DebugCorners[6] = { float2(0.0, 0.0), float2(1.0, 0.0), float2(1.0, 1.0), float2(0.0, 0.0), float2(1.0, 1.0), float2(0.0, 1.0) };

[shader("vertex")]
DebugVertexOutput shadowDebugVertex(uint vertex : SV_VulkanVertexID)
{
    DebugVertexOutput output;
    output.UV = DebugCorners[vertex % 6];
    output.Position = float4(lerp(Object.CustomData.xy, Object.CustomData.zw, output.UV), 0.0, 1.0);
    return output;
}

// The cascade's layer, as much of it as the view drew, its depth as grey, white nearest the light,
// framed in the cascade's tint
[shader("fragment")]
float4 shadowDebugFragment(DebugVertexOutput input) : SV_Target
{
    const uint cascade = min(uint(Object.CustomData2.x), nyx::MaxShadowCascades - 1);
    const float2 frame = min(input.UV, 1.0 - input.UV);
    if (min(frame.x, frame.y) < 0.015)
    {
        return float4(nyx::shadowCascadeTint(cascade), 1.0);
    }
    const float depth = View.ShadowMap.SampleLevel(nyx::PointClamp, float3(input.UV * View.ShadowMapScale, float(cascade)), 0.0).r;
    return float4(saturate(depth).xxx, 1.0);
}
