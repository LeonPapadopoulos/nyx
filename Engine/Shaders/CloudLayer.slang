// The high cloud layer (VulkanCloudLayer; Docs/Design/atmosphere.md), per pixel of the view, at its
// size, before its scene pass: each pixel's ray from the camera meets the layer's shell (nyx.clouds)
// once from below it, up to twice from above it (in, then out on its far side), never behind the
// ground. At each meeting, the clouds there, from the noise (CloudNoise.slang) at the pattern's
// place as the wind has carried it: cloudlets or streaks by the type, warped, broken up at their
// edges, thresholded by the coverage. They are lit by the sun's light at the layer's height
// (through the air, from the transmittance table), shadowed by the cloud towards the sun and, from
// the side the sun doesn't light, by their own thickness; their thin edges glow against the sun;
// the sky's light (its irradiance cube) lights them too, and at night alone. They fade into the
// air between them and the camera (its transmittance, from the same table), and near the camera.
// Written: their light along the ray (rgb) and the part of the light from behind them that gets
// through (a), which the sky, the surfaces beyond the shell and the light shafts read. One
// triangle over the target, with the scene pipelines' layout. The clouds' math is nyx.clouds'
// (cloudLayerAlong()), which the sky's cube draws them with too.

import nyx.bindless;
import nyx.clouds;
import nyx.sky;
import nyx.view;

[shader("vertex")]
float4 cloudLayerVertex(uint vertex : SV_VulkanVertexID) : SV_Position
{
    const float2 corner = float2((vertex << 1) & 2, vertex & 2);
    return float4(corner * 2.0 - 1.0, 0.0, 1.0);
}

// The way a pixel looks, normalised: through the view's rotation and projection without its place,
// so as precise far from the world's middle as near it
float3 pixelRay(float2 pixel)
{
    const float2 ndc = pixel / View.ViewportSize * 2.0 - 1.0;
    const float4 point = mul(View.CloudInverseSkyViewProjection, float4(ndc, 1.0, 1.0));
    return normalize(point.xyz / point.w);
}

// The clouds along the pixel's ray, nearest first: their light (rgb), and what of the light from
// behind them gets through (a)
[shader("fragment")]
float4 cloudLayerFragment(float4 position : SV_Position) : SV_Target
{
    const float3 direction = pixelRay(position.xy);
    return nyx::cloudLayerAlong(nyx::viewCloudParams(), direction, ddx(direction), ddy(direction));
}
