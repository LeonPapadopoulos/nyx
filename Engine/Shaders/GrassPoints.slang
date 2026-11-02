// The grass's ground points of a tile, made only when the ground changes (VulkanGrass). Each triangle
// of the surface's mesh that faces up marks the cells of the world's grid of blades it lies over, so
// every layer of ground, under an overhang too, gets its own; a blade's x and z come from its cell
// alone, so a mesh made again moves only the blades whose ground moved.

import nyx.grass;

struct MeshVertex
{
    float3 Position;
    float3 Color;
    float2 UV;
    float3 Normal;
    float4 Tangent;
    uint Custom[2];
}

[vk::binding(0, 0)] StructuredBuffer<MeshVertex> Vertices;
[vk::binding(1, 0)] StructuredBuffer<uint> Indices;
[vk::binding(2, 0)] RWStructuredBuffer<nyx::GrassPoint> Points;
[vk::binding(3, 0)] RWStructuredBuffer<nyx::GrassTileCount> Tiles;

// A scatter blocker reaching the tile (ScatterBlocker, GetBlockerDensity())
struct Blocker
{
    float4x4 WorldToShape;
    float3 Size;
    float Margin;
    float Falloff;
    // ScatterBlocker::EKind: a circle, a box, a sphere, a capsule
    uint Kind;
    uint2 _Padding;
}
[vk::binding(4, 0)] StructuredBuffer<Blocker> Blockers;

struct Tile
{
    float4x4 Model;
    uint Triangles;
    uint FirstPoint;
    uint Capacity;
    uint Slot;
    float CellSize;
    // In the low 2 bits the level of the grid it keeps (Grass.h); from bit 8 its first blocker, 12
    // bits, and how many, 12 bits
    uint LevelAndBlockers;
    uint Seed;
    uint MaterialMask;
    float2 NormalYRange;
    float2 HeightRange;
    float ClumpSize;
    // Its corner across x and z, and how wide it is
    float2 Corner;
    float Size;
}
[vk::push_constant] ConstantBuffer<Tile> Constants;

static const uint GridLevels = 4;

// GetGrassCellLevel() in Grass.cpp
uint cellLevel(int column, int row)
{
    const uint bits = uint(column) | uint(row);
    uint level = 0;
    while (level + 1 < GridLevels && (bits & (1u << level)) == 0)
    {
        ++level;
    }
    return level;
}

// GetGrassRank() in Grass.cpp
float rankOf(uint level, float random)
{
    const float top = 1.0 / float(1u << (2u * level));
    const float bottom = level + 1 < GridLevels ? top * 0.25 : 0.0;
    return bottom + (top - bottom) * random;
}

// The nearest clump's cell, from the cell the point is in: each cell has one clump, somewhere in it
int2 nearestClump(float2 place)
{
    const int2 cell = int2(floor(place / Constants.ClumpSize));
    float best = 1.0e9;
    int2 nearest = int2(0, 0);
    for (int z = -1; z <= 1; ++z)
    {
        for (int x = -1; x <= 1; ++x)
        {
            const int2 other = cell + int2(x, z);
            const uint hash = nyx::grassHash(uint(other.x), uint(other.y), Constants.Seed + 1u);
            const float2 centre = (float2(other) + float2(nyx::grassRandom(hash), nyx::grassRandom(hash, 16))) * Constants.ClumpSize;
            const float2 away = centre - place;
            const float distance = dot(away, away);
            if (distance < best)
            {
                best = distance;
                nearest = int2(x, z);
            }
        }
    }
    return nearest;
}

float blockerDensity(Blocker blocker, float3 point)
{
    const float3 inShape = mul(blocker.WorldToShape, float4(point, 1.0)).xyz;
    float distance;
    if (blocker.Kind == 1u)
    {
        const float3 beyond = abs(inShape) - blocker.Size;
        distance = length(max(beyond, 0.0)) + min(max(beyond.x, max(beyond.y, beyond.z)), 0.0);
    }
    else if (blocker.Kind == 2u)
    {
        distance = length(inShape) - blocker.Size.x;
    }
    else if (blocker.Kind == 3u)
    {
        distance = length(float3(inShape.x, inShape.y - clamp(inShape.y, -blocker.Size.y, blocker.Size.y), inShape.z)) - blocker.Size.x;
    }
    else
    {
        distance = length(inShape.xz) - blocker.Size.x;
    }
    if (distance <= blocker.Margin)
    {
        return 0.0;
    }
    return blocker.Falloff > 0.0 ? smoothstep(blocker.Margin, blocker.Margin + blocker.Falloff, distance) : 1.0;
}

// The material number in a vertex's half float (Vertex::Custom.y), without half floats on the device
float materialOf(MeshVertex vertex)
{
    const uint half = vertex.Custom[0] >> 16;
    const uint exponent = (half >> 10) & 31u;
    if (exponent == 0u)
    {
        return 0.0;
    }
    return float(1u << exponent) / 32768.0 * (1.0 + float(half & 1023u) / 1024.0);
}

[shader("compute")]
[numthreads(32, 1, 1)]
void grassPoints(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Constants.Triangles)
    {
        return;
    }
    const MeshVertex v0 = Vertices[Indices[id.x * 3]];
    const MeshVertex v1 = Vertices[Indices[id.x * 3 + 1]];
    const MeshVertex v2 = Vertices[Indices[id.x * 3 + 2]];
    const float3 n0 = normalize(mul((float3x3)Constants.Model, v0.Normal));
    const float3 n1 = normalize(mul((float3x3)Constants.Model, v1.Normal));
    const float3 n2 = normalize(mul((float3x3)Constants.Model, v2.Normal));
    // None of it faces up enough
    if (max(n0.y, max(n1.y, n2.y)) < Constants.NormalYRange.x)
    {
        return;
    }
    const float3 p0 = mul(Constants.Model, float4(v0.Position, 1.0)).xyz;
    const float3 p1 = mul(Constants.Model, float4(v1.Position, 1.0)).xyz;
    const float3 p2 = mul(Constants.Model, float4(v2.Position, 1.0)).xyz;
    const float2 ab = p1.xz - p0.xz;
    const float2 ac = p2.xz - p0.xz;
    const float area = ab.x * ac.y - ab.y * ac.x;
    if (abs(area) < 1.0e-6)
    {
        return;
    }

    // The cells over it of the grid the tile keeps; a blade may lie anywhere in its cell
    const int step = 1 << (Constants.LevelAndBlockers & 3u);
    const uint firstBlocker = (Constants.LevelAndBlockers >> 8) & 4095u;
    const uint blockers = Constants.LevelAndBlockers >> 20;
    const float2 low = min(p0.xz, min(p1.xz, p2.xz)) / Constants.CellSize;
    const float2 high = max(p0.xz, max(p1.xz, p2.xz)) / Constants.CellSize;
    const int firstColumn = int(floor(low.x / float(step))) * step;
    const int firstRow = int(floor(low.y / float(step))) * step;
    const int lastColumn = int(floor(high.x));
    const int lastRow = int(floor(high.y));
    for (int row = firstRow; row <= lastRow; row += step)
    {
        for (int column = firstColumn; column <= lastColumn; column += step)
        {
            const uint hash = nyx::grassHash(uint(column), uint(row), Constants.Seed);
            const float2 place = (float2(column, row) + float2(nyx::grassRandom(hash), nyx::grassRandom(hash, 16))) * Constants.CellSize;
            const float2 ap = place - p0.xz;
            const float b = (ap.x * ac.y - ap.y * ac.x) / area;
            const float c = (ab.x * ap.y - ab.y * ap.x) / area;
            const float a = 1.0 - b - c;
            if (a < 0.0 || b < 0.0 || c < 0.0)
            {
                continue;
            }
            const float3 normal = normalize(n0 * a + n1 * b + n2 * c);
            if (normal.y < Constants.NormalYRange.x || normal.y > Constants.NormalYRange.y + 0.001)
            {
                continue;
            }
            const float y = p0.y * a + p1.y * b + p2.y * c;
            if (y < Constants.HeightRange.x || y > Constants.HeightRange.y)
            {
                continue;
            }
            // The material of the corner it is nearest, so a border between two falls between blades
            const float material = a >= b && a >= c ? materialOf(v0) : (b >= c ? materialOf(v1) : materialOf(v2));
            if ((Constants.MaterialMask & (1u << (uint(material + 0.5) & 31u))) == 0)
            {
                continue;
            }

            // What the blockers leave, by a number of its own
            if (blockers > 0u)
            {
                float density = 1.0;
                for (uint blocker = 0u; blocker < blockers; ++blocker)
                {
                    density *= blockerDensity(Blockers[firstBlocker + blocker], float3(place.x, y, place.y));
                }
                if (nyx::grassRandom(nyx::grassHash(uint(column), uint(row), Constants.Seed + 7u)) >= density)
                {
                    continue;
                }
            }

            uint index = 0;
            InterlockedAdd(Tiles[Constants.Slot].Count, 1u, index);
            if (index >= Constants.Capacity)
            {
                continue;
            }

            const uint random = nyx::grassHash(uint(column), uint(row), Constants.Seed + 2u);
            const float rank = rankOf(cellLevel(column, row), nyx::grassRandom(random));
            const int2 clump = nearestClump(place);
            const float slide = (v0.Tangent.y * a + v1.Tangent.y * b + v2.Tangent.y * c) * length(Constants.Model[1].xyz);
            const float2 across = saturate((place - Constants.Corner) / Constants.Size) * 65535.0 + 0.5;

            nyx::GrassPoint point;
            point.Place = uint(across.x) | (uint(across.y) << 16);
            point.Y = y;
            point.NormalRandom = nyx::packGrassNormal(normal) | (random & 0xFFFF0000u);
            point.Ground = nyx::packGrassColor(v0.Color * a + v1.Color * b + v2.Color * c);
            point.RankClump = uint(saturate(rank) * 65535.0) | (uint(clump.x + 1) << 16) | (uint(clump.y + 1) << 18) | (uint(clamp(slide / nyx::GrassMorphUnit + 2048.0, 0.0, 4095.0)) << 20);
            Points[Constants.FirstPoint + index] = point;
        }
    }
}

// After the tile's triangles: its count kept to what it holds, and the groups of the pass over them
[shader("compute")]
[numthreads(1, 1, 1)]
void grassPointsDone()
{
    const uint count = min(Tiles[Constants.Slot].Count, Constants.Capacity);
    Tiles[Constants.Slot].Count = count;
    Tiles[Constants.Slot].Groups = (count + 63u) / 64u;
    Tiles[Constants.Slot].GroupsY = 1u;
    Tiles[Constants.Slot].GroupsZ = 1u;
}
