// The volumetric clouds' weather map (VulkanVolumetricClouds, nyx.volumetricclouds): a square of 512
// texels a side that tiles, laid over the ground and carried by the wind, made from the clouds' seed
// when they are first drawn or their seed changes, then halved into its mips. Slices of tiling 3D
// noise (nyx.cloudnoise):
//   - r, the coverage: fractal gradient noise from 4 cells across, mixed with cellular noise, so
//     there are patches and bands of cloud with open sky between;
//   - g, the type: larger, slower noise from 2 cells, tall clouds here, small puffs there;
//   - b, the density: noise from 8 cells, some clouds thicker than others;
//   - a, finer cells, from 16 across.
// Images in General layout while they are written.

import nyx.cloudnoise;

[vk::binding(0, 0)] [vk::image_format("rgba8")] RWTexture2D<float4> Target;
[vk::binding(1, 0)] [vk::image_format("rgba8")] RWTexture2D<float4> Previous;

// NoiseDispatch in VulkanVolumetricClouds.cpp
struct NoiseDispatch
{
    uint Size;
    uint Seed;
    uint Padding0;
    uint Padding1;
}
[vk::push_constant] ConstantBuffer<NoiseDispatch> Dispatch;

[shader("compute")]
[numthreads(8, 8, 1)]
void cloudWeather(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= Dispatch.Size))
    {
        return;
    }
    // A slice through the middle of tiling cubes: it tiles across
    const float3 uvw = float3((float2(id.xy) + 0.5) / float(Dispatch.Size), 0.5);
    const uint seed = Dispatch.Seed;
    const float patches = nyx::cloudPerlinFbm(uvw, 4, 5, 2.5, seed, 100u);
    const float cells = nyx::cloudWorleyFbm(uvw, 4, seed, 110u);
    const float coverage = saturate((patches * 0.7 + cells * 0.3 - 0.47) * 1.8 + 0.5);
    const float type = nyx::cloudPerlinFbm(uvw, 2, 3, 2.5, seed, 120u);
    const float density = nyx::cloudPerlinFbm(uvw, 8, 3, 2.0, seed, 130u);
    const float fine = nyx::cloudWorleyFbm(uvw, 16, seed, 140u);
    Target[id.xy] = float4(coverage, type, density, fine);
}

// A mip from the one before: each texel the mean of its four
[shader("compute")]
[numthreads(8, 8, 1)]
void cloudWeatherDownsample(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= Dispatch.Size))
    {
        return;
    }
    const uint2 corner = id.xy * 2;
    Target[id.xy] = 0.25 * (Previous[corner] + Previous[corner + uint2(1, 0)] + Previous[corner + uint2(0, 1)] + Previous[corner + uint2(1, 1)]);
}
