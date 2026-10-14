// A material whose fields sit at awkward offsets on purpose: under scalar layout no vector and no
// texture lands on a multiple of its own size. LayoutProbeShaders.slang loads it from the material
// buffer the way surfaceFragment does; NyxShaderCompilerGpuTests fills the buffer at the offsets
// reflection reports and compares.
import nyx.surface;

struct Pair
{
    float A;
    float3 B;
}

struct LayoutProbe : ISurface
{
    float F;              // scalar  0, std430  0
    float3 V3;            // scalar  4, std430 16
    float G;
    float2 V2;            // scalar 20: not a multiple of 8
    int I;
    bool Flag;
    float4 V4;            // scalar 36: not a multiple of 16
    nyx::Texture Texture; // scalar 52
    float3 Tail;
    float Last;
    Pair Nested;
    // No array: materials can't have arrays yet (Slang 2026.1 crashes on them; WithArray.slang)

    SurfaceOutput shade(SurfaceInput input) { return SurfaceOutput::defaults(input); }
}
