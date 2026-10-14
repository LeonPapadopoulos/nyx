// A scene view's HDR picture, exposed and tonemapped into the 8-bit picture the editor shows
// (sRGB-encoded). One triangle over the view; the editor's overlays are drawn over it afterwards.

import nyx.bindless;
import nyx.color;
import nyx.view;

[shader("vertex")]
float4 tonemapVertex(uint vertex : SV_VulkanVertexID) : SV_Position
{
    const float2 corner = float2((vertex << 1) & 2, vertex & 2);
    return float4(corner * 2.0 - 1.0, 0.0, 1.0);
}

[shader("fragment")]
float4 tonemapFragment(float4 position : SV_Position) : SV_Target
{
    const float3 hdr = View.Scene.get().Load(int3(int2(position.xy), 0)).rgb;
    const uint debug = View.DebugView;
    // A material's output as it is, neither exposed nor tonemapped: colours sRGB-encoded, data
    // (normals, roughness, ...) as stored, so that 0.5 shows as 128
    if (debug == nyx::DebugBaseColor || debug == nyx::DebugEmission)
    {
        return float4(nyx::linearToSrgb(saturate(hdr)), 1.0);
    }
    if (nyx::isMaterialDebugView())
    {
        return float4(saturate(hdr), 1.0);
    }

    const float3 scene = hdr * View.ExposureScale;
    const float3 picture = nyx::linearToSrgb(nyx::tonemapPbrNeutral(scene));
    // Where the light went wrong, over the picture in grey
    const float3 grey = dot(picture, float3(0.2126, 0.7152, 0.0722)) * 0.6;
    if (debug == nyx::DebugNanInf)
    {
        return any(isnan(hdr)) || any(isinf(hdr)) ? float4(1.0, 0.0, 1.0, 1.0) : float4(grey, 1.0);
    }
    if (debug == nyx::DebugOutOfRange)
    {
        if (any(scene > 1.0))
        {
            return float4(1.0, 0.15, 0.1, 1.0);
        }
        return any(scene < 0.0) ? float4(0.1, 0.35, 1.0, 1.0) : float4(grey, 1.0);
    }
    return float4(picture, 1.0);
}
