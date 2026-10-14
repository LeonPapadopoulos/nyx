// The light of the environment, made from the sky when it loads (VulkanEnvironment,
// Design/shader-materials.md, recommendation 6):
//   - environmentCopy, environmentDownsample: the sky in HDR with every mip, the source of the rest;
//   - environmentPrefilter: the specular cube, each mip the sky as a rougher surface reflects it
//     (GGX, filtered importance sampling);
//   - environmentIrradiance: the sky projected onto nine spherical harmonics, for diffuse light;
//   - brdfLookup: the split-sum table, scale and bias of F0 by n·v and roughness.
// Run once each, in compute; the images are written in General layout.

import nyx.bindless;

[vk::binding(0, 1)] [vk::image_format("rgba16f")] RWTexture2DArray<float4> Target;
[vk::binding(1, 1)] [vk::image_format("rgba16f")] RWTexture2DArray<float4> Previous;
[vk::binding(2, 1)] RWStructuredBuffer<float4> Partials;
// RGBA: a core storage format, where RG would need extended formats
[vk::binding(3, 1)] [vk::image_format("rgba16f")] RWTexture2D<float4> Lookup;

// EnvironmentConstants in VulkanEnvironment.cpp
struct EnvironmentConstants
{
    // The cube sampled, in the texture array
    uint Source;
    // Of the image written, per side
    uint Size;
    // Of the source's first mip, per side
    uint SourceSize;
    float Roughness;
    uint SampleCount;
    // The source's mip to read
    float SourceLevel;
    uint2 _Padding;
}
[vk::push_constant] ConstantBuffer<EnvironmentConstants> Constants;

static const float Pi = 3.14159265358979;

// A cube face's texel as a direction (Vulkan's faces: +X, -X, +Y, -Y, +Z, -Z)
float3 directionOf(uint3 texel, uint size)
{
    const float2 st = (float2(texel.xy) + 0.5) / float(size) * 2.0 - 1.0;
    const float s = st.x;
    const float t = st.y;
    float3 direction;
    switch (texel.z)
    {
    case 0: direction = float3(1.0, -t, -s); break;
    case 1: direction = float3(-1.0, -t, s); break;
    case 2: direction = float3(s, 1.0, t); break;
    case 3: direction = float3(s, -1.0, -t); break;
    case 4: direction = float3(s, -t, 1.0); break;
    default: direction = float3(-s, -t, -1.0); break;
    }
    return normalize(direction);
}

TextureCube source()
{
    nyx::Cubemap cube;
    cube.Index = Constants.Source;
    return cube.get();
}

float2 hammersley(uint index, uint count)
{
    return float2(float(index) / float(count), float(reversebits(index)) * 2.3283064365386963e-10);
}

// A half vector around `n` drawn from GGX of `alpha` (roughness squared)
float3 sampleGgx(float2 xi, float3 n, float alpha)
{
    const float phi = 2.0 * Pi * xi.x;
    // Clamped: for a near-zero alpha, rounding can put cos above 1, and sin would be NaN
    const float cosTheta = min(sqrt((1.0 - xi.y) / (1.0 + (alpha * alpha - 1.0) * xi.y)), 1.0);
    const float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    const float3 h = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);

    const float3 up = abs(n.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    const float3 tangent = normalize(cross(up, n));
    const float3 bitangent = cross(n, tangent);
    return normalize(tangent * h.x + bitangent * h.y + n * h.z);
}

float ggx(float nDotH, float alpha)
{
    const float a2 = alpha * alpha;
    const float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / (Pi * d * d);
}

// Height-correlated Smith visibility, G / (4 n·l n·v)
float smithVisibility(float nDotV, float nDotL, float alpha)
{
    const float a2 = alpha * alpha;
    const float lambdaV = nDotL * sqrt(nDotV * nDotV * (1.0 - a2) + a2);
    const float lambdaL = nDotV * sqrt(nDotL * nDotL * (1.0 - a2) + a2);
    return 0.5 / (lambdaV + lambdaL);
}

// Mip 0 of the HDR sky: the sky sampled at each texel's direction
[shader("compute")]
[numthreads(8, 8, 1)]
void environmentCopy(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Constants.Size || id.y >= Constants.Size)
    {
        return;
    }
    Target[id] = float4(source().SampleLevel(nyx::LinearClamp, directionOf(id, Constants.Size), 0.0).rgb, 1.0);
}

// The next mip: each texel the mean of four of the one before
[shader("compute")]
[numthreads(8, 8, 1)]
void environmentDownsample(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Constants.Size || id.y >= Constants.Size)
    {
        return;
    }
    const uint3 corner = uint3(id.xy * 2, id.z);
    Target[id] = 0.25 * (Previous[corner] + Previous[corner + uint3(1, 0, 0)] + Previous[corner + uint3(0, 1, 0)] + Previous[corner + uint3(1, 1, 0)]);
}

// One mip of the specular cube: the light a surface of the roughness reflects towards its
// normal (n = v = r). Samples read the mip whose texels cover as much of the sphere as the sample
// does, so few of them make a smooth result (Křivánek and Colbert).
[shader("compute")]
[numthreads(8, 8, 1)]
void environmentPrefilter(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Constants.Size || id.y >= Constants.Size)
    {
        return;
    }
    const float3 n = directionOf(id, Constants.Size);
    if (Constants.Roughness <= 0.0)
    {
        Target[id] = float4(source().SampleLevel(nyx::LinearClamp, n, Constants.SourceLevel).rgb, 1.0);
        return;
    }

    const float alpha = Constants.Roughness * Constants.Roughness;
    const float texelSolidAngle = 4.0 * Pi / (6.0 * float(Constants.SourceSize) * float(Constants.SourceSize));
    float3 sum = 0.0;
    float weight = 0.0;
    for (uint index = 0; index < Constants.SampleCount; ++index)
    {
        const float3 h = sampleGgx(hammersley(index, Constants.SampleCount), n, alpha);
        const float nDotH = saturate(dot(n, h));
        const float3 l = normalize(2.0 * dot(n, h) * h - n);
        const float nDotL = dot(n, l);
        if (nDotL > 0.0)
        {
            // pdf = D(h) n·h / (4 v·h), and v·h = n·h here
            const float pdf = ggx(nDotH, alpha) * 0.25;
            const float sampleSolidAngle = 1.0 / (float(Constants.SampleCount) * pdf + 0.0001);
            const float level = max(0.5 * log2(sampleSolidAngle / texelSolidAngle) + 1.0, 0.0);
            sum += source().SampleLevel(nyx::LinearClamp, l, level).rgb * nDotL;
            weight += nDotL;
        }
    }
    Target[id] = float4(sum / max(weight, 0.0001), 1.0);
}

// The sky's nine spherical harmonics, each texel of one mip weighted by the solid angle it covers.
// One group; each thread writes its partial sums, the CPU adds them up.
[shader("compute")]
[numthreads(256, 1, 1)]
void environmentIrradiance(uint3 id : SV_DispatchThreadID)
{
    float3 sums[9];
    for (uint term = 0; term < 9; ++term)
    {
        sums[term] = 0.0;
    }

    const uint size = Constants.Size;
    for (uint texel = id.x; texel < 6 * size * size; texel += 256)
    {
        const uint3 position = uint3(texel % size, (texel / size) % size, texel / (size * size));
        const float3 d = directionOf(position, size);
        const float2 st = (float2(position.xy) + 0.5) / float(size) * 2.0 - 1.0;
        const float solidAngle = 4.0 / (float(size) * float(size) * pow(1.0 + dot(st, st), 1.5));
        const float3 radiance = source().SampleLevel(nyx::LinearClamp, d, Constants.SourceLevel).rgb * solidAngle;

        sums[0] += radiance * 0.282095;
        sums[1] += radiance * 0.488603 * d.y;
        sums[2] += radiance * 0.488603 * d.z;
        sums[3] += radiance * 0.488603 * d.x;
        sums[4] += radiance * 1.092548 * d.x * d.y;
        sums[5] += radiance * 1.092548 * d.y * d.z;
        sums[6] += radiance * 0.315392 * (3.0 * d.z * d.z - 1.0);
        sums[7] += radiance * 1.092548 * d.x * d.z;
        sums[8] += radiance * 0.546274 * (d.x * d.x - d.y * d.y);
    }
    for (uint term = 0; term < 9; ++term)
    {
        Partials[id.x * 9 + term] = float4(sums[term], 0.0);
    }
}

// The split-sum table: for n·v (x) and roughness (y), what F0 is scaled by and what is added to it
[shader("compute")]
[numthreads(8, 8, 1)]
void brdfLookup(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Constants.Size || id.y >= Constants.Size)
    {
        return;
    }
    const float nDotV = (float(id.x) + 0.5) / float(Constants.Size);
    const float roughness = (float(id.y) + 0.5) / float(Constants.Size);
    const float alpha = roughness * roughness;
    const float3 v = float3(sqrt(1.0 - nDotV * nDotV), 0.0, nDotV);
    const float3 n = float3(0.0, 0.0, 1.0);

    float2 sum = 0.0;
    for (uint index = 0; index < Constants.SampleCount; ++index)
    {
        const float3 h = sampleGgx(hammersley(index, Constants.SampleCount), n, alpha);
        const float3 l = normalize(2.0 * dot(v, h) * h - v);
        const float nDotL = saturate(l.z);
        const float nDotH = saturate(h.z);
        const float vDotH = saturate(dot(v, h));
        if (nDotL > 0.0)
        {
            const float visibility = smithVisibility(nDotV, nDotL, alpha) * 4.0 * nDotL * vDotH / nDotH;
            const float fresnel = pow(1.0 - vDotH, 5.0);
            sum += float2((1.0 - fresnel) * visibility, fresnel * visibility);
        }
    }
    Lookup[id.xy] = float4(sum / float(Constants.SampleCount), 0.0, 1.0);
}
