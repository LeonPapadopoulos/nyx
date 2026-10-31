// How much of the sky a surface sees past the SDF terrain round the camera, from the terrain's
// occlusion volume (TerrainOcclusion.h, VulkanTerrainOcclusion): cones from a point off the surface,
// one along its normal and four round it, each marched in steps of its width through the
// level whose cells are as wide, light lost to how solid each step is. The same numbers as
// GetTerrainSkyVisibility() on the CPU, which its tests check. 1 without a volume, or past its
// coarsest window.

import nyx.bindless;
import nyx.view;

namespace nyx
{
    static const uint TerrainOcclusionMaxLevels = 6;
    static const float TerrainOcclusionSize = 64.0;
    static const float TerrainOcclusionFinestCell = 1.0;
    static const uint TerrainOcclusionCones = 5;
    static const float TerrainOcclusionConeTilt = 0.96;
    static const float TerrainOcclusionConeTangent = 0.58;
    static const float TerrainOcclusionStepWidths = 1.0;
    static const float TerrainOcclusionMaxDistance = 256.0;
    static const uint TerrainOcclusionMaxSteps = 24;

    // How solid a level is at a point in the world, between its cells' middles; below 0 outside its
    // window. Its texture holds a cell at its world cell's index modulo its size, so it is sampled
    // wrapping at the point over the window's size.
    float terrainOcclusionSolid(uint level, float3 position)
    {
        const float4 window = View.TerrainOcclusionWindows[level];
        const float3 cells = (position - window.xyz) / window.w;
        if (any(cells < 0.0) || any(cells >= TerrainOcclusionSize))
        {
            return -1.0;
        }
        Volume volume;
        volume.Index = View.TerrainOcclusionVolumes[level / 4][level % 4];
        return volume.SampleLevel(LinearWrap, position / (window.w * TerrainOcclusionSize), 0.0).r;
    }

    // How much of the sky a surface at `position` facing `normal` sees, 0 to 1, dimmed by the view's
    // strength
    public float terrainSkyVisibility(float3 position, float3 normal)
    {
        if (View.TerrainOcclusionLevels == 0)
        {
            return 1.0;
        }
        const float3 n = normalize(normal);
        const float3 other = abs(n.y) < 0.9 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
        const float3 u = normalize(cross(n, other));
        const float3 v = cross(n, u);
        const float3 start = position + n * TerrainOcclusionFinestCell;
        const int levels = int(min(View.TerrainOcclusionLevels, TerrainOcclusionMaxLevels));
        float seen = 0.0;
        float weights = 0.0;
        for (uint cone = 0; cone < TerrainOcclusionCones; ++cone)
        {
            float3 direction = n;
            float weight = 1.0;
            if (cone > 0)
            {
                const float around = 6.2831853 * float(cone - 1) / float(TerrainOcclusionCones - 1);
                direction = n * cos(TerrainOcclusionConeTilt) + (u * cos(around) + v * sin(around)) * sin(TerrainOcclusionConeTilt);
                weight = cos(TerrainOcclusionConeTilt);
            }
            float through = 1.0;
            float t = TerrainOcclusionFinestCell;
            for (uint step = 0; step < TerrainOcclusionMaxSteps && t < TerrainOcclusionMaxDistance && through > 0.01; ++step)
            {
                const float width = max(2.0 * t * TerrainOcclusionConeTangent, TerrainOcclusionFinestCell);
                const float3 at = start + direction * t;
                int level = clamp(int(floor(log2(width / TerrainOcclusionFinestCell))), 0, levels - 1);
                float solid = terrainOcclusionSolid(uint(level), at);
                while (solid < 0.0 && level + 1 < levels)
                {
                    level += 1;
                    solid = terrainOcclusionSolid(uint(level), at);
                }
                if (solid < 0.0)
                {
                    break;
                }
                const float stride = TerrainOcclusionStepWidths * width;
                through *= pow(1.0 - min(solid, 0.999), stride / View.TerrainOcclusionWindows[level].w);
                t += stride;
            }
            seen += through * weight;
            weights += weight;
        }
        return lerp(1.0, seen / weights, View.TerrainOcclusionStrength);
    }
}
