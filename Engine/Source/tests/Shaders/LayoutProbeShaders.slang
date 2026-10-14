// Compute shaders that read LayoutProbe like surfaceFragment does: probeLayout writes every scalar
// out in field order, probeStore stores the whole struct back. probeRawHandle shows why
// nyx::Texture holds a plain index rather than Slang's DescriptorHandle.
import nyx.surface;
import LayoutProbe;

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

// Slang's own handle type at an offset that isn't a multiple of 8
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
