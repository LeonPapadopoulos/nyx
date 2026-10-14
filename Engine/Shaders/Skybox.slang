// The sky: the view's environment cube map on a cube around the camera, at the far plane.
// Ported from Skybox.vert and .frag.

import nyx.bindless;
import nyx.view;

struct VertexOutput
{
    float4 Position : SV_Position;
    float3 Direction : DIRECTION;
}

[shader("vertex")]
VertexOutput skyboxVertex(float3 position : POSITION)
{
    VertexOutput output;
    output.Direction = position;
    // At the far plane: depth w / w
    output.Position = mul(View.SkyboxViewProjection, float4(position, 1.0)).xyww;
    return output;
}

[shader("fragment")]
float4 skyboxFragment(VertexOutput input) : SV_Target
{
    // Behind a material's debug view, a dark grey that keeps the shape apart: the same on screen
    // whether the view encodes colours (base colour, emission) or shows data as it is
    if (nyx::isMaterialDebugView())
    {
        const bool bColor = View.DebugView == nyx::DebugBaseColor || View.DebugView == nyx::DebugEmission;
        return float4((bColor ? 0.003 : 0.04).xxx, 1.0);
    }
    return View.Environment.Sample(nyx::LinearClamp, nyx::skyDirection(normalize(input.Direction)));
}
