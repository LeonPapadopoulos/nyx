// Noise that tiles, for the volumetric clouds' noises (VolumetricCloudNoise.slang,
// VolumetricCloudWeather.slang): gradient (Perlin) and cellular (Worley) noise on grids that divide
// a cube, `cells` across it, their hashes wrapping with it, so the cube repeats seamlessly; each of
// a seed and a salt apart for each kind of noise. Coordinates `uvw` are 0 to 1 over the cube.

namespace nyx
{
    // Jarzynski and Olano's PCG hash of three numbers (2020): every bit of each depends on every bit
    // of all three
    public uint3 cloudHash(uint3 v)
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

    // Three numbers from 0 to 1 for a cell of a grid `cells` across, wrapped
    public float3 cloudCellRandom(int3 cell, int cells, uint seed, uint salt)
    {
        const int3 wrapped = (cell % cells + cells) % cells;
        return float3(cloudHash(uint3(wrapped) + uint3(0u, 0u, (seed * 64u + salt) * 1024u))) * (1.0 / 4294967296.0);
    }

    // Gradient noise, about -1 to 1: each corner of the grid a gradient of its own, blended by the
    // quintic
    public float cloudGradientNoise(float3 uvw, int cells, uint seed, uint salt)
    {
        const float3 p = uvw * float(cells);
        const int3 base = int3(floor(p));
        const float3 f = p - float3(base);
        const float3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
        float corners[8];
        for (int corner = 0; corner < 8; ++corner)
        {
            const int3 offset = int3(corner & 1, (corner >> 1) & 1, corner >> 2);
            const float3 gradient = normalize(cloudCellRandom(base + offset, cells, seed, salt) * 2.0 - 1.0 + 1e-4);
            corners[corner] = dot(gradient, f - float3(offset));
        }
        const float x0 = lerp(corners[0], corners[1], u.x);
        const float x1 = lerp(corners[2], corners[3], u.x);
        const float x2 = lerp(corners[4], corners[5], u.x);
        const float x3 = lerp(corners[6], corners[7], u.x);
        return 1.5 * lerp(lerp(x0, x1, u.y), lerp(x2, x3, u.y), u.z);
    }

    // Fractal gradient noise, `octaves` of it from `cells` across, each twice as fine and half as
    // strong, 0 to 1 about its middle 0.5, stretched by `contrast`
    public float cloudPerlinFbm(float3 uvw, int cells, int octaves, float contrast, uint seed, uint salt)
    {
        float sum = 0.0;
        float amplitude = 1.0;
        float total = 0.0;
        for (int octave = 0; octave < octaves; ++octave)
        {
            sum += cloudGradientNoise(uvw, cells << octave, seed, salt + uint(octave)) * amplitude;
            total += amplitude;
            amplitude *= 0.5;
        }
        return saturate(0.5 + 0.5 * contrast * sum / total);
    }

    // Cellular noise: one point a cell, at a place of its own; 1 at a point, falling with the
    // distance to the nearest (inverted Worley: round, billowy blobs)
    public float cloudWorley(float3 uvw, int cells, uint seed, uint salt)
    {
        const float3 p = uvw * float(cells);
        const int3 base = int3(floor(p));
        float nearest = 1e9;
        for (int z = -1; z <= 1; ++z)
        {
            for (int y = -1; y <= 1; ++y)
            {
                for (int x = -1; x <= 1; ++x)
                {
                    const int3 cell = base + int3(x, y, z);
                    const float3 d = float3(cell) + cloudCellRandom(cell, cells, seed, salt) - p;
                    nearest = min(nearest, dot(d, d));
                }
            }
        }
        return 1.0 - saturate(sqrt(nearest));
    }

    // Three octaves of it, each twice as fine and weighed 0.625, 0.25, 0.125
    public float cloudWorleyFbm(float3 uvw, int cells, uint seed, uint salt)
    {
        return cloudWorley(uvw, cells, seed, salt) * 0.625 + cloudWorley(uvw, cells * 2, seed, salt + 1u) * 0.25 + cloudWorley(uvw, cells * 4, seed, salt + 2u) * 0.125;
    }
}
