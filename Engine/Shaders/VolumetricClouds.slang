// The volumetric clouds (VulkanVolumetricClouds; Docs/Design/atmosphere.md), per view, at half its
// size each way, before its scene pass:
//   1. the march (volumetricCloudsMarchFragment): each pixel's ray through the band (nyx.volumetricclouds),
//      its light, what gets through and how far along the ray the clouds are. Every pixel in a
//      frame that marches all of them; else one pixel of each 4 x 4, this frame's of them in a
//      Bayer matrix's order, into a quarter of the target each way;
//   2. the resolve (volumetricCloudsResolveFragment): every pixel of this frame's picture of them:
//      the one marched, or the frame before's where the clouds it looks at were then, found by
//      their depth (the camera's turn and move, and the wind carrying them, taken back), or where
//      that is off the picture, this frame's marched pixels round it.
// Written: their light (rgb) and what gets through (a), and their depth (kilometres), which the sky,
// the surfaces beyond them and the light shafts read (nyx.clouds), filtered up to the view's size.
// One triangle over the target each, with the scene pipelines' layout.

import nyx.bindless;
import nyx.view;
import nyx.volumetricclouds;

struct VolumetricCloudTargets
{
    float4 Light : SV_Target0;
    float Depth : SV_Target1;
}

[shader("vertex")]
float4 volumetricCloudsVertex(uint vertex : SV_VulkanVertexID) : SV_Position
{
    const float2 corner = float2((vertex << 1) & 2, vertex & 2);
    return float4(corner * 2.0 - 1.0, 0.0, 1.0);
}

// The half size, its width and height
int2 cloudExtent()
{
    return int2(View.VolumetricCloudDetailOrigin.zw);
}

bool isEveryPixelMarched()
{
    return (View.VolumetricCloudMode & 1u) != 0u;
}

int2 updateOffset()
{
    return int2(int(View.VolumetricCloudUpdateX), int(View.VolumetricCloudUpdateY));
}

// The way a pixel of the half size looks, normalised: through the view's rotation and projection
// without its place, so as precise far from the world's middle as near it
float3 cloudPixelRay(float2 pixel)
{
    const float2 ndc = pixel / float2(cloudExtent()) * 2.0 - 1.0;
    const float4 point = mul(View.CloudInverseSkyViewProjection, float4(ndc, 1.0, 1.0));
    return normalize(point.xyz / point.w);
}

// Where in each step a pixel's samples are, 0 to 1: the 2 x 2 Bayer matrix's order, each pixel of a
// square its own quarter, which the filter up to the view's size (nyx.clouds), a 2 x 2 box, evens
// out exactly; the same for the pixel every frame, so a still picture stays still
static const uint JitterOrder[4] = { 0, 2, 3, 1 };

float pixelJitter(int2 pixel)
{
    return (float(JitterOrder[(pixel.y & 1) * 2 + (pixel.x & 1)]) + 0.5) / 4.0;
}

// 1. The march, of every pixel or one of each 4 x 4
[shader("fragment")]
VolumetricCloudTargets volumetricCloudsMarchFragment(float4 position : SV_Position)
{
    int2 pixel = int2(position.xy);
    if (!isEveryPixelMarched())
    {
        pixel = pixel * 4 + updateOffset();
    }
    pixel = min(pixel, cloudExtent() - 1);
    const nyx::VolumetricCloudsResult clouds = nyx::volumetricCloudsAlong(nyx::viewVolumetricCloudParams(), cloudPixelRay(float2(pixel) + 0.5), pixelJitter(pixel));
    VolumetricCloudTargets targets;
    targets.Light = float4(clouds.Light, clouds.Transmittance);
    targets.Depth = clouds.Depth;
    return targets;
}

VolumetricCloudTargets marchedAt(int2 texel)
{
    VolumetricCloudTargets targets;
    targets.Light = View.VolumetricCloudMarchLight.get().Load(int3(texel, 0));
    targets.Depth = View.VolumetricCloudMarchDepth.get().Load(int3(texel, 0)).r;
    return targets;
}

// This frame's marched pixels round a pixel, blended by how near each is: the march's texel q is the
// pixel q * 4 + the frame's offset
VolumetricCloudTargets marchedAround(int2 pixel)
{
    const int2 last = (cloudExtent() - 1 - updateOffset()) / 4;
    const float2 texel = float2(pixel - updateOffset()) / 4.0;
    const int2 first = clamp(int2(floor(texel)), int2(0), last);
    const float2 f = saturate(texel - float2(first));
    const VolumetricCloudTargets a = marchedAt(first);
    const VolumetricCloudTargets b = marchedAt(min(first + int2(1, 0), last));
    const VolumetricCloudTargets c = marchedAt(min(first + int2(0, 1), last));
    const VolumetricCloudTargets d = marchedAt(min(first + int2(1, 1), last));
    VolumetricCloudTargets targets;
    targets.Light = lerp(lerp(a.Light, b.Light, f.x), lerp(c.Light, d.Light, f.x), f.y);
    targets.Depth = lerp(lerp(a.Depth, b.Depth, f.x), lerp(c.Depth, d.Depth, f.x), f.y);
    return targets;
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

// 2. The resolve: this frame's picture of the clouds, every pixel of the half size
[shader("fragment")]
VolumetricCloudTargets volumetricCloudsResolveFragment(float4 position : SV_Position)
{
    const int2 pixel = int2(position.xy);
    if (isEveryPixelMarched())
    {
        return updateDebug(marchedAt(pixel), float3(0.5, 0.05, 0.05));
    }
    if (all(pixel % 4 == updateOffset()))
    {
        return updateDebug(marchedAt(pixel / 4), float3(0.5, 0.05, 0.05));
    }

    // Where the clouds this pixel looks at were in the frame before: as far along its ray as the
    // frame before's clouds at this pixel were, moved back by the camera's move and the wind, seen
    // through the frame before's rotation and projection
    const int2 extent = cloudExtent();
    const float depth = View.VolumetricCloudPreviousDepth.get().Load(int3(pixel, 0)).r;
    const float3 before = cloudPixelRay(float2(pixel) + 0.5) * depth + View.VolumetricCloudMotion.xyz;
    const float4 clip = mul(View.VolumetricCloudPreviousSkyViewProjection, float4(before, 1.0));
    if (clip.w > 0.0)
    {
        const float2 uv = clip.xy / clip.w * 0.5 + 0.5;
        const float2 then = uv * float2(extent) - 0.5;
        if (all(then >= -0.5) && all(then <= float2(extent) - 0.5))
        {
            VolumetricCloudTargets targets;
            if (all(abs(then - float2(pixel)) < 0.01))
            {
                // Where it was: as it was, not blurred by filtering
                targets.Light = View.VolumetricCloudPreviousLight.get().Load(int3(pixel, 0));
                targets.Depth = depth;
            }
            else
            {
                targets.Light = previousLightAt(then);
                targets.Depth = View.VolumetricCloudPreviousDepth.get().Load(int3(clamp(int2(then + 0.5), int2(0), extent - 1), 0)).r;
            }
            return updateDebug(targets, float3(0.12, 0.12, 0.12));
        }
    }
    return updateDebug(marchedAround(pixel), float3(0.05, 0.1, 0.5));
}
