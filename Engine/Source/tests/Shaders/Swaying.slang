// A material with a vertex function: its vertices lean along x by their weight (the mesh's
// Custom.x) at their instance's phase (InstanceCustom.x). NyxShaderCompilerTests checks that it
// gets the vertex function's entry points.
import nyx.surface;

struct Swaying : ISurface, ISurfaceVertex
{
    [Category("Surface"), Color] float3 BaseColor = float3(0.3, 0.6, 0.2);
    [Category("Wind")] float Strength = 0.1;

    SurfaceOutput shade(SurfaceInput input)
    {
        SurfaceOutput o = SurfaceOutput::defaults(input);
        o.BaseColor = BaseColor;
        return o;
    }

    void displace(inout SurfaceVertex vertex)
    {
        vertex.OffsetWS.x = Strength * vertex.Custom.x * sin(vertex.Time + vertex.InstanceCustom.x * 6.2831853);
    }
}
