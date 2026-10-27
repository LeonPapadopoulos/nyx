// The volumetric clouds (VulkanVolumetricClouds; Docs/Design/atmosphere.md), per view, at the size
// they are marched at (half the view's each way, or its own), before its scene pass:
//   1. the march (volumetricCloudsMarchFragment): each pixel's ray through the band (nyx.volumetricclouds),
//      its light, what gets through, and where along the ray the clouds are. Every pixel in a frame
//      that marches all of them; else one pixel of each square (2 x 2 or 4 x 4), this frame's of them
//      in a Bayer matrix's order, into a part of the target each way;
//   2. the resolve (volumetricCloudsResolveFragment): every pixel of this frame's picture of them:
//      from the frame before's where the clouds it looks at were then, found by their depth (the
//      camera's turn and move, and the wind carrying them, taken back). Still, as it was, the
//      marched pixels as marched. Moving, the frame before's held to what this frame's marched
//      pixels round it span, the marched pixels blended with it, so the squares don't show; where
//      it was off the picture, this frame's marched pixels round it, weighed by nearness and by how
//      near their depths are.
// Written: their light (rgb) and what gets through (a); their light's mean depth, where they begin
// and all but end (kilometres) and what of the sun gets through them (a); which the sky, the
// surfaces among and beyond them and the light shafts read (nyx.clouds), filtered up to the view's
// size. One triangle over the target each, with the scene pipelines' layout.

import nyx.bindless;
import nyx.view;
import nyx.volumetricclouds;

struct VolumetricCloudTargets
{
    float4 Light : SV_Target0;
    float4 Depth : SV_Target1;
}

[shader("vertex")]
float4 volumetricCloudsVertex(uint vertex : SV_VulkanVertexID) : SV_Position
{
    const float2 corner = float2((vertex << 1) & 2, vertex & 2);
    return float4(corner * 2.0 - 1.0, 0.0, 1.0);
}

// The size the clouds are marched at, its width and height
int2 cloudExtent()
{
    return int2(View.VolumetricCloudDetailOrigin.zw);
}

bool isEveryPixelMarched()
{
    return (View.VolumetricCloudMode & 1u) != 0u;
}

// The squares of which one pixel is marched this frame, pixels a side
int blockSize()
{
    return max(int((View.VolumetricCloudMode >> 1) & 7u), 1);
}

int2 updateOffset()
{
    return int2(int(View.VolumetricCloudUpdateX), int(View.VolumetricCloudUpdateY));
}

// The way a pixel of the clouds' picture looks, normalised: through the view's rotation and
// projection without its place, so as precise far from the world's middle as near it
float3 cloudPixelRay(float2 pixel)
{
    const float2 ndc = pixel / float2(cloudExtent()) * 2.0 - 1.0;
    const float4 point = mul(View.CloudInverseSkyViewProjection, float4(ndc, 1.0, 1.0));
    return normalize(point.xyz / point.w);
}

// Where in each step the samples are: its middle, for every pixel. A dither of it from pixel to
// pixel (as step 1 had) evens out banding, but carried and resampled from frame to frame its pattern
// no longer lines up with the filter that evens it out, and shows as a grid while the camera turns;
// the steps through a cloud, a quarter of the coarse ones (a few metres for clouds a few hundred
// metres up), are short enough that their middles show no bands
static const float StepPlace = 0.5;

// How far the frame before's clouds may be from this frame's marched pixels round them, as a part
// of what those span, before they are held in (the marched pixels are few, a square apart, so what
// they span is narrower than what lies between them); how much of a marched pixel is the frame
// before's, held, while the picture moves, so it doesn't stand out from the carried ones round it
static const float ClampMargin = 0.5;
static const float MarchedHistory = 0.3;

// 1. The march, of every pixel or one of each square
[shader("fragment")]
VolumetricCloudTargets volumetricCloudsMarchFragment(float4 position : SV_Position)
{
    int2 pixel = int2(position.xy);
    if (!isEveryPixelMarched())
    {
        pixel = pixel * blockSize() + updateOffset();
    }
    pixel = min(pixel, cloudExtent() - 1);
    const nyx::VolumetricCloudsResult clouds = nyx::volumetricCloudsAlong(nyx::viewVolumetricCloudParams(), cloudPixelRay(float2(pixel) + 0.5), StepPlace);
    VolumetricCloudTargets targets;
    targets.Light = float4(clouds.Light, clouds.Transmittance);
    targets.Depth = float4(clouds.Depth, clouds.Front, clouds.Back, clouds.Through);
    return targets;
}

VolumetricCloudTargets marchedAt(int2 texel)
{
    VolumetricCloudTargets targets;
    targets.Light = View.VolumetricCloudMarchLight.get().Load(int3(texel, 0));
    targets.Depth = View.VolumetricCloudMarchDepth.get().Load(int3(texel, 0));
    return targets;
}

// This frame's marched pixels round a pixel, the 3 x 3 nearest: blended by how near each is (a tent
// a square and a half wide) and by how near its depth is to the nearest one's, so a cloud's edge
// against the sky stays an edge; and the least and most of their light and transmittance
struct MarchedAround
{
    VolumetricCloudTargets Blend;
    float4 Least;
    float4 Most;
}

MarchedAround marchedAround(int2 pixel)
{
    const int block = blockSize();
    const int2 offset = updateOffset();
    const int2 last = max((cloudExtent() - 1 - offset) / block, int2(0));
    const float2 at = float2(pixel - offset) / float(block);
    const int2 middle = clamp(int2(round(at)), int2(0), last);
    const float nearestDepth = marchedAt(middle).Depth.x;

    MarchedAround around;
    around.Blend.Light = float4(0.0);
    around.Blend.Depth = float4(0.0);
    around.Least = float4(1e9);
    around.Most = float4(-1e9);
    float total = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            const int2 texel = clamp(middle + int2(x, y), int2(0), last);
            const VolumetricCloudTargets marched = marchedAt(texel);
            const float2 apart = float2(texel) - at;
            const float near = max(1.0 - abs(apart.x) / 1.5, 0.0) * max(1.0 - abs(apart.y) / 1.5, 0.0);
            const float alike = exp(-abs(marched.Depth.x - nearestDepth) / (0.1 * nearestDepth + 0.05));
            const float weight = near * alike + 1e-4;
            around.Blend.Light += marched.Light * weight;
            around.Blend.Depth += marched.Depth * weight;
            total += weight;
            around.Least = min(around.Least, marched.Light);
            around.Most = max(around.Most, marched.Light);
        }
    }
    around.Blend.Light /= total;
    around.Blend.Depth /= total;
    return around;
}

// The frame before's light and transmittance at a point of it (`at` in its texels, texel k's middle
// at k), filtered by Catmull and Rom's cubic over the 4 x 4 texels round it in nine bilinear taps:
// sharp, so the clouds carried frame after frame as the camera turns don't blur as bilinear
// filtering would blur them a little each frame; held in range, as its lobes can overshoot
float4 previousLightAt(float2 at)
{
    const float2 texel = 1.0 / float2(cloudExtent());
    const float2 corner = floor(at);
    const float2 f = at - corner;
    const float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    const float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    const float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    const float2 w3 = f * f * (-0.5 + 0.5 * f);
    // The middle two texels each way in one bilinear tap, the outer ones apart
    const float2 w12 = w1 + w2;
    const float2 uv0 = (corner - 1.0 + 0.5) * texel;
    const float2 uv12 = (corner + w2 / w12 + 0.5) * texel;
    const float2 uv3 = (corner + 2.0 + 0.5) * texel;
    const Texture2D previous = View.VolumetricCloudPreviousLight.get();
    float4 sum = float4(0.0);
    sum += previous.SampleLevel(nyx::LinearClamp, float2(uv0.x, uv0.y), 0.0) * (w0.x * w0.y);
    sum += previous.SampleLevel(nyx::LinearClamp, float2(uv12.x, uv0.y), 0.0) * (w12.x * w0.y);
    sum += previous.SampleLevel(nyx::LinearClamp, float2(uv3.x, uv0.y), 0.0) * (w3.x * w0.y);
    sum += previous.SampleLevel(nyx::LinearClamp, float2(uv0.x, uv12.y), 0.0) * (w0.x * w12.y);
    sum += previous.SampleLevel(nyx::LinearClamp, float2(uv12.x, uv12.y), 0.0) * (w12.x * w12.y);
    sum += previous.SampleLevel(nyx::LinearClamp, float2(uv3.x, uv12.y), 0.0) * (w3.x * w12.y);
    sum += previous.SampleLevel(nyx::LinearClamp, float2(uv0.x, uv3.y), 0.0) * (w0.x * w3.y);
    sum += previous.SampleLevel(nyx::LinearClamp, float2(uv12.x, uv3.y), 0.0) * (w12.x * w3.y);
    sum += previous.SampleLevel(nyx::LinearClamp, float2(uv3.x, uv3.y), 0.0) * (w3.x * w3.y);
    return float4(max(sum.rgb, 0.0), saturate(sum.a));
}

// What a debug view of the updates shows: the pixels marched this frame red, those carried grey,
// those with nothing to carry blue
VolumetricCloudTargets updateDebug(VolumetricCloudTargets targets, float3 color)
{
    if ((View.VolumetricCloudMode >> 8) == nyx::VolumetricCloudDebugUpdates)
    {
        targets.Light = float4(color, 0.0);
    }
    return targets;
}

// 2. The resolve: this frame's picture of the clouds, every pixel
[shader("fragment")]
VolumetricCloudTargets volumetricCloudsResolveFragment(float4 position : SV_Position)
{
    const int2 pixel = int2(position.xy);
    if (isEveryPixelMarched())
    {
        return updateDebug(marchedAt(pixel), float3(0.5, 0.05, 0.05));
    }
    const int block = blockSize();
    const bool bMarched = all(pixel % block == updateOffset());

    // Where the clouds this pixel looks at were in the frame before: as far along its ray as the
    // frame before's clouds at this pixel were, moved back by the camera's move and the wind, seen
    // through the frame before's rotation and projection
    const int2 extent = cloudExtent();
    const float4 depth = View.VolumetricCloudPreviousDepth.get().Load(int3(pixel, 0));
    const float3 before = cloudPixelRay(float2(pixel) + 0.5) * depth.x + View.VolumetricCloudMotion.xyz;
    const float4 clip = mul(View.VolumetricCloudPreviousSkyViewProjection, float4(before, 1.0));
    const float2 then = clip.xy / max(clip.w, 1e-6) * 0.5 * float2(extent) + 0.5 * float2(extent) - 0.5;
    const bool bSeen = clip.w > 0.0 && all(then >= -0.5) && all(then <= float2(extent) - 0.5);

    // Still: as it was, the marched pixels as marched, so a still picture is the one every pixel
    // marched gives, to the bit
    const float shift = length(then - float2(pixel));
    if (bSeen && shift < 0.01)
    {
        if (bMarched)
        {
            return updateDebug(marchedAt(pixel / block), float3(0.5, 0.05, 0.05));
        }
        VolumetricCloudTargets targets;
        targets.Light = View.VolumetricCloudPreviousLight.get().Load(int3(pixel, 0));
        targets.Depth = depth;
        return updateDebug(targets, float3(0.12, 0.12, 0.12));
    }

    const MarchedAround around = marchedAround(pixel);
    if (!bSeen)
    {
        // Nothing to carry: this frame's marched pixels round it
        return updateDebug(around.Blend, float3(0.05, 0.1, 0.5));
    }

    // Moving: the frame before's, held to what this frame's marched pixels round it span (a little
    // more), the more the faster it moves
    const float moving = saturate(shift);
    const float4 margin = (around.Most - around.Least) * ClampMargin;
    const float4 history = previousLightAt(then);
    const float4 held = lerp(history, clamp(history, around.Least - margin, around.Most + margin), moving);
    VolumetricCloudTargets targets;
    targets.Depth = View.VolumetricCloudPreviousDepth.get().Load(int3(clamp(int2(then + 0.5), int2(0), extent - 1), 0));
    if (bMarched)
    {
        // Half the marched pixel, half the frame before's held: no square stands out from the rest
        const VolumetricCloudTargets marched = marchedAt(pixel / block);
        targets.Light = lerp(marched.Light, held, MarchedHistory * moving);
        targets.Depth = marched.Depth;
        return updateDebug(targets, float3(0.5, 0.05, 0.05));
    }
    targets.Light = held;
    return updateDebug(targets, float3(0.12, 0.12, 0.12));
}
