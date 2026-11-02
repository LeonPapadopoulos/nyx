// What the grass's passes share (VulkanGrass): the ground points a tile keeps, made only when the
// ground changes (GrassPoints.slang), the blades made of them each frame and how they are drawn
// (Grass.slang), and the hashes that decide a blade, as Grass.h does on the CPU.

namespace nyx
{
    // Where a blade can grow, 20 bytes: where it is across its tile, its ground and what it is
    public struct GrassPoint
    {
        // x and z across the tile, 16 bits each
        public uint Place;
        // In the world, as the ground was when the tile was made
        public float Y;
        // The ground's normal, packed (packGrassNormal()) in the low 16 bits; a random number in the high
        public uint NormalRandom;
        // The ground's colour (packGrassColor())
        public uint Ground;
        // The blade's rank in the low 16 bits (Grass.h); in the high: its clump's cell from the cell
        // the blade is in, -1 to 1 each way, in 2 bits each; then how far the ground below it slides
        // up or down with the terrain's level of detail, 12 bits
        public uint RankClump;
    }

    // A tile's points: the groups of the pass that goes over them, as a dispatch's arguments, and how
    // many there are
    public struct GrassTileCount
    {
        public uint Groups;
        public uint GroupsY;
        public uint GroupsZ;
        public uint Count;
    }

    // A blade to draw this frame, 36 bytes
    public struct GrassBlade
    {
        public float3 Base;
        // Its height and width, 16 bits each, in GrassBladeHeightUnit and GrassBladeWidthUnit
        public uint Size;
        // Where its tip is from its foot along x and z, 16 bits each, in GrassBladeTipUnit
        public uint Tip;
        // In 8 bits each from the lowest: how much it bends; its flutter's phase; how far it is on its
        // way to the far blade's shape; how dark its clump makes it
        public uint Shape;
        public uint TipColor;
        public uint RootColor;
        // The ground's normal, packed, in the low 16 bits; its kind in the high
        public uint Extras;
    }

    public static const float GrassBladeHeightUnit = 16.0 / 65535.0;
    public static const float GrassBladeWidthUnit = 2.0 / 65535.0;
    public static const float GrassBladeTipUnit = 32.0 / 65535.0;
    public static const float GrassMorphUnit = 16.0 / 4095.0;

    // A kind of grass's values (VulkanGrass's twin)
    public struct GrassKind
    {
        public float Height;
        public float HeightVariance;
        public float Width;
        public float WidthVariance;
        // In radians
        public float Lean;
        public float LeanVariance;
        public float Bend;
        public float ClumpSize;
        public float ClumpHeight;
        public float ClumpFacing;
        public float ClumpPull;
        public float ClumpColor;
        public float PatchSize;
        public float PatchHeight;
        public float PatchDryness;
        public float ColorVariance;
        public float3 RootColor;
        public float WindResponse;
        public float3 TipColor;
        public float Stiffness;
        public float3 DryColor;
        public float Flutter;
        public float NormalRounding;
        public float SelfShadow;
        public float Translucency;
        public float Roughness;
        public float FullDensityDistance;
        public float Range;
        public uint Seed;
        public float RootToGround;
    }

    // Mixes two numbers and a seed into 32 bits that look random
    public uint grassHash(uint a, uint b, uint seed)
    {
        uint h = a * 1597334677u ^ b * 3812015801u ^ seed * 2798796415u;
        h = (h ^ (h >> 15)) * 2246822519u;
        h = (h ^ (h >> 13)) * 3266489917u;
        return h ^ (h >> 16);
    }

    // 0 up to 1 from a hash's bits, `shift` of them dropped
    public float grassRandom(uint hash, uint shift = 0)
    {
        return float((hash >> shift) & 65535u) / 65536.0;
    }

    // A unit vector in the upper half of the sphere, as x and z, 8 bits each
    public uint packGrassNormal(float3 n)
    {
        return uint(saturate(n.x * 0.5 + 0.5) * 255.0 + 0.5) | (uint(saturate(n.z * 0.5 + 0.5) * 255.0 + 0.5) << 8);
    }

    public float3 unpackGrassNormal(uint packed)
    {
        const float x = float(packed & 255u) / 255.0 * 2.0 - 1.0;
        const float z = float((packed >> 8) & 255u) / 255.0 * 2.0 - 1.0;
        return float3(x, sqrt(saturate(1.0 - x * x - z * z)), z);
    }

    // A linear colour, its square roots in 10 bits each, so its dark greens don't band; bright past 1
    // is cut off
    public uint packGrassColor(float3 color)
    {
        const float3 c = sqrt(saturate(color)) * 1023.0 + 0.5;
        return uint(c.x) | (uint(c.y) << 10) | (uint(c.z) << 20);
    }

    public float3 unpackGrassColor(uint packed)
    {
        const float3 c = float3(float(packed & 1023u), float((packed >> 10) & 1023u), float((packed >> 20) & 1023u)) / 1023.0;
        return c * c;
    }

    // Smooth noise over the ground, 0 to 1, a patch about each unit of `place`
    public float grassPatch(float2 place, uint seed)
    {
        const float2 cell = floor(place);
        const float2 along = place - cell;
        const float2 smooth = along * along * (3.0 - 2.0 * along);
        const int x = int(cell.x);
        const int z = int(cell.y);
        const float a = grassRandom(grassHash(uint(x), uint(z), seed));
        const float b = grassRandom(grassHash(uint(x + 1), uint(z), seed));
        const float c = grassRandom(grassHash(uint(x), uint(z + 1), seed));
        const float d = grassRandom(grassHash(uint(x + 1), uint(z + 1), seed));
        return lerp(lerp(a, b, smooth.x), lerp(c, d, smooth.x), smooth.y);
    }

    // GetGrassDrawnShare() in Grass.cpp
    public float grassDrawnShare(float fullDensityDistance, float range, float distance)
    {
        const float falling = min(fullDensityDistance / max(distance, 0.001), 1.0);
        const float last = saturate((range - distance) / (range * 0.3));
        return falling * last * last * (3.0 - 2.0 * last);
    }
}
