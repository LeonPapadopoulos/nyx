// The noise the high cloud layer is made from (VulkanCloudLayer, CloudLayer.slang): a square of
// 256 x 256 texels that tiles, made from the layer's seed when it is first drawn or its seed
// changes, then halved into its mips:
//   - r: cloudlets, round soft domes in 16 x 16 cells, one a cell at a place and of a size of its
//     own, merged where they meet (a mackerel sky);
//   - g: the same in 32 x 32 cells, finer, which break up their edges;
//   - b: fractal noise of 5 octaves from 4 cells across, which the shader stretches along the wind
//     into streaks of cirrus;
//   - a: fractal noise of 4 octaves from 2 cells across, large and slow, which warps the pattern and
//     varies the coverage.
// Every octave's cells divide the square, and their hashes wrap with it, so it tiles. Images in
// General layout while they are written.

[vk::binding(0, 0)] [vk::image_format("rgba16f")] RWTexture2D<float4> Target;
[vk::binding(1, 0)] [vk::image_format("rgba16f")] RWTexture2D<float4> Previous;

// NoiseDispatch in VulkanCloudLayer.cpp
struct NoiseDispatch
{
    // Of the mip written, a side
    uint Size;
    uint Seed;
    uint Padding0;
    uint Padding1;
}
[vk::push_constant] ConstantBuffer<NoiseDispatch> Dispatch;

// Jarzynski and Olano's PCG hash of three numbers (2020): every bit of each depends on every bit of
// all three
uint3 pcg3d(uint3 v)
{
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    return v;
}

// Three numbers from 0 to 1 for a cell of a grid `period` cells across, wrapped, of the seed and a
// salt apart for each kind of noise
float3 cellRandom(int2 cell, uint period, uint salt)
{
    const uint2 wrapped = uint2((cell % int(period) + int(period)) % int(period));
    return float3(pcg3d(uint3(wrapped, Dispatch.Seed * 16u + salt))) * (1.0 / 4294967296.0);
}

// Soft round domes, one a cell, `cells` across the square: each 1 at its middle, falling to 0 at its
// radius ((1 - d²)²: smooth there), at a place within its cell and of a radius (0.4 to 0.75 of a
// cell) of its own, apart from their neighbours; where they meet, merged as 1 - (1 - a)(1 - b)
float cloudlets(float2 uv, uint cells, uint salt)
{
    const float2 p = uv * float(cells);
    const int2 base = int2(floor(p));
    float clear = 1.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            const int2 cell = base + int2(x, y);
            const float3 random = cellRandom(cell, cells, salt);
            const float2 middle = float2(cell) + 0.25 + 0.5 * random.xy;
            const float radius = 0.4 + 0.35 * random.z;
            const float2 d = (p - middle) / radius;
            const float dome = saturate(1.0 - dot(d, d));
            clear *= 1.0 - dome * dome;
        }
    }
    return 1.0 - clear;
}

// Gradient noise on a grid `cells` across the square, -1 to 1 about: each corner a gradient of its
// own, blended by the quintic
float gradientNoise(float2 uv, uint cells, uint salt)
{
    const float2 p = uv * float(cells);
    const int2 base = int2(floor(p));
    const float2 f = p - float2(base);
    const float2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float corners[4];
    for (int corner = 0; corner < 4; ++corner)
    {
        const int2 offset = int2(corner & 1, corner >> 1);
        const float angle = cellRandom(base + offset, cells, salt).x * 6.28318530718;
        corners[corner] = dot(float2(cos(angle), sin(angle)), f - float2(offset));
    }
    return 1.41421356 * lerp(lerp(corners[0], corners[1], u.x), lerp(corners[2], corners[3], u.x), u.y);
}

// Fractal noise, `octaves` of it from `cells` across, each twice as fine and half as strong, 0 to 1
float fractalNoise(float2 uv, uint cells, int octaves, uint salt)
{
    float sum = 0.0;
    float amplitude = 0.5;
    float total = 0.0;
    for (int octave = 0; octave < octaves; ++octave)
    {
        sum += gradientNoise(uv, cells << uint(octave), salt + uint(octave)) * amplitude;
        total += amplitude;
        amplitude *= 0.5;
    }
    return saturate(0.5 + 0.5 * sum / total * 1.6);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void cloudNoise(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Dispatch.Size || id.y >= Dispatch.Size)
    {
        return;
    }
    const float2 uv = (float2(id.xy) + 0.5) / float(Dispatch.Size);
    Target[id.xy] = float4(cloudlets(uv, 16u, 0u), cloudlets(uv, 32u, 1u), fractalNoise(uv, 4u, 5, 2u), fractalNoise(uv, 2u, 4, 8u));
}

// A mip from the one before: each texel the mean of its four
[shader("compute")]
[numthreads(8, 8, 1)]
void cloudNoiseDownsample(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Dispatch.Size || id.y >= Dispatch.Size)
    {
        return;
    }
    const uint2 corner = id.xy * 2;
    Target[id.xy] = 0.25 * (Previous[corner] + Previous[corner + uint2(1, 0)] + Previous[corner + uint2(0, 1)] + Previous[corner + uint2(1, 1)]);
}
