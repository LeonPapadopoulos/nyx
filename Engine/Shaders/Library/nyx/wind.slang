// The prevailing wind: WindState and nyx::sampleWind(), as Wind.h has them on the CPU
// (Docs/Design/wind.md). Only the contract: it reads nothing of the view, so any shader can sample
// a wind it is given; the view's own wind and a game's local air are nyx.worldwind's.
//
// Every step is as Wind.cpp does it, in the same order: only additions, multiplications, floors and
// integer hashes, each product rounded alone (no fused multiply-adds: windMul()), so the CPU and the
// GPU agree to the bit. Change both together.

// The pattern of the gust fronts and eddies (WindPattern in Wind.h): the way its fronts travel; its
// fronts a metre, the patches of their strength a metre across; how far its eddies have been
// carried by the state's Time, and its eddies a metre
public struct WindPattern
{
    public float2 Direction;
    public float GustsPerMetre;
    public float GustPatchesPerMetre;
    public float2 EddyOffset;
    public float EddiesPerMetre;
    public float Padding;
}

// Laid out in whole 16 bytes, so the uniform, scalar and storage layouts are all the same
public struct WindState
{
    // Where it blows to, in the world's x and z, normalised; metres a second at the reference height
    // between gusts (0: calm); how much faster at the strongest front's peak, as a part of it
    public float2 Direction;
    public float Speed;
    public float Gustiness;
    // Fronts passed a place by Time (wrapped), and a second; the eddies' part of Speed; how far it has
    // faded from the pattern From to To (1: To only); how fast the eddies are carried
    public float GustPhase;
    public float GustRate;
    public float Turbulence;
    public float PatternBlend;
    public float2 EddyVelocity;
    // Always 0: what windMul() XORs each product's bits with, so the compiler can't fuse it
    public float Exact;
    public float Padding1;
    public WindPattern From;
    public WindPattern To;
    // The height profile (GetWindHeightFactor() in Wind.h)
    public float GroundHeight;
    public float GroundFactor;
    public float InverseReferenceHeight;
    public float Aloft;
    public float AloftStart;
    public float InverseAloftSpan;
    // The world's time the phases and offsets are at
    public float Time;
    public float Padding2;
}

namespace nyx
{
    static const uint WindGustCycles = 1024;
    static const uint WindEddyCycles = 1024;

    // A product rounded on its own: its bits XORed with `zero` (the state's Exact, always 0 but not
    // known to the compiler), so no driver can fuse it into a multiply-add. `precise` would ask for
    // that, but this Slang doesn't tell SPIR-V (NoContraction).
    float windMul(float a, float b, uint zero)
    {
        return asfloat(asuint(a * b) ^ zero);
    }

    uint windHash(uint x, uint y)
    {
        uint h = (x * 0x8DA6B343u) ^ (y * 0xD8163841u);
        h ^= h >> 16;
        h *= 0x7FEB352Du;
        h ^= h >> 15;
        h *= 0x846CA68Bu;
        h ^= h >> 16;
        return h;
    }

    float windUnit16(uint bits, uint z)
    {
        return windMul(float(bits & 0xFFFFu), 1.0 / 65536.0, z);
    }

    uint windCell(float whole)
    {
        return uint(int(whole));
    }

    float windSmooth(float t, uint z)
    {
        const float b = 3.0 - windMul(2.0, t, z);
        return windMul(windMul(t, t, z), b, z);
    }

    float windSaturate(float value)
    {
        return min(max(value, 0.0), 1.0);
    }

    float windLerp(float a, float b, float w, uint z)
    {
        return a + windMul(b - a, w, z);
    }

    // The height profile alone: what Speed is multiplied by at the height `y`
    public float windHeightFactor(WindState wind, float y)
    {
        const uint z = asuint(wind.Exact);
        const float height = y - wind.GroundHeight;
        const float q = windSaturate(windMul(height, wind.InverseReferenceHeight, z));
        const float near = wind.GroundFactor + windMul(1.0 - wind.GroundFactor, windMul(q, 2.0 - q, z), z);
        const float e = windSaturate(windMul(height, wind.InverseAloftSpan, z) - wind.AloftStart);
        return near + windMul(wind.Aloft, windMul(e, 2.0 - e, z), z);
    }

    float windGust(WindPattern pattern, float3 position, float phase, uint z)
    {
        const float along = windMul(position.x, pattern.Direction.x, z) + windMul(position.z, pattern.Direction.y, z);
        const float a = windMul(along, pattern.GustsPerMetre, z) - phase;
        const float front = floor(a);
        const float u = a - front;
        // A front arrives sharply (u falling from 1 to 0.8 as it passes) and dies away slowly
        const float t = u > 0.8 ? windMul(1.0 - u, 5.0, z) : windMul(u, 1.25, z);
        const float shape = windSmooth(t, z);

        // Its strength, from patch to patch along the front
        const float across = windMul(position.z, pattern.Direction.x, z) - windMul(position.x, pattern.Direction.y, z);
        const float c = windMul(across, pattern.GustPatchesPerMetre, z);
        const float patch = floor(c);
        const float blend = windSmooth(c - patch, z);
        const uint index = windCell(front) & (WindGustCycles - 1u);
        const float s0 = windUnit16(windHash(index, windCell(patch)), z);
        const float s1 = windUnit16(windHash(index, windCell(patch) + 1u), z);
        return windMul(shape, windLerp(s0, s1, blend, z), z);
    }

    float2 windCorner(uint h, uint z)
    {
        return float2(windMul(windUnit16(h, z), 2.0, z) - 1.0, windMul(windUnit16(h >> 16, z), 2.0, z) - 1.0);
    }

    // The eddies' swirl at a point, each part -1 to 1
    float2 windEddies(WindPattern pattern, float2 move, float3 position, uint z)
    {
        const float offsetX = pattern.EddyOffset.x + move.x;
        const float offsetZ = pattern.EddyOffset.y + move.y;
        const float qx = windMul(position.x - offsetX, pattern.EddiesPerMetre, z);
        const float qz = windMul(position.z - offsetZ, pattern.EddiesPerMetre, z);
        const float cellX = floor(qx);
        const float cellZ = floor(qz);
        const float wx = windSmooth(qx - cellX, z);
        const float wz = windSmooth(qz - cellZ, z);
        const uint x0 = windCell(cellX) & (WindEddyCycles - 1u);
        const uint z0 = windCell(cellZ) & (WindEddyCycles - 1u);
        const uint x1 = (x0 + 1u) & (WindEddyCycles - 1u);
        const uint z1 = (z0 + 1u) & (WindEddyCycles - 1u);
        const float2 c00 = windCorner(windHash(x0, z0), z);
        const float2 c10 = windCorner(windHash(x1, z0), z);
        const float2 c01 = windCorner(windHash(x0, z1), z);
        const float2 c11 = windCorner(windHash(x1, z1), z);
        const float ax = windLerp(c00.x, c10.x, wx, z);
        const float az = windLerp(c00.y, c10.y, wx, z);
        const float bx = windLerp(c01.x, c11.x, wx, z);
        const float bz = windLerp(c01.y, c11.y, wx, z);
        return float2(windLerp(ax, bx, wz, z), windLerp(az, bz, wz, z));
    }

    float windBlendedGust(WindState wind, float3 position, float time)
    {
        const uint z = asuint(wind.Exact);
        const float phase = wind.GustPhase + windMul(wind.GustRate, time - wind.Time, z);
        const float to = windGust(wind.To, position, phase, z);
        if (wind.PatternBlend >= 1.0)
        {
            return to;
        }
        const float from = windGust(wind.From, position, phase, z);
        return windLerp(from, to, wind.PatternBlend, z);
    }

    float2 windBlendedEddies(WindState wind, float3 position, float time)
    {
        const uint z = asuint(wind.Exact);
        const float since = time - wind.Time;
        const float2 move = float2(windMul(wind.EddyVelocity.x, since, z), windMul(wind.EddyVelocity.y, since, z));
        const float2 to = windEddies(wind.To, move, position, z);
        if (wind.PatternBlend >= 1.0)
        {
            return to;
        }
        const float2 from = windEddies(wind.From, move, position, z);
        return float2(windLerp(from.x, to.x, wind.PatternBlend, z), windLerp(from.y, to.y, wind.PatternBlend, z));
    }

    // How strongly a gust front is blowing at `position` at `time`: 0 between fronts, up to 1 at the
    // peak of the strongest
    public float sampleWindGust(WindState wind, float3 position, float time)
    {
        if (!(wind.Speed > 0.0))
        {
            return 0.0;
        }
        return windBlendedGust(wind, position, time);
    }

    // The wind's velocity at `position` (metres, in the world) at the world's `time` (seconds), in
    // metres a second, along the ground (y is 0)
    public float3 sampleWind(WindState wind, float3 position, float time)
    {
        if (!(wind.Speed > 0.0))
        {
            return float3(0.0, 0.0, 0.0);
        }
        const uint z = asuint(wind.Exact);
        const float speed = windMul(wind.Speed, windHeightFactor(wind, position.y), z);
        const float gusting = windMul(speed, 1.0 + windMul(wind.Gustiness, windBlendedGust(wind, position, time), z), z);
        const float2 swirl = windBlendedEddies(wind, position, time);
        const float swirling = windMul(speed, wind.Turbulence, z);
        const float x = windMul(wind.Direction.x, gusting, z) + windMul(swirl.x, swirling, z);
        const float across = windMul(wind.Direction.y, gusting, z) + windMul(swirl.y, swirling, z);
        return float3(x, 0.0, across);
    }
}
