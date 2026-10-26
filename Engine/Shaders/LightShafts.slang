// Light shafts (VulkanLightShafts): the sun's light streaming through the gaps in what stands
// before it, as a blur along lines out from the sun's place on the picture (a screen-space radial
// blur, as Mitchell's in GPU Gems 3), at half the view's size. What may stream is the scene's
// light where the depth says it is the sky (or far enough away), the more of it the nearer the
// sun; each pixel gathers it along its line towards the sun, so a gap's light streams out from it,
// away from the sun, over what stands nearer. Where a high cloud layer lies over the sky, the sky
// streams only as much as the clouds let through (their transmittance along the pixel's ray). The tonemap adds what they gathered, times their
// colour (ViewConstants.LightShaftsColor). Every pass is one triangle over its target; its source
// is in the draw's push constants:
//   - MaterialOffset: the slot of its source (0 for the first, which reads the view's set);
//   - CustomData.xy: a texel of the target, as a part of its size; .zw: its size.

import nyx.bindless;
import nyx.color;
import nyx.view;

// The most light, exposed, a pixel may give: the sun's disk is forty times as bright as a white
// surface it lights, and would drown the shafts from the sky round it
static const float MostLight = 4.0;
// How the light that may stream falls off from the sun: the cosine between a pixel's way and the
// sun's, raised to this (half at about 24 degrees from the sun, a tenth at about 45)
static const float GlowPower = 8.0;

[shader("vertex")]
float4 lightShaftsVertex(uint vertex : SV_VulkanVertexID) : SV_Position
{
    const float2 corner = float2((vertex << 1) & 2, vertex & 2);
    return float4(corner * 2.0 - 1.0, 0.0, 1.0);
}

Texture2D source()
{
    nyx::Texture texture;
    texture.Index = Object.MaterialOffset;
    return texture.get();
}

// A depth of the depth buffer as metres along the view (reversed: MakePerspectiveProjection())
float linearDepth(float depth)
{
    return View.ProjectionInfo.w / max(depth + View.ProjectionInfo.z, 1e-7);
}

// The way through `uv` on the picture, in view space, normalised: the same space for every point
// of the picture, so the cosine between two ways is the one in the world
float3 viewRay(float2 uv)
{
    return normalize(float3((uv * 2.0 - 1.0) * View.ProjectionInfo.xy, 1.0));
}

// 1. The light that may stream, halved: the four pixels' brightness, exposed and held below
// MostLight, each as much as the depth lets through (the sky, which is at the far plane, all of it,
// or as much as the cloud layer lets through; what stands nearer than the threshold none, all of it
// from twice as far), times the glow round the sun
[shader("fragment")]
float lightShaftsPrefilterFragment(float4 position : SV_Position) : SV_Target
{
    const int2 last = int2(View.ViewportSize) - 1;
    const int2 first = int2(position.xy) * 2;
    const float threshold = max(View.LightShaftsSun.w, 1e-3);
    float light = 0.0;
    for (int index = 0; index < 4; ++index)
    {
        const int2 pixel = min(first + int2(index & 1, index >> 1), last);
        const float depth = View.SceneDepth.get().Load(int3(pixel, 0)).r;
        float through = depth <= 0.0 ? 1.0 : smoothstep(threshold, 2.0 * threshold, linearDepth(depth));
        if (depth <= 0.0 && View.CloudLayer != 0)
        {
            through = View.CloudLight.get().Load(int3(pixel, 0)).a;
        }
        const float3 hdr = View.Scene.get().Load(int3(pixel, 0)).rgb;
        light += min(nyx::luminance(max(hdr, 0.0)) * View.ExposureScale, MostLight) * through;
    }
    const float2 uv = position.xy * Object.CustomData.xy;
    const float glow = pow(saturate(dot(viewRay(uv), viewRay(View.LightShaftsSun.xy))), GlowPower);
    return light * 0.25 * glow;
}

// Jimenez's interleaved gradient noise, 0 to 1: neighbours in every direction far apart, so a
// pixel's samples fall between its neighbours' along any line
float gradientNoise(float2 pixel)
{
    return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

// The step between two samples along the line from `uv` towards the sun
float2 sampleStep(float2 uv)
{
    return (View.LightShaftsSun.xy - uv) * View.LightShaftsSun.z / float(max(View.LightShaftsSamples, 1u));
}

// 2. Gathered along the line towards the sun: LightShaftsSamples samples over its density's part of
// the way, the nearest weighing most, each the decay of the one before; their mean by their
// weights. What lies off the picture gives nothing. The line starts a part of a step further on,
// by the noise, so the samples of neighbouring pixels fall between each other's.
[shader("fragment")]
float lightShaftsBlurFragment(float4 position : SV_Position) : SV_Target
{
    const float2 uv = position.xy * Object.CustomData.xy;
    const float2 step = sampleStep(uv);
    const float decay = View.LightShaftsColor.a;
    float2 at = uv + step * gradientNoise(position.xy);
    float weight = 1.0;
    float weights = 0.0;
    float gathered = 0.0;
    for (uint index = 0; index < View.LightShaftsSamples; ++index)
    {
        if (all(at >= 0.0) && all(at <= 1.0))
        {
            gathered += source().SampleLevel(nyx::LinearClamp, at, 0.0).r * weight;
        }
        weights += weight;
        weight *= decay;
        at += step;
    }
    return weights > 0.0 ? gathered / weights : 0.0;
}

// 3. Evened out along the line: four taps over a step of it, each its own turn of the noise, so
// the steps' bands and the noise between them go
[shader("fragment")]
float lightShaftsSmoothFragment(float4 position : SV_Position) : SV_Target
{
    const float2 uv = position.xy * Object.CustomData.xy;
    const float2 step = sampleStep(uv);
    float sum = 0.0;
    for (int index = 0; index < 4; ++index)
    {
        sum += source().SampleLevel(nyx::LinearClamp, uv + step * ((float(index) + 0.5) * 0.25 - 0.5), 0.0).r;
    }
    return sum * 0.25;
}
