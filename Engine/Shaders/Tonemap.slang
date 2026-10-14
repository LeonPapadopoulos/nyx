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
    const float3 scene = View.Scene.get().Load(int3(int2(position.xy), 0)).rgb * View.ExposureScale;
    return float4(nyx::linearToSrgb(nyx::tonemapPbrNeutral(scene)), 1.0);
}
