// The 3D noises the volumetric clouds' shapes are made from (VulkanVolumetricClouds,
// nyx.volumetricclouds), cubes that tile, made from the clouds' seed when they are first drawn or
// their seed changes, then halved into their mips:
//   - the shapes, 128 texels a side: r the shapes' field, Perlin-Worley (fractal gradient noise of
//     five octaves from 4 cells across, lifted by cellular noise of 4 cells, so its blobs are round
//     and billowy) worn down where cellular noise of three octaves from 4, 8 and 16 cells is low,
//     spread so its values are about evenly spread from 0 to 1: a threshold at 1 - c leaves about
//     c of space inside; g, b, a those cellular noises (not read yet);
//   - the details, 32 texels a side: r, g, b cellular noise of three octaves from 2, 4 and 8 cells,
//     the small billows and wisps that wear the shapes' edges away.
// Images in General layout while they are written.

import nyx.cloudnoise;

[vk::binding(0, 0)] [vk::image_format("rgba8")] RWTexture3D<float4> Target;
[vk::binding(1, 0)] [vk::image_format("rgba8")] RWTexture3D<float4> Previous;

// NoiseDispatch in VulkanVolumetricClouds.cpp
struct NoiseDispatch
{
    // Of the mip written, a side
    uint Size;
    uint Seed;
    uint Padding0;
    uint Padding1;
}
[vk::push_constant] ConstantBuffer<NoiseDispatch> Dispatch;

// The worn shapes' mean and deviation over the cube, before they are spread
static const float ShapeMean = 0.83;
static const float ShapeDeviation = 0.056;

float3 texelMiddle(uint3 id)
{
    return (float3(id) + 0.5) / float(Dispatch.Size);
}

[shader("compute")]
[numthreads(4, 4, 4)]
void cloudShapeNoise(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= Dispatch.Size))
    {
        return;
    }
    const float3 uvw = texelMiddle(id);
    const uint seed = Dispatch.Seed;
    const float perlin = nyx::cloudPerlinFbm(uvw, 4, 5, 1.6, seed, 0u);
    const float worley4 = nyx::cloudWorleyFbm(uvw, 4, seed, 10u);
    const float worley8 = nyx::cloudWorleyFbm(uvw, 8, seed, 20u);
    const float worley16 = nyx::cloudWorleyFbm(uvw, 16, seed, 30u);
    // The gradient noise lifted to start from the cells: 1 where either is, round where the cells are
    const float perlinWorley = worley4 + perlin * (1.0 - worley4);
    // Worn where the cells are low (Schneider's remap of the shape from the cells less 1 to 1)
    const float cells = worley4 * 0.625 + worley8 * 0.25 + worley16 * 0.125;
    const float worn = saturate((perlinWorley - (cells - 1.0)) / (2.0 - cells));
    // That is about normal, its mean 0.83 and its deviation 0.056 (measured over the cube): spread
    // evenly by its distribution (a logistic close to the normal's)
    const float field = 1.0 / (1.0 + exp(-1.702 * (worn - ShapeMean) / ShapeDeviation));
    Target[id] = float4(field, worley4, worley8, worley16);
}

[shader("compute")]
[numthreads(4, 4, 4)]
void cloudDetailNoise(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= Dispatch.Size))
    {
        return;
    }
    const float3 uvw = texelMiddle(id);
    const uint seed = Dispatch.Seed;
    Target[id] = float4(nyx::cloudWorleyFbm(uvw, 2, seed, 40u), nyx::cloudWorleyFbm(uvw, 4, seed, 50u), nyx::cloudWorleyFbm(uvw, 8, seed, 60u), 1.0);
}

// A mip from the one before: each texel the mean of its eight
[shader("compute")]
[numthreads(4, 4, 4)]
void cloudNoiseDownsample(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= Dispatch.Size))
    {
        return;
    }
    const uint3 corner = id * 2;
    float4 sum = float4(0.0);
    for (uint index = 0; index < 8; ++index)
    {
        sum += Previous[corner + uint3(index & 1, (index >> 1) & 1, index >> 2)];
    }
    Target[id] = sum * 0.125;
}
