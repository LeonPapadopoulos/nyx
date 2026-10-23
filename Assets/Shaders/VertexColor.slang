// A lit surface in its mesh's vertex colours, e.g. a generated ground's grass, earth and stone
// (GroundComponent). The colours are linear; the tint multiplies them.

import nyx.surface;

struct VertexColor : ISurface
{
    [Category("Surface"), Color] float3 Tint = float3(1.0, 1.0, 1.0);
    [Category("Surface"), Range(0, 1)] float Metallic = 0.0;
    [Category("Surface"), Range(0, 1)] float Roughness = 0.9;

    SurfaceOutput shade(SurfaceInput input)
    {
        SurfaceOutput o = SurfaceOutput::defaults(input);
        o.BaseColor = input.Color * Tint;
        o.Metallic = Metallic;
        o.Roughness = Roughness;
        return o;
    }
}
