// Ambient occlusion (VulkanAmbientOcclusion): how much of the sky's light reaches each pixel, from
// the scene's depth, at half the view's size, then applied to the sky's light alone, which the
// scene pass wrote apart (ViewConstants.AmbientLight). Ground truth ambient occlusion (Jimenez et
// al. 2016, after Intel's XeGTAO): for a few slices round the view direction, the horizons on both
// sides found by stepping out across the screen, and the cosine-weighted visible arc between them
// integrated exactly. Every pass is one triangle over its target; its sources are in the draw's
// push constants:
//   - MaterialOffset, EntityId: the slots of its first and second source;
//   - CustomData.xy: a texel of the first source, as a part of its size; .zw: of the target;
//   - CustomData2: the blur's direction, in texels (occlusionBlurFragment).

import nyx.bindless;
import nyx.view;

static const float Pi = 3.14159265358979;
static const float HalfPi = 1.57079632679490;

[shader("vertex")]
float4 occlusionVertex(uint vertex : SV_VulkanVertexID) : SV_Position
{
    const float2 corner = float2((vertex << 1) & 2, vertex & 2);
    return float4(corner * 2.0 - 1.0, 0.0, 1.0);
}

Texture2D firstSource()
{
    nyx::Texture texture;
    texture.Index = Object.MaterialOffset;
    return texture.get();
}

Texture2D secondSource()
{
    nyx::Texture texture;
    texture.Index = Object.EntityId;
    return texture.get();
}

// A depth of the depth buffer as metres along the view (reversed: MakePerspectiveProjection())
float linearDepth(float depth)
{
    return View.ProjectionInfo.w / max(depth + View.ProjectionInfo.z, 1e-7);
}

// The point of view space (x right, y up, z ahead: left-handed, as XeGTAO's) at `uv` on the
// picture, `depth` metres along the view
float3 viewPosition(float2 uv, float depth)
{
    const float2 ndc = uv * 2.0 - 1.0;
    return float3(ndc * View.ProjectionInfo.xy * depth, depth);
}

// 1. The depth, halved: each half-size pixel the depth of the top left of its four, as metres
// along the view, so that the occlusion and the blur work in metres. One of the four, not their
// mean: a mean across an edge is a depth nothing is at.
[shader("fragment")]
float occlusionDepthFragment(float4 position : SV_Position) : SV_Target
{
    const int2 texel = int2(position.xy) * 2;
    return linearDepth(firstSource().Load(int3(texel, 0)).r);
}

// The half-size depth at `uv`, its texel's own
float depthAt(float2 uv)
{
    return firstSource().SampleLevel(nyx::PointClamp, uv, 0.0).r;
}

// The point of view space a half-size texel's depth was taken at: its top left full-size pixel's
// middle, a quarter of a texel up and left of its own (a slope seen from the texel's middle would
// lean, and an open plane occlude itself)
float3 halfSizePosition(float2 uv, float2 texel)
{
    return viewPosition(uv - 0.25 * texel, depthAt(uv));
}

// A pixel's place in a 4 x 4 tile, mixed so that neighbours are far apart in it: the noise that
// turns each pixel's slices and steps, which the blur (4 wide) evens out
uint tileIndex(uint2 pixel)
{
    static const uint Order[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
    return Order[(pixel.y & 3) * 4 + (pixel.x & 3)];
}

// 2. The occlusion at half size: 1 where all of the sky above the surface is seen, less where
// what is nearby hides it
[shader("fragment")]
float occlusionFragment(float4 position : SV_Position) : SV_Target
{
    const float2 texel = Object.CustomData.zw;
    const float2 uv = position.xy * texel;
    const float centerDepth = depthAt(uv);
    // The sky, or too far for its radius to cover a pixel: nothing occludes it
    const float3 center = halfSizePosition(uv, texel);
    const float pixelSize = 2.0 * View.ProjectionInfo.x * centerDepth * texel.x;
    const float screenRadius = View.OcclusionRadius / max(pixelSize, 1e-6);
    if (centerDepth >= View.ProjectionInfo.w / max(View.ProjectionInfo.z, 1e-7) * 0.999 || screenRadius < 1.0)
    {
        return 1.0;
    }

    // The surface's normal from the depth: across and up, the neighbour each way whose depth is
    // nearer the pixel's, so that a step at an edge isn't taken for a slope
    const float3 left = halfSizePosition(uv - float2(texel.x, 0.0), texel);
    const float3 right = halfSizePosition(uv + float2(texel.x, 0.0), texel);
    const float3 top = halfSizePosition(uv - float2(0.0, texel.y), texel);
    const float3 bottom = halfSizePosition(uv + float2(0.0, texel.y), texel);
    const float3 across = abs(right.z - center.z) < abs(center.z - left.z) ? right - center : center - left;
    const float3 up = abs(top.z - center.z) < abs(center.z - bottom.z) ? top - center : center - bottom;
    const float3 normal = normalize(cross(up, across));
    const float3 toCamera = normalize(-center);

    // What lies beyond the radius counts less and less, over its outer part
    const float falloffRange = 0.615 * View.OcclusionRadius;
    const float falloffMul = -1.0 / falloffRange;
    const float falloffAdd = (View.OcclusionRadius - falloffRange) / falloffRange + 1.0;

    const uint index = tileIndex(uint2(position.xy));
    const float sliceNoise = frac(0.5 + float(index) * 0.7548776662);
    const float stepNoise = frac(0.5 + float(index) * 0.56984029);
    const uint slices = max(View.OcclusionSlices, 1u);
    const uint steps = max(View.OcclusionSteps, 1u);
    const float radius = min(screenRadius, 128.0);
    const float nearest = 1.3 / radius;

    float visibility = 0.0;
    for (uint slice = 0; slice < slices; ++slice)
    {
        // The slice's direction across the screen, and the plane it cuts through the view
        const float phi = (float(slice) + sliceNoise) / float(slices) * Pi;
        const float2 direction = float2(cos(phi), sin(phi));
        const float2 omega = float2(direction.x, -direction.y) * radius;
        const float3 directionVS = float3(direction, 0.0);
        const float3 ortho = directionVS - dot(directionVS, toCamera) * toCamera;
        const float3 axis = normalize(cross(ortho, toCamera));
        const float3 projectedNormal = normal - axis * dot(normal, axis);
        const float projectedLength = length(projectedNormal);
        const float signNormal = sign(dot(ortho, projectedNormal));
        const float cosNormal = saturate(dot(projectedNormal, toCamera) / max(projectedLength, 1e-6));
        const float n = signNormal * acos(cosNormal);

        // The horizons on both sides, from the surface's own (nothing above it) up to the highest
        // point seen within the radius
        const float lowHorizon0 = cos(n + HalfPi);
        const float lowHorizon1 = cos(n - HalfPi);
        float horizon0 = lowHorizon0;
        float horizon1 = lowHorizon1;
        for (uint stepIndex = 0; stepIndex < steps; ++stepIndex)
        {
            const float jitter = frac(stepNoise + float(slice + stepIndex * slices) * 0.6180339887);
            float s = (float(stepIndex) + jitter) / float(steps);
            s = s * s + nearest;
            const float2 offset = round(s * omega) * texel;
            const float3 sample0 = halfSizePosition(uv + offset, texel);
            const float3 sample1 = halfSizePosition(uv - offset, texel);
            const float3 delta0 = sample0 - center;
            const float3 delta1 = sample1 - center;
            const float distance0 = length(delta0);
            const float distance1 = length(delta1);
            const float weight0 = saturate(distance0 * falloffMul + falloffAdd);
            const float weight1 = saturate(distance1 * falloffMul + falloffAdd);
            horizon0 = max(horizon0, lerp(lowHorizon0, dot(delta0 / max(distance0, 1e-6), toCamera), weight0));
            horizon1 = max(horizon1, lerp(lowHorizon1, dot(delta1 / max(distance1, 1e-6), toCamera), weight1));
        }

        // The visible arc between the horizons, cosine-weighted about the normal, exactly
        const float h0 = n + clamp(-acos(clamp(horizon1, -1.0, 1.0)) - n, -HalfPi, HalfPi);
        const float h1 = n + clamp(acos(clamp(horizon0, -1.0, 1.0)) - n, -HalfPi, HalfPi);
        const float arc0 = (cosNormal + 2.0 * h0 * sin(n) - cos(2.0 * h0 - n)) * 0.25;
        const float arc1 = (cosNormal + 2.0 * h1 * sin(n) - cos(2.0 * h1 - n)) * 0.25;
        visibility += lerp(projectedLength, 1.0, 0.05) * (arc0 + arc1);
    }
    visibility /= float(slices);
    return saturate(pow(saturate(visibility), View.OcclusionIntensity));
}

// 3. The occlusion blurred along a direction, 5 taps of 4 texels' width (so the 4 x 4 tile's noise
// evens out), each weighed down by how far its depth is from the pixel's, so that the blur doesn't
// run across edges
[shader("fragment")]
float occlusionBlurFragment(float4 position : SV_Position) : SV_Target
{
    const float2 texel = Object.CustomData.zw;
    const float2 uv = position.xy * texel;
    const float2 step = Object.CustomData2 * texel;
    const float centerDepth = secondSource().SampleLevel(nyx::PointClamp, uv, 0.0).r;
    float sum = 0.0;
    float weights = 0.0;
    for (int tap = -2; tap <= 2; ++tap)
    {
        const float2 at = uv + step * float(tap);
        const float depth = secondSource().SampleLevel(nyx::PointClamp, at, 0.0).r;
        const float weight = (abs(tap) == 2 ? 0.5 : 1.0) * saturate(1.0 - abs(depth - centerDepth) / (0.05 * centerDepth + 0.05));
        sum += firstSource().SampleLevel(nyx::PointClamp, at, 0.0).r * weight;
        weights += weight;
    }
    return weights > 1e-4 ? sum / weights : firstSource().SampleLevel(nyx::PointClamp, uv, 0.0).r;
}

// The occlusion at a full-size pixel `pixel` of linear depth `depth`: the four half-size pixels
// round it, each weighed by how near it is and how near its depth is to the pixel's, so that an
// edge between near and far keeps each side's own (a bilateral upsample)
public float occlusionAt(float2 pixel, float depth)
{
    const float2 halfSize = max(floor(View.ViewportSize * 0.5), float2(1.0));
    const float2 at = (pixel + 0.5) * 0.5 - 0.5;
    const float2 corner = floor(at);
    const float2 f = at - corner;
    float sum = 0.0;
    float weights = 0.0;
    float nearestDepth = 1e30;
    float nearestOcclusion = 1.0;
    for (int index = 0; index < 4; ++index)
    {
        const int2 offset = int2(index & 1, index >> 1);
        const int2 texel = clamp(int2(corner) + offset, int2(0), int2(halfSize) - 1);
        const float halfDepth = View.OcclusionDepth.get().Load(int3(texel, 0)).r;
        const float occlusion = View.Occlusion.get().Load(int3(texel, 0)).r;
        const float2 bilinear = lerp(1.0 - f, f, float2(offset));
        const float difference = abs(halfDepth - depth) / max(depth, 1e-4);
        const float weight = bilinear.x * bilinear.y / (difference * 50.0 + 1e-3);
        sum += occlusion * weight;
        weights += weight;
        if (abs(halfDepth - depth) < nearestDepth)
        {
            nearestDepth = abs(halfDepth - depth);
            nearestOcclusion = occlusion;
        }
    }
    return weights > 1e-6 ? sum / weights : nearestOcclusion;
}

// 4. The sky's light the scene pass wrote apart, occluded, added to the scene (the pipeline adds)
[shader("fragment")]
float4 occlusionCompositeFragment(float4 position : SV_Position) : SV_Target
{
    const int2 pixel = int2(position.xy);
    const float3 ambient = View.AmbientLight.get().Load(int3(pixel, 0)).rgb;
    const float depth = linearDepth(View.SceneDepth.get().Load(int3(pixel, 0)).r);
    return float4(ambient * occlusionAt(position.xy - 0.5, depth), 0.0);
}
