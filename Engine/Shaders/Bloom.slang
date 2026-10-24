// Bloom (VulkanBloom): a view's HDR scene, its bright parts, halved level by level, then each level
// mixed back into the one above, so the light of what is brightest spreads wide round it. Every
// pass is one triangle over its level. Its values are in the draw's push constants:
//   - MaterialOffset: the source's slot in the bindless array;
//   - CustomData.xy: a texel of the source, as a part of its size; .zw: of the target;
//   - CustomData2: the threshold and the knee (bloomPrefilterFragment), the radius
//     (bloomUpsampleFragment).

import nyx.bindless;
import nyx.color;
import nyx.view;

[shader("vertex")]
float4 bloomVertex(uint vertex : SV_VulkanVertexID) : SV_Position
{
    const float2 corner = float2((vertex << 1) & 2, vertex & 2);
    return float4(corner * 2.0 - 1.0, 0.0, 1.0);
}

float3 sampleSource(float2 uv)
{
    nyx::Texture source;
    source.Index = Object.MaterialOffset;
    return source.SampleLevel(nyx::LinearClamp, uv, 0.0).rgb;
}

// Where the target's pixel's middle lies on the source, 0 to 1
float2 targetUv(float4 position)
{
    return position.xy * Object.CustomData.zw;
}

// The part of a colour that blooms: none below the threshold, all of what is above it, and
// between, over the knee on either side, a smooth start (a quadratic, as Unity's bloom does)
float3 brightPart(float3 color, float threshold, float knee)
{
    const float brightness = max(color.r, max(color.g, color.b));
    const float soft = clamp(brightness - threshold + knee, 0.0, 2.0 * knee);
    const float curve = soft * soft / (4.0 * knee + 1e-5);
    return color * (max(curve, brightness - threshold) / max(brightness, 1e-5));
}

// Thirteen taps a source texel and two apart round the pixel, as five overlapping groups of
// four, the middle one weighed half and the four corner ones an eighth each (Jimenez 2014): a
// halving without the flicker of a plain 2 x 2 average
struct Groups
{
    float3 Middle;
    float3 Corners[4];
}

Groups gatherGroups(float2 uv, float2 texel)
{
    const float3 a = sampleSource(uv + texel * float2(-2.0, -2.0));
    const float3 b = sampleSource(uv + texel * float2(0.0, -2.0));
    const float3 c = sampleSource(uv + texel * float2(2.0, -2.0));
    const float3 d = sampleSource(uv + texel * float2(-1.0, -1.0));
    const float3 e = sampleSource(uv + texel * float2(1.0, -1.0));
    const float3 f = sampleSource(uv + texel * float2(-2.0, 0.0));
    const float3 g = sampleSource(uv);
    const float3 h = sampleSource(uv + texel * float2(2.0, 0.0));
    const float3 i = sampleSource(uv + texel * float2(-1.0, 1.0));
    const float3 j = sampleSource(uv + texel * float2(1.0, 1.0));
    const float3 k = sampleSource(uv + texel * float2(-2.0, 2.0));
    const float3 l = sampleSource(uv + texel * float2(0.0, 2.0));
    const float3 m = sampleSource(uv + texel * float2(2.0, 2.0));
    Groups groups;
    groups.Middle = (d + e + i + j) * 0.25;
    groups.Corners[0] = (a + b + f + g) * 0.25;
    groups.Corners[1] = (b + c + g + h) * 0.25;
    groups.Corners[2] = (f + g + k + l) * 0.25;
    groups.Corners[3] = (g + h + l + m) * 0.25;
    return groups;
}

// The scene, exposed as the tonemap exposes it, its bright parts, halved into the first level.
// Each group is weighed down by its brightness (Karis's average), so a few very bright pixels
// spread as wide as a dimmer patch rather than flickering as they move.
[shader("fragment")]
float4 bloomPrefilterFragment(float4 position : SV_Position) : SV_Target
{
    const Groups groups = gatherGroups(targetUv(position), Object.CustomData.xy);
    const float threshold = Object.CustomData2.x;
    const float knee = max(Object.CustomData2.y, 1e-4);
    const float3 middle = brightPart(groups.Middle * View.ExposureScale, threshold, knee);
    const float middleWeight = 0.5 / (1.0 + nyx::luminance(middle));
    float3 sum = middle * middleWeight;
    float weights = middleWeight;
    for (int corner = 0; corner < 4; ++corner)
    {
        const float3 part = brightPart(groups.Corners[corner] * View.ExposureScale, threshold, knee);
        const float weight = 0.125 / (1.0 + nyx::luminance(part));
        sum += part * weight;
        weights += weight;
    }
    return float4(sum / weights, 1.0);
}

// A level halved into the next
[shader("fragment")]
float4 bloomDownsampleFragment(float4 position : SV_Position) : SV_Target
{
    const Groups groups = gatherGroups(targetUv(position), Object.CustomData.xy);
    const float3 corners = groups.Corners[0] + groups.Corners[1] + groups.Corners[2] + groups.Corners[3];
    return float4(groups.Middle * 0.5 + corners * 0.125, 1.0);
}

// The level below, filtered up with a cubic B-spline over its 4 x 4 texels round the pixel, in four
// bilinear taps (Sigg and Hadwiger): smooth where bilinear filtering would show the small levels'
// texels as creases and squares in a wide glow, and a little blurred. Mixed into this level by the
// radius (the pipeline blends by the alpha).
[shader("fragment")]
float4 bloomUpsampleFragment(float4 position : SV_Position) : SV_Target
{
    const float2 texel = Object.CustomData.xy;
    // In the source's texels, texel k's middle at k
    const float2 at = targetUv(position) / texel - 0.5;
    const float2 corner = floor(at);
    const float2 f = at - corner;
    const float2 w0 = (1.0 - f) * (1.0 - f) * (1.0 - f) / 6.0;
    const float2 w1 = (3.0 * f * f * f - 6.0 * f * f + 4.0) / 6.0;
    const float2 w2 = (-3.0 * f * f * f + 3.0 * f * f + 3.0 * f + 1.0) / 6.0;
    const float2 w3 = f * f * f / 6.0;
    // Two pairs of texels each way, each pair one bilinear tap between them
    const float2 near = w0 + w1;
    const float2 far = w2 + w3;
    const float2 nearUv = (corner - 1.0 + w1 / near + 0.5) * texel;
    const float2 farUv = (corner + 1.0 + w3 / far + 0.5) * texel;
    const float3 sum = (sampleSource(nearUv) * near.x + sampleSource(float2(farUv.x, nearUv.y)) * far.x) * near.y
        + (sampleSource(float2(nearUv.x, farUv.y)) * near.x + sampleSource(farUv) * far.x) * far.y;
    return float4(sum, saturate(Object.CustomData2.x));
}
