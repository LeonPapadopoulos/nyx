// The sky: the view's environment cube map on a cube around the camera, at the far plane, tinted
// as the world's air says, with its glow round the sun, behind the height fog (nyx.atmosphere).
// While the view draws the sky made by the air's scattering, that sky instead: its sky view table
// and the sun's disk through the air (nyx.sky). Ported from Skybox.vert and .frag.

import nyx.atmosphere;
import nyx.bindless;
import nyx.sky;
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
    // At the far plane, whose depth is 0 (reversed: MakePerspectiveProjection())
    const float4 clip = mul(View.SkyboxViewProjection, float4(position, 1.0));
    output.Position = float4(clip.xy, 0.0, clip.w);
    return output;
}

// The scene pass's two targets (nyx.surface's SceneTargets): the sky, and no light for the ambient
// occlusion to dim
struct SkyTargets
{
    float4 Color : SV_Target0;
    float4 Ambient : SV_Target1;
}

float4 skyColor(VertexOutput input)
{
    // Behind a material's debug view, a dark grey that keeps the shape apart: the same on screen
    // whether the view encodes colours (base colour, emission) or shows data as it is
    if (nyx::isMaterialDebugView())
    {
        const bool bColor = View.DebugView == nyx::DebugBaseColor || View.DebugView == nyx::DebugEmission;
        return float4((bColor ? 0.003 : 0.04).xxx, 1.0);
    }
    const float3 direction = normalize(input.Direction);
    if (nyx::hasSkyAtmosphere())
    {
        const float3 light = nyx::skyAtmosphereRadiance(direction) + nyx::sunDiskRadiance(direction);
        return float4(nyx::skyFog(direction).apply(light), 1.0);
    }
    const float4 sky = View.Environment.Sample(nyx::LinearClamp, nyx::skyDirection(direction));
    const float3 color = sky.rgb * nyx::skyTint() + nyx::sunGlow(direction);
    return float4(nyx::skyFog(direction).apply(color), sky.a);
}

[shader("fragment")]
SkyTargets skyboxFragment(VertexOutput input)
{
    SkyTargets targets;
    targets.Color = skyColor(input);
    targets.Ambient = float4(0.0, 0.0, 0.0, 1.0);
    return targets;
}
