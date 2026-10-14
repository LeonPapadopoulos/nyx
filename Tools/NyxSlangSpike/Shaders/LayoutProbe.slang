// A material whose fields sit at awkward offsets on purpose: under scalar layout no vector and no
// texture lands on a multiple of its own size. Compute shaders load it from the material buffer
// the way surfaceFragment does, and write every scalar out in field order (probeLayout), or store
// it back whole (probeStore). NyxSlangSpike fills the buffer at the offsets reflection reports and
// compares.
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
    float Array[3];       // stride 4 under std430 and scalar, 16 under std140

    SurfaceOutput shade(SurfaceInput input) { return SurfaceOutput::defaults(input); }
}

[vk::binding(3, 0)] RWByteAddressBuffer ProbeOut;

[shader("compute")]
[numthreads(1, 1, 1)]
void probeLayout()
{
    LayoutProbe p = MaterialBuffer.Load<LayoutProbe>(Object.MaterialOffset);
    uint i = 0;
    ProbeOut.Store((i++) * 4, asuint(p.F));
    ProbeOut.Store((i++) * 4, asuint(p.V3.x)); ProbeOut.Store((i++) * 4, asuint(p.V3.y)); ProbeOut.Store((i++) * 4, asuint(p.V3.z));
    ProbeOut.Store((i++) * 4, asuint(p.G));
    ProbeOut.Store((i++) * 4, asuint(p.V2.x)); ProbeOut.Store((i++) * 4, asuint(p.V2.y));
    ProbeOut.Store((i++) * 4, asuint(p.I));
    ProbeOut.Store((i++) * 4, p.Flag ? 1u : 0u);
    ProbeOut.Store((i++) * 4, asuint(p.V4.x)); ProbeOut.Store((i++) * 4, asuint(p.V4.y)); ProbeOut.Store((i++) * 4, asuint(p.V4.z)); ProbeOut.Store((i++) * 4, asuint(p.V4.w));
    ProbeOut.Store((i++) * 4, p.Texture.Index);
    ProbeOut.Store((i++) * 4, asuint(p.Tail.x)); ProbeOut.Store((i++) * 4, asuint(p.Tail.y)); ProbeOut.Store((i++) * 4, asuint(p.Tail.z));
    ProbeOut.Store((i++) * 4, asuint(p.Last));
    ProbeOut.Store((i++) * 4, asuint(p.Nested.A));
    ProbeOut.Store((i++) * 4, asuint(p.Nested.B.x)); ProbeOut.Store((i++) * 4, asuint(p.Nested.B.y)); ProbeOut.Store((i++) * 4, asuint(p.Nested.B.z));
    ProbeOut.Store((i++) * 4, asuint(p.Array[0])); ProbeOut.Store((i++) * 4, asuint(p.Array[1])); ProbeOut.Store((i++) * 4, asuint(p.Array[2]));

    // The model matrix from the push constants, as surfaceVertex uses it: must match glm's
    // model * vec4(1, 2, 3, 1)
    float4 moved = mul(Object.Model, float4(1.0, 2.0, 3.0, 1.0));
    ProbeOut.Store((i++) * 4, asuint(moved.x)); ProbeOut.Store((i++) * 4, asuint(moved.y)); ProbeOut.Store((i++) * 4, asuint(moved.z)); ProbeOut.Store((i++) * 4, asuint(moved.w));
}

// Load, then Store the whole struct: the bytes must come out where they went in
[shader("compute")]
[numthreads(1, 1, 1)]
void probeStore()
{
    ProbeOut.Store<LayoutProbe>(0, MaterialBuffer.Load<LayoutProbe>(Object.MaterialOffset));
}

// Slang's own handle type at an offset that isn't a multiple of 8: shows why nyx::Texture holds a
// plain index instead (check 5)
struct RawHandleProbe
{
    float A;
    DescriptorHandle<Texture2D> Handle; // scalar 4
}

[shader("compute")]
[numthreads(1, 1, 1)]
void probeRawHandle()
{
    RawHandleProbe p = MaterialBuffer.Load<RawHandleProbe>(Object.MaterialOffset);
    uint2 handle = (uint2)p.Handle;
    ProbeOut.Store(0, asuint(p.A));
    ProbeOut.Store(4, handle.x);
    ProbeOut.Store(8, handle.y);
}
